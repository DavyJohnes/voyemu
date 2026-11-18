#include "os/platform.hpp"
#include "os/process.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

using namespace voyemu;

namespace {
void check(bool value, const std::string &message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

// The host names select this host's own pinned dependencies and SDK binary directories; another
// host's directory, such as darwin-x86_64 on Linux, would select binaries this host cannot run.
int main() {
    try {
        check(!os::dependency_host.empty(), "build host without pinned dependencies");
        check(os::dependency_host.starts_with(std::string(os::dependency_system) + "/"),
              "dependency system differs from the dependency host");
        const std::map<std::string_view, std::string_view> directories{
                {"darwin", "darwin-"}, {"linux", "linux-"}, {"win32", "windows-"}};
        const auto prefix = directories.at(os::dependency_system);
        check(os::emulator_host.starts_with(prefix), "Emulator directory of another host");
        check(os::ndk_host.starts_with(prefix), "NDK directory of another host");
        // This test executable is a host executable.
        std::ifstream self(os::executable_path(), std::ios::binary);
        std::string magic(4, '\0');
        self.read(magic.data(), 4);
        const std::string elf{'\x7f', 'E', 'L', 'F'};
        const auto format = magic.starts_with("MZ") ? "PE" : magic == elf ? "ELF" : "Mach-O";
        check(self && format == os::executable_format, "executable format of another host");
        std::cout << "host " << os::dependency_host << " selects " << os::emulator_host << " and "
                  << os::ndk_host << '\n';

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
