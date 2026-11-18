#include "admission.hpp"
#include <atomic>
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
using namespace voyemu;
using namespace voyemu::api;

template <class F> void rejected(F f) {
    bool caught = false;

    try {
        f();
    } catch (const std::exception &) {
        caught = true;
    }

    assert(caught);
}

std::filesystem::path record(const std::filesystem::path &root, const std::string &operation_id,
                             const std::string &suffix) {
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        auto name = entry.path().filename().string();

        if (name.size() == 64 + suffix.size() && name.starts_with(operation_id) &&
            name.ends_with(suffix)) {
            return entry.path();
        }
    }

    assert(false);

    return {};
}

int main(int argc, char **argv) try {
    assert(argc == 2);
    auto root = std::filesystem::path(argv[1]) / std::to_string(getpid());
    assert(!std::filesystem::exists(root));
    auto insecure = root.string() + "-insecure";
    assert(mkdir(insecure.c_str(), 0755) == 0);
    assert(chmod(insecure.c_str(), 0755) == 0);
    rejected([&] {
        AdmissionStore denied(insecure);
    });
    struct stat mode{};
    assert(stat(insecure.c_str(), &mode) == 0 && (mode.st_mode & 07777) == 0755);
    auto symlink = root.string() + "-symlink";
    std::filesystem::create_symlink(insecure, symlink);
    rejected([&] {
        AdmissionStore denied(symlink);
    });
    assert(stat(insecure.c_str(), &mode) == 0 && (mode.st_mode & 07777) == 0755);
    std::filesystem::remove(symlink);
    std::filesystem::remove(insecure);
    AdmissionStore store(root);
    Json payload = {{"op", "input.tap"}, {"x", 1}};
    assert(stat(root.c_str(), &mode) == 0 && (mode.st_mode & 07777) == 0700);
    auto nested = root / "nonexistent" / "a" / "b";
    AdmissionStore nested_store(nested);
    assert(stat(nested.c_str(), &mode) == 0 && (mode.st_mode & 07777) == 0700);
    assert(stat(nested.parent_path().c_str(), &mode) == 0 && (mode.st_mode & 07777) == 0700);
    assert(!nested_store.lookup("nested", "missing"));
    auto nested_first = nested_store.begin("nested", "request", payload);
    AdmissionStore nested_reopened(nested);
    assert(nested_reopened.lookup("nested", "request").value().operation_id ==
           nested_first.operation_id);
    std::filesystem::remove_all(root / "nonexistent");
    rejected([&] {
        nested_store.lookup("nested", "request");
    });
    assert(!std::filesystem::exists(root / "nonexistent"));
    auto bad_parent = root / "bad-parent";
    assert(mkdir(bad_parent.c_str(), 0700) == 0);
    assert(chmod(bad_parent.c_str(), 0777) == 0);
    rejected([&] {
        AdmissionStore denied(bad_parent / "store");
    });
    assert(!std::filesystem::exists(bad_parent / "store"));
    std::filesystem::remove(bad_parent);
    auto linked_parent = root / "linked-parent";
    std::filesystem::create_directory_symlink(root, linked_parent);
    rejected([&] {
        AdmissionStore denied(linked_parent / "store");
    });
    assert(!std::filesystem::exists(root / "store"));
    std::filesystem::remove(linked_parent);
    auto first = store.begin("session-a", "request", payload);
    assert(first.admitted);
    assert(first.operation_id.size() == 32 &&
           first.operation_id.find_first_not_of("0123456789abcdef") == std::string::npos);
    AdmissionStore reopened(root);
    auto again = reopened.begin("session-a", "request", Json{{"x", 1}, {"op", "input.tap"}});
    assert(!again.admitted && again.operation_id == first.operation_id &&
           again.status == "pending");
    rejected([&] {
        store.begin("session-a", "request", Json{{"x", 2}});
    });
    Json reply = {{"ok", true}, {"ownership_id", "public-owner"}};
    store.complete("session-a", "request", first.operation_id, reply);
    assert(reopened.begin("session-a", "request", payload).response == reply);
    assert(reopened.lookup("session-a", "request").value().response == reply);
    assert(!store.lookup("session-a", "missing"));
    auto large = store.begin("session-a", "large", payload);
    Json large_reply = {{"report", std::string(256 * 1024, 'x')}};
    store.complete("session-a", "large", large.operation_id, large_reply);
    assert(reopened.lookup("session-a", "large").value().response == large_reply);
    assert(!reopened.begin("session-a", "large", payload).admitted);
    rejected([&] {
        store.begin("session-a", "large-payload", Json{{"payload", std::string(128 * 1024, 'x')}});
    });
    assert(!store.lookup("session-a", "large-payload"));
    auto excessive = store.begin("session-a", "excessive", payload);
    rejected([&] {
        store.complete("session-a", "excessive", excessive.operation_id,
                       Json{{"report", std::string(8 * 1024 * 1024, 'x')}});
    });
    assert(!reopened.begin("session-a", "excessive", payload).admitted);
    assert(reopened.lookup("session-a", "excessive").value().status == "pending");
    std::atomic<int> admitted{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([&] {
            AdmissionStore own(root);

            if (own.begin("session-a", "race", payload).admitted) {
                ++admitted;
            }
        });
    }

    for (auto &thread : threads) {
        thread.join();
    }

    assert(admitted == 1);
    std::vector<pid_t> children;

    for (int i = 0; i < 8; ++i) {
        auto child = fork();
        assert(child >= 0);

        if (child == 0) {
            AdmissionStore own(root);
            _exit(own.begin("session-a", "process-race", payload).admitted ? 10 : 0);
        }

        children.push_back(child);
    }

    int winners = 0;

    for (auto child : children) {
        int status = 0;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
        assert(WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 10);

        if (WEXITSTATUS(status) == 10) {
            ++winners;
        }
    }

    assert(winners == 1);
    auto reserved = store.begin("session-a", "reserved", payload);
    std::filesystem::remove(record(root, reserved.operation_id, ".json"));
    assert(reopened.begin("session-a", "reserved", payload).status == "pending");
    auto partial = store.begin("session-a", "partial", payload);
    auto partial_path = record(root, partial.operation_id, ".lock");
    {
        std::ofstream file(partial_path);
        file << "{";
    }
    assert(reopened.begin("session-a", "partial", payload).status == "unknown");
    rejected([&] {
        store.complete("session-a", "partial", partial.operation_id, reply);
    });
    auto oversize = store.begin("session-a", "oversize", payload);
    {
        std::ofstream file(record(root, oversize.operation_id, ".json"));
        file << std::string(8 * 1024 * 1024 + 1, 'x');
    }
    rejected([&] {
        store.begin("session-a", "oversize", payload);
    });
    auto symbolic = store.begin("session-a", "symbolic", payload);
    auto symbolic_path = record(root, symbolic.operation_id, ".lock");
    std::filesystem::remove(symbolic_path);
    std::filesystem::create_symlink(partial_path, symbolic_path);
    rejected([&] {
        store.begin("session-a", "symbolic", payload);
    });
    std::filesystem::remove_all(root);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
