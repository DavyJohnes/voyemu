#include "main_host.hpp"
#include "wine_native_backend_windows.hpp"
#include <string_view>
namespace voyemu::cli {
std::optional<int> host_command(int argc, char **argv) {
    if (argc > 1 && std::string_view(argv[1]) == "_wine-native") {
        return voyemu::wine_native_proxy_main(argc - 1, argv + 1);
    }

    return std::nullopt;
}

void ignore_broken_pipe() {}
} // namespace voyemu::cli
