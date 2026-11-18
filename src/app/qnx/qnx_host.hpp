#pragma once
#include "model.hpp"
namespace voyemu {
const Json &qnx_host_catalog();
// Installs the pinned host engine without administrator access.
Json install_qnx_host(const std::filesystem::path &root, bool offline = false,
                      std::uint64_t reserve_bytes = 20ULL << 30);
// Why deps cannot install the QNX engine of this host, empty when it can: on Windows while the
// prebuilt pin is pending and no engine is cached, where deps throws QNX_HOST_ASSET_MISSING.
std::string qnx_host_unavailable(const std::filesystem::path &root);

// Single shared host binding file: macOS or Linux factory keys at the top level, the Windows
// binding under the "windows" section.
inline std::filesystem::path qnx_host_bindings_path(const std::filesystem::path &root) {
    return root / ".local/toolchains/installed.json";
}

// Platform view over the shared binding document: on Windows the "windows" section, else the
// validated legacy adjacent file, else the flat document; the flat document elsewhere.
Json qnx_host_binding_view(const std::filesystem::path &root, const Json &shared);
// Creates local paths and pins from this checkout's build outputs; never replaces explicit
// bindings.
Json ensure_qnx_host_bindings(const std::filesystem::path &root, bool offline = false);
#ifdef __APPLE__
// Installs the published engine of the prebuilt pin in qnx-host.json, or builds it from the recipe
// while that pin is pending or when from_source is set.
Json install_qnx_host_macos(const std::filesystem::path &, bool, std::uint64_t,
                            bool from_source = false);
#endif
// Installs the published Linux x64 engine of the prebuilt pin in qnx-host.json, or builds it from
// the recipe with the pinned Zig while that pin is pending or when from_source is set.
Json install_qnx_host_linux(const std::filesystem::path &, bool, std::uint64_t,
                            bool from_source = false);
// The DT_NEEDED libraries of an x86-64 ELF executable, in file order.
std::vector<std::string> qnx_host_elf_needed(const std::filesystem::path &file);
// Builds the pinned Windows ARM64 QEMU host from source with the pinned MSYS/Zig closure; never
// changes guest images.
Json install_qnx_host_windows_source(const std::filesystem::path &, bool, std::uint64_t);
#ifdef _WIN32
// Read-only MSI/CAB extraction using Windows APIs; only the pinned extractor payload is selected.
void extract_qnx_unpacker(const std::filesystem::path &msi,
                          const std::filesystem::path &destination);
#endif
} // namespace voyemu
