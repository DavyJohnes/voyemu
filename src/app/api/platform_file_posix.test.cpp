#include "model.hpp"
#include "platform_file.hpp"
#include "process.hpp"
#include <cassert>
#include <cstdlib>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <sys/file.h>
#include <unistd.h>

using namespace voyemu;
using namespace voyemu::api;
namespace {
template <class F> void rejects(std::string_view label, F action, std::string_view code) {
    std::string observed = "accepted";

    try {
        action();
    } catch (const std::exception &error) {
        observed = error.what();

        if (std::string_view(observed).starts_with(code)) {
            std::cout << "PASS " << label << ": " << observed << std::endl;

            return;
        }
    }

    std::cerr << "FAIL " << label << ": expected " << code << ", observed " << observed
              << std::endl;
    std::abort();
}

void metadata(const std::filesystem::path &path) {
    (void)path;
}

void write(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path, std::ios::binary);

    file << value;
    assert(file.good());
}

bool change_owner(const std::filesystem::path &path) {
    return ::chown(path.c_str(), ::geteuid() + 1, static_cast<gid_t>(-1)) == 0;
}
} // namespace

int main(int argc, char **argv) try {
    if (argc == 3 && std::string_view(argv[1]) == "--probe") {
        return file_lock_released(argv[2]) ? 0 : 7;
    }

    if (argc == 3 && std::string_view(argv[1]) == "--acquire") {
        try {
            FileLock lock(argv[2]);

            return 0;
        } catch (const std::exception &error) {
            return std::string_view(error.what()).starts_with("TEST_BUSY:") ? 7 : 8;
        }
    }

    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) / ("api-platform-file-" + random_token());
    ensure_private_directory(root);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::filesystem::remove_all(path);
        }
    } cleanup{root};
    auto child = [&](const char *mode, const std::filesystem::path &path) {
        ProcessSpec spec;
        spec.executable = std::filesystem::canonical(argv[0]);
        spec.directory = root;
        spec.arguments = {mode, path.string()};
        auto result = execute(spec, 5000);
        assert(!result.timed_out);

        return result.exit_code;
    };
    const auto lock_path = root / "writer.lock";
    assert(!file_lock_released(lock_path) && !std::filesystem::exists(lock_path));
    {
        FileLock lock(lock_path);
        assert(!file_lock_released(lock_path));
        assert(child("--probe", lock_path) == 7);
        assert(child("--acquire", lock_path) == 7);
    }
    assert(file_lock_released(lock_path));
    assert(child("--probe", lock_path) == 0);
    assert(child("--acquire", lock_path) == 0);
    // Match the existing CLI supervisor lock, not just another helper instance.
    const int supervisor = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
                                  0600);
    assert(supervisor >= 0 && ::flock(supervisor, LOCK_EX | LOCK_NB) == 0);
    assert(child("--probe", lock_path) == 7);
    assert(child("--acquire", lock_path) == 7);
    assert(::close(supervisor) == 0);
    assert(child("--probe", lock_path) == 0);
    const auto log_path = root / "supervisor.jsonl";
    const std::string content = std::string(65536, 'a') + "tail\n";
    write(log_path, content);
    {
        LogFile log(log_path);
        assert(log.size() == content.size() && !log.identity().empty());
        assert(log.read(0) == content.substr(0, 65536));
        assert(log.read(65536) == "tail\n");
        assert(log.read(content.size()).empty());
        rejects(
                "overflow offset",
                [&] {
                    log.read(std::numeric_limits<std::uint64_t>::max());
                },
                "TEST_LOGS:");
        const auto identity = log.identity();
        std::filesystem::rename(log_path, root / "old.jsonl");
        write(log_path, "replacement\n");
        LogFile replacement(log_path);
        assert(replacement.identity() != identity && replacement.read(0) == "replacement\n");
        assert(log.identity() == identity && log.read(65536) == "tail\n");
    }
    rejects(
            "missing log",
            [&] {
                LogFile log(root / "missing");
            },
            "TEST_LOGS_UNAVAILABLE:");
    rejects(
            "directory log",
            [&] {
                LogFile log(root);
            },
            "TEST_LOGS");
    std::error_code error;
    std::filesystem::create_hard_link(log_path, root / "hardlink", error);

    if (error) {
        std::cout << "SKIP hardlink creation: " << error.message() << '\n';
    } else {
        metadata(log_path);
        metadata(root / "hardlink");
        rejects(
                "hardlinked original log",
                [&] {
                    LogFile log(log_path);
                },
                "TEST_LOGS:");
        rejects(
                "hardlink log",
                [&] {
                    LogFile log(root / "hardlink");
                },
                "TEST_LOGS:");
        rejects(
                "hardlinked lock",
                [&] {
                    FileLock lock(log_path);
                },
                "TEST_BUSY:");
        assert(!file_lock_released(log_path));
        std::filesystem::remove(root / "hardlink");
    }

    error.clear();
    std::filesystem::create_symlink(log_path, root / "symlink", error);

    if (error) {
        std::cout << "SKIP symlink/reparse creation: " << error.message() << '\n';
    } else {
        metadata(root / "symlink");
        rejects(
                "symlink log",
                [&] {
                    LogFile log(root / "symlink");
                },
                "TEST_LOGS");
        rejects(
                "symlink lock",
                [&] {
                    FileLock lock(root / "symlink");
                },
                "TEST_BUSY:");
        assert(!file_lock_released(root / "symlink"));
    }

    const auto foreign = root / "foreign";
    write(foreign, "foreign\n");

    if (!change_owner(foreign)) {
        metadata(foreign);
        std::cout
                << "SKIP foreign owner rejection: foreign owner assignment not confirmed by readback\n";
    } else {
        metadata(foreign);
        rejects(
                "foreign owner log",
                [&] {
                    LogFile log(foreign);
                },
                "TEST_LOGS:");
        rejects(
                "foreign owner lock",
                [&] {
                    FileLock lock(foreign);
                },
                "TEST_BUSY:");
        assert(!file_lock_released(foreign));
    }

    std::cout
            << "PASS interprocess exclusive lock and supervisor release; bounded handle-bound log reads; rejection checks above\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
