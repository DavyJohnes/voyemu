#include "fs.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>
#ifndef _WIN32
#include <atomic>
#include <fcntl.h>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#endif

namespace {
int failures = 0;

void expect(bool actual, bool expected, const char *path) {
    if (actual != expected) {
        std::fprintf(stderr, "volume_root(%s) != %d\n", path, expected ? 1 : 0);
        ++failures;
    }
}

void root(const char *path, bool expected) {
    expect(voyemu::os::volume_root(path), expected, path);
}

void walk(const char *path, std::initializer_list<const char *> expected) {
    const auto actual = voyemu::os::ancestors(path);
    bool same = actual.size() == expected.size();

    for (std::size_t i = 0; same && i < actual.size(); ++i) {
        same = actual[i] == std::filesystem::path(expected.begin()[i]);
    }

    if (!same) {
        std::fprintf(stderr, "ancestors(%s) differs (%zu paths)\n", path, actual.size());
        ++failures;
    }
}

// Shared holders coexist and exclude an exclusive one; an exclusive holder excludes both kinds.
void locks() {
    const auto file = std::filesystem::temp_directory_path() /
                      ("voyemu-fs-lock-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto check = [](bool actual, const char *what) {
        if (!actual) {
            std::fprintf(stderr, "FileLock: %s\n", what);
            ++failures;
        }
    };
    {
        const voyemu::os::FileLock first(file, false), second(file, false);
        check(first.held() && second.held(), "shared holders must coexist");
        const voyemu::os::FileLock writer(file, true);
        check(!writer.held(), "shared holder must exclude an exclusive one");
    }
    {
        const voyemu::os::FileLock writer(file, true);
        check(writer.held(), "released shared holders must admit an exclusive one");
        const voyemu::os::FileLock reader(file, false), other(file, true);
        check(!reader.held() && !other.held(), "exclusive holder must exclude every other");
    }

    check(voyemu::os::FileLock(file, false).held(),
          "released exclusive holder must admit a reader");
    // A lock file that cannot be opened, such as one left by another user, is an error that names
    // the file, not a running holder.
    std::string unopenable;

    try {
        (void)voyemu::os::FileLock(file / "absent", false).held();
    } catch (const std::exception &error) {
        unopenable = error.what();
    }

    check(unopenable.starts_with("LOCK_FILE: ") &&
                  unopenable.find((file / "absent").string()) != std::string::npos,
          "unopenable path must throw LOCK_FILE naming it");
    std::filesystem::remove(file);
}

#ifndef _WIN32
// Writers of a directory take its lock in turn: a holder that releases and takes it again waits
// behind a writer that arrived meanwhile, and a writer waits in the kernel for another holder until
// its deadline.
void directory_locks() {
    using std::chrono::milliseconds;
    using voyemu::os::DirectoryLock;
    const auto check = [](bool actual, const char *what) {
        if (!actual) {
            std::fprintf(stderr, "DirectoryLock: %s\n", what);
            ++failures;
        }
    };
    const auto after = [](int ms) {
        return std::chrono::steady_clock::now() + milliseconds(ms);
    };
    const auto directory =
            std::filesystem::temp_directory_path() /
            ("voyemu-fs-directory-lock-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    check(DirectoryLock(directory / "absent", after(100)).status() ==
                  DirectoryLock::Status::unavailable,
          "an absent directory must be unavailable");
    std::atomic<bool> stop = false, holding = false;
    std::atomic<unsigned> takes = 0;
    std::thread rival([&] {
        while (!stop) {
            const DirectoryLock lock(directory, after(5000));

            if (lock.status() == DirectoryLock::Status::held) {
                ++takes;
                holding = true;
                std::this_thread::sleep_for(milliseconds(5));
            }
        }
    });

    while (!holding) {
        std::this_thread::sleep_for(milliseconds(1));
    }

    const unsigned before = takes;
    {
        const DirectoryLock waiter(directory, after(2000));
        check(waiter.status() == DirectoryLock::Status::held && takes - before <= 1,
              "a writer that takes the lock again must not overtake a waiter");
        stop = true;
    }
    rival.join();
    const int other = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    check(other >= 0 && ::flock(other, LOCK_EX) == 0, "another holder must lock the directory");
    check(DirectoryLock(directory, after(100)).status() == DirectoryLock::Status::timeout,
          "a writer must give up at its deadline");
    std::thread release([&] {
        std::this_thread::sleep_for(milliseconds(100));
        ::close(other);
    });
    check(DirectoryLock(directory, after(5000)).status() == DirectoryLock::Status::held,
          "a writer must wait for the other holder");
    release.join();
    // The wait that gave up releases the lock it may take after its deadline.
    const int probe = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    bool free = false;

    for (int attempt = 0; attempt < 100 && !free; ++attempt) {
        free = ::flock(probe, LOCK_EX | LOCK_NB) == 0;

        if (!free) {
            std::this_thread::sleep_for(milliseconds(10));
        }
    }

    check(free, "a wait that gave up must not keep the lock");
    ::close(probe);
    std::filesystem::remove(directory);
}
#endif

void exclusive_files() {
    const auto directory =
            std::filesystem::temp_directory_path() /
            ("voyemu-fs-create-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    std::filesystem::create_directory(directory);
    const auto path = directory / "claim";
    {
        voyemu::os::ExclusiveFile first(path, true);

        if (!first.open() || first.already_exists() || !first.write("owner", 5) || !first.sync()) {
            ++failures;
        }

        voyemu::os::ExclusiveFile second(path, true);

        if (second.open() || !second.already_exists()) {
            ++failures;
        }

        std::ifstream reader(path);
        std::string text;
        reader >> text;

        if (text != "owner") {
            ++failures;
        }

        voyemu::os::ExclusiveFile absent(directory / "absent" / "file");

        if (absent.open() || absent.already_exists()) {
            ++failures;
        }
    }
    std::filesystem::remove_all(directory);
}

// Sizes past 2 GiB and 4 GiB keep all 64 bits: a 32-bit size fails above 2 GiB and wraps a
// multiple of 4 GiB to zero. Growth keeps the content and reads back zeros in the hole.
void resized_files() {
    const auto path = std::filesystem::temp_directory_path() /
                      ("voyemu-fs-resize-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        std::ofstream file(path, std::ios::binary);
        file << 'x';
    }

    for (const std::uint64_t bytes : {(2ULL << 30) + 1, (4ULL << 30) + 1, 4ULL << 30, 1ULL}) {
        voyemu::os::resize_file(path, bytes);
        std::ifstream file(path, std::ios::binary);
        const char first = static_cast<char>(file.get());
        file.seekg(static_cast<std::streamoff>(bytes - 1));
        const int last = file.get();

        if (std::filesystem::file_size(path) != bytes || first != 'x' ||
            last != (bytes == 1 ? 'x' : 0)) {
            std::fprintf(stderr, "resize_file(%llu) differs\n",
                         static_cast<unsigned long long>(bytes));
            ++failures;
        }
    }

    bool rejected = false;

    try {
        voyemu::os::resize_file(path.string() + "-absent", 1);
    } catch (const std::filesystem::filesystem_error &) {
        rejected = true;
    }

    if (!rejected) {
        std::fprintf(stderr, "resize_file of an absent file must throw\n");
        ++failures;
    }

    std::filesystem::remove(path);
}
} // namespace

int main() {
    try {
        locks();
#ifndef _WIN32
        directory_locks();
#endif
        exclusive_files();
        resized_files();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FileLock: %s\n", error.what());
        ++failures;
    }

    root("", false);
    root("relative", false);
    root("relative/child", false);
    walk("", {});
    walk("relative/child", {"relative/child", "relative"});
#ifdef _WIN32
    root("C:\\", true);
    root("c:/", true);
    root("Z:\\", true);
    root("C:\\Users", false);
    root("Z:\\home\\user", false);
    root("C:", false);
    root("C:relative", false);
    root("\\\\server\\share", true);
    root("\\\\server\\share\\", true);
    root("//server/share", true);
    root("\\\\server\\", true);
    root("\\\\server\\share\\dir", false);
    // Every ancestor walk ends below the volume root and never inspects the server alone.
    walk("C:\\", {});
    walk("C:\\Users\\voyemu", {"C:\\Users\\voyemu", "C:\\Users"});
    walk("Z:\\home\\user", {"Z:\\home\\user", "Z:\\home"});
    walk("\\\\server\\share", {});
    walk("\\\\server\\share\\a\\b", {"\\\\server\\share\\a\\b", "\\\\server\\share\\a"});
    walk("C:relative", {"C:relative", "C:"});
#else
    root("/", true);
    root("/home", false);
    root("/home/user/", false);
    walk("/", {});
    walk("/home/user", {"/home/user", "/home"});
#endif

    return failures == 0 ? 0 : 1;
}
