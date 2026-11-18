#include "admission.hpp"
#include "process.hpp"
#include <atomic>
#include <cassert>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <exception>
using namespace voyemu;
using namespace voyemu::api;
namespace {
const Json &payload() {
    static const Json value = {{"op", "input.tap"}, {"x", 1}};

    return value;
}

template <class F> void rejects(const char *label, F action) {
    try {
        action();
    } catch (const std::exception &error) {
        std::cout << "PASS " << label << ": " << error.what() << std::endl;

        return;
    }

    throw std::runtime_error(std::string("guard accepted: ") + label);
}

std::filesystem::path record(const std::filesystem::path &root, const std::string &id,
                             const std::string &suffix) {
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
        auto name = entry.path().filename().string();

        if (name.size() == 64 + suffix.size() && name.starts_with(id) && name.ends_with(suffix)) {
            return entry.path();
        }
    }

    throw std::runtime_error("record missing");
}

void overwrite(const std::filesystem::path &path, const std::string &text) {
    std::ofstream file(path, std::ios::binary);

    file << text;
    assert(file.good());
}

bool make_insecure(const std::filesystem::path &path) {
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(world);

    assert(CreateWellKnownSid(WinWorldSid, nullptr, world, &size));
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_ALL_ACCESS;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(world);
    PACL acl = nullptr;
    assert(SetEntriesInAclW(1, &access, nullptr, &acl) == ERROR_SUCCESS);
    const auto result =
            SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, acl, nullptr);
    LocalFree(acl);

    if (result != ERROR_SUCCESS) {
        return false;
    }

    PACL observed = nullptr;
    PSECURITY_DESCRIPTOR security = nullptr;

    if (GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr,
                              nullptr, &observed, nullptr, &security) != ERROR_SUCCESS) {
        return false;
    }

    bool confirmed = false;

    if (observed) {
        for (DWORD i = 0; i < observed->AceCount; ++i) {
            void *raw = nullptr;

            if (GetAce(observed, i, &raw) &&
                static_cast<ACE_HEADER *>(raw)->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                auto *ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);

                if (EqualSid(&ace->SidStart, world) && (ace->Mask & FILE_WRITE_DATA)) {
                    confirmed = true;
                }
            }
        }
    }

    if (security) {
        LocalFree(security);
    }

    return confirmed;
}

bool make_foreign_owner(const std::filesystem::path &path) {
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof(world);

    assert(CreateWellKnownSid(WinWorldSid, nullptr, world, &size));
    const auto changed = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT,
                                               OWNER_SECURITY_INFORMATION, world, nullptr, nullptr,
                                               nullptr);
    PSID owner = nullptr;
    PSECURITY_DESCRIPTOR security = nullptr;
    const auto observed = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT,
                                                OWNER_SECURITY_INFORMATION, &owner, nullptr,
                                                nullptr, nullptr, &security);
    const bool confirmed = changed == ERROR_SUCCESS && observed == ERROR_SUCCESS && owner &&
                           EqualSid(owner, world);

    if (security) {
        LocalFree(security);
    }

    return confirmed;
}
} // namespace

