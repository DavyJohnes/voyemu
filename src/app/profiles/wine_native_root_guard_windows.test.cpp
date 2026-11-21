#include "portable_runtime.hpp"
#include "process.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "wine_native_backend_windows.hpp"

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            return 64;
        }

        using namespace voyemu;

        // Only Wine maps drive roots through reparse points; native Windows skips.
        if (!wine_native_host()) {
            return 77;
        }

        std::filesystem::create_directories(argv[1]);
        // The default Wine Z: drive maps / through the reparse point under test.
        const std::filesystem::path root = std::filesystem::path("Z:") /
                                           std::filesystem::path(wine_native_unix_path(argv[1]))
                                                   .make_preferred(),
                                    file = root / "tool.exe", linked = root / "linked" / "tool.exe";
        auto check = [](bool value, const char *message) {
            if (!value) {
                throw std::runtime_error(message);
            }
        };
        check((GetFileAttributesW(root.root_path().c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
              "Wine drive root fixture required");
        std::filesystem::remove_all(root);
        std::filesystem::create_directories(root);
        std::ofstream(file, std::ios::binary) << "wine native root guard tool\n";
        check(CreateSymbolicLinkW((root / "linked").c_str(), root.c_str(),
                                  SYMBOLIC_LINK_FLAG_DIRECTORY |
                                          SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE) != 0,
              "descendant reparse fixture creation failed");
        PortableRuntimeHostBindings host;
        const auto digest = sha256_file(file);
        host.pins[file.string()] = digest;
        host.pins[linked.string()] = digest;
        auto blocked = [&](const std::filesystem::path &path) {
            try {
                verify_portable_host_tool(root, host, path);

                return false;
            } catch (const std::exception &) {
                return true;
            }
        };
        _putenv_s("VOYEMU_WINE_NATIVE_DIAGNOSTIC", "");
        check(verify_portable_host_tool(root, host, file) == digest,
              "Wine mapped drive root rejected");
        check((GetFileAttributesW((root / "linked").c_str()) & FILE_ATTRIBUTE_REPARSE_POINT) != 0,
              "descendant reparse fixture required");
        check(blocked(linked), "descendant reparse accepted");
        _putenv_s("VOYEMU_WINE_NATIVE_DIAGNOSTIC", "private-test");
        check(verify_portable_host_tool(root, host, file) == digest,
              "diagnostic mapped-root rejected");
        _putenv_s("VOYEMU_WINE_NATIVE_DIAGNOSTIC", "");
        check(blocked(linked), "descendant reparse accepted under cleared opt-in");
        std::cout
                << "PASS production host-tool guard: Wine drive root accepted, descendant reparse rejected, opt-in irrelevant\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
