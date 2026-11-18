#include "fs.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <string>

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
    check(!voyemu::os::FileLock(file / "absent", false).held(), "unopenable path must not lock");
    std::filesystem::remove(file);
}

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
} // namespace

int main() {
    try {
        locks();
        exclusive_files();
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