int main(int argc, char **argv) try {
    if (argc == 3 && std::string_view(argv[1]) == "--worker") {
        try {
            AdmissionStore store(argv[2]);

            return store.begin("session", "process-race", payload()).admitted ? 10 : 0;
        } catch (const std::exception &error) {
            std::cerr << error.what() << std::endl;

            return 4;
        }
    }

    if (argc == 3 && std::string_view(argv[1]) == "--crash-reservation") {
        const std::filesystem::path path(argv[2]);
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        assert(file != INVALID_HANDLE_VALUE);
        OVERLAPPED position{};
        assert(LockFileEx(file, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &position));
        DWORD count = 0;
        assert(WriteFile(file, "{", 1, &count, nullptr) && count == 1 && SetEndOfFile(file) &&
               FlushFileBuffers(file));
        TerminateProcess(GetCurrentProcess(), 42);

        return 5;
    }

    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) / ("windows-admission-" + random_token());
    try {
        AdmissionStore store(root);
        auto first = store.begin("session", "request", payload());
        assert(first.admitted && first.operation_id.size() == 32);
        AdmissionStore reopened(root);
        auto again = reopened.begin("session", "request", Json{{"x", 1}, {"op", "input.tap"}});
        assert(!again.admitted && again.operation_id == first.operation_id &&
               again.status == "pending");
        rejects("payload conflict", [&] {
            store.begin("session", "request", Json{{"x", 2}});
        });
        Json reply = {{"ok", true}, {"ownership_id", "public-owner"}};
        store.complete("session", "request", first.operation_id, reply);
        store.complete("session", "request", first.operation_id, reply);
        assert(reopened.begin("session", "request", payload()).response == reply &&
               reopened.lookup("session", "request").value().response == reply &&
               !store.lookup("session", "missing"));
        rejects("terminal result conflict", [&] {
            store.complete("session", "request", first.operation_id, Json{{"ok", false}});
        });
        {
            const auto path = record(root, first.operation_id, ".lock");
            HANDLE held = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            assert(held != INVALID_HANDLE_VALUE);
            OVERLAPPED position{};
            assert(LockFileEx(held, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &position));
            auto waiting = std::async(std::launch::async, [&] {
                return reopened.begin("session", "request", payload());
            });
            assert(waiting.wait_for(std::chrono::milliseconds(100)) == std::future_status::timeout);
            CloseHandle(held);
            assert(waiting.get().response == reply);
        }
        auto nested = root / "nested/a/b";
        AdmissionStore nested_store(nested);
        assert(!nested_store.lookup("nested", "missing"));
        nested_store.begin("nested", "request", payload());
        std::filesystem::remove_all(root / "nested");
        rejects("missing ancestry not recreated", [&] {
            nested_store.lookup("nested", "request");
        });
        assert(!std::filesystem::exists(root / "nested"));
        auto large = store.begin("session", "large", payload());
        Json large_reply = {{"report", std::string(256 * 1024, 'x')}};
        store.complete("session", "large", large.operation_id, large_reply);
        assert(reopened.lookup("session", "large").value().response == large_reply);
        rejects("payload extent", [&] {
            store.begin("session", "large-payload",
                        Json{{"payload", std::string(128 * 1024, 'x')}});
        });
        assert(!store.lookup("session", "large-payload"));
        auto excessive = store.begin("session", "excessive", payload());
        rejects("receipt extent", [&] {
            store.complete("session", "excessive", excessive.operation_id,
                           Json{{"report", std::string(8 * 1024 * 1024, 'x')}});
        });
        assert(reopened.lookup("session", "excessive").value().status == "pending");
        std::atomic<unsigned> winners{0};
        std::vector<std::thread> threads;

        for (unsigned i = 0; i < 16; ++i) {
            threads.emplace_back([&] {
                AdmissionStore own(root);

                if (own.begin("session", "thread-race", payload()).admitted) {
                    ++winners;
                }
            });
        }

        for (auto &thread : threads) {
            thread.join();
        }

        assert(winners == 1);
        std::vector<std::future<ProcessResult>> processes;

        for (unsigned i = 0; i < 8; ++i) {
            processes.push_back(std::async(std::launch::async, [&] {
                ProcessSpec spec;
                spec.executable = std::filesystem::canonical(argv[0]);
                spec.directory = root;
                spec.arguments = {"--worker", root.string()};

                return execute(spec, 10000);
            }));
        }

        unsigned admitted = 0;

        for (auto &process : processes) {
            auto result = process.get();
            assert(!result.timed_out && (result.exit_code == 0 || result.exit_code == 10));
            admitted += result.exit_code == 10;
        }

        assert(admitted == 1);
        auto reserved = store.begin("session", "reserved", payload());
        std::filesystem::remove(record(root, reserved.operation_id, ".json"));
        assert(!reopened.begin("session", "reserved", payload()).admitted &&
               reopened.lookup("session", "reserved").value().status == "pending");
        auto partial = store.begin("session", "partial", payload());
        auto partial_path = record(root, partial.operation_id, ".lock");
        overwrite(partial_path, "{");
        assert(reopened.begin("session", "partial", payload()).status == "unknown");
        rejects("partial reservation cannot complete", [&] {
            store.complete("session", "partial", partial.operation_id, reply);
        });
        {
            auto crashed = store.begin("session", "crashed", payload());
            ProcessSpec child;
            child.executable = std::filesystem::canonical(argv[0]);
            child.directory = root;
            child.arguments = {"--crash-reservation",
                               record(root, crashed.operation_id, ".lock").string()};
            auto result = execute(child, 10000);
            assert(!result.timed_out && result.exit_code == 42);
            assert(reopened.begin("session", "crashed", payload()).status == "unknown");
            rejects("crashed reservation cannot complete", [&] {
                store.complete("session", "crashed", crashed.operation_id, reply);
            });
        }
        auto corrupt = store.begin("session", "corrupt", payload());
        overwrite(record(root, corrupt.operation_id, ".json"), "{");
        rejects("corrupt terminal", [&] {
            store.begin("session", "corrupt", payload());
        });
        auto mismatched = store.begin("session", "mismatched", payload());
        auto mismatched_path = record(root, mismatched.operation_id, ".json");
        auto state = read_private_json(mismatched_path);
        state["digest"] = "foreign-payload";
        overwrite(mismatched_path, state.dump());
        rejects("valid JSON receipt association", [&] {
            store.begin("session", "mismatched", payload());
        });
        auto oversize = store.begin("session", "oversize", payload());
        overwrite(record(root, oversize.operation_id, ".json"),
                  std::string(8 * 1024 * 1024 + 1, 'x'));
        rejects("oversize terminal", [&] {
            store.begin("session", "oversize", payload());
        });
        auto pending = store.begin("session", "pending", payload());
        auto pending_path = record(root, pending.operation_id, ".json");
        pending_path.replace_extension(".pending");
        overwrite(pending_path, "interrupted");
        rejects("interrupted pending not overwritten", [&] {
            store.complete("session", "pending", pending.operation_id, reply);
        });
        assert(store.lookup("session", "pending").value().status == "pending");
        std::filesystem::remove(pending_path);
        store.complete("session", "pending", pending.operation_id, reply);
        auto linked = store.begin("session", "hardlink", payload());
        const auto linked_path = record(root, linked.operation_id, ".lock");
        std::filesystem::create_hard_link(linked_path, root / "alias.lock");
        rejects("hardlinked reservation", [&] {
            store.begin("session", "hardlink", payload());
        });
        std::filesystem::remove(root / "alias.lock");
        auto symbolic = store.begin("session", "symbolic", payload());
        auto symbolic_path = record(root, symbolic.operation_id, ".lock");
        std::filesystem::remove(symbolic_path);
        std::error_code error;
        std::filesystem::create_symlink(partial_path, symbolic_path, error);

        if (error) {
            std::cout << "SKIP reservation reparse fixture: " << error.message() << std::endl;
        } else {
            rejects("reparse reservation", [&] {
                store.begin("session", "symbolic", payload());
            });
        }

        const auto alias = root / "linked-parent";
        error.clear();
        std::filesystem::create_directory_symlink(root, alias, error);

        if (error) {
            std::cout << "SKIP ancestry reparse fixture: " << error.message() << std::endl;
        } else {
            rejects("reparse ancestry", [&] {
                AdmissionStore denied(alias / "store");
            });
            assert(!std::filesystem::exists(root / "store"));
            std::filesystem::remove(alias);
        }

        const auto insecure = root / "insecure";
        AdmissionStore private_store(insecure);

        if (make_insecure(insecure)) {
            rejects("foreign write parent ACL", [&] {
                AdmissionStore denied(insecure / "store");
            });
            rejects("non-private store ACL", [&] {
                AdmissionStore denied(insecure);
            });
            assert(!std::filesystem::exists(insecure / "store"));
        } else {
            std::cout << "SKIP insecure ACL mutation: readback did not confirm Everyone write"
                      << std::endl;
        }

        const auto foreign = root / "foreign-owner";
        AdmissionStore foreign_store(foreign);

        if (make_foreign_owner(foreign)) {
            rejects("foreign owner", [&] {
                AdmissionStore denied(foreign);
            });
        } else {
            std::cout << "SKIP foreign owner rejection: assignment not confirmed by SID readback"
                      << std::endl;
        }

        std::filesystem::remove_all(root);
        std::cout
                << "PASS durable Windows admission: replay/conflict, 16 threads, 8 processes, bounded receipts and fail-closed recovery/integrity"
                << std::endl;

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL Windows admission: " << error.what() << " root=" << root.string()
                  << std::endl;

        return 4;
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
