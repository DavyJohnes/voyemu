#pragma once
#include "download.hpp"
#include "model.hpp"
#include <functional>
#include <string>
#include <vector>
namespace voyemu {
// Source-only Free A9 has 1,388,636,623 pinned image bytes; no guest RAM file is created.
inline constexpr std::uint64_t qnx_qinggan09_disk_floor = 8ULL << 30;
inline constexpr std::uint64_t qnx_qinggan09_guest_workspace = 1ULL << 30;
inline constexpr std::uint64_t qnx_qinggan09_image_budget = 1536ULL << 20;

inline constexpr bool qnx_qinggan09_source_space_available(std::uint64_t available) {
    return available >=
           qnx_qinggan09_disk_floor + qnx_qinggan09_guest_workspace + qnx_qinggan09_image_budget;
}

// Source-only Free A11 preparation retains the runtime disk floor without allocating RAM.
inline constexpr std::uint64_t qnx_qinggan11_disk_floor = 8ULL << 30;
inline constexpr std::uint64_t qnx_qinggan11_guest_workspace = 1ULL << 30;
// Pinned images total 1,233,648,436 bytes; allow the full 256 MiB IFS extent.
inline constexpr std::uint64_t qnx_qinggan11_image_budget = 1400ULL << 20;

inline constexpr bool qnx_qinggan11_source_space_available(std::uint64_t available) {
    return available >=
           qnx_qinggan11_disk_floor + qnx_qinggan11_guest_workspace + qnx_qinggan11_image_budget;
}

enum class QnxGuestAdapter {
    qinggan09,
    qinggan11,
    passion21480,
    qinggan11611
};
// The guest ldqnx preloader reads a full page per PT_GNU_RELRO segment; a file ending short of the
// last relro page fails preload fatally.
bool elf64_gnu_relro_covers_page_rounded_read(const std::filesystem::path &file);
// Key of the adapter in qnxGuestModules of toolchain.json, its hardware_adapter.
std::string qnx_guest_adapter_name(QnxGuestAdapter adapter);
// Files of the module set of the adapter, in release asset order.
std::vector<std::string> qnx_guest_module_files(QnxGuestAdapter adapter);
// Digest of the current module pins of the adapter named hardware_adapter; a source build receipt
// records the module set it staged as guest_build.modules_digest.
std::string qnx_guest_modules_digest(std::string_view hardware_adapter);
// Size of the section of the Free A9 front module that preparation fills with OEM data.
inline constexpr std::uint64_t qnx_qinggan09_oem_capacity = 176ULL << 10;
// The recipe of the module set: compiles the shipped guest sources and pinned headers with the
// clang and ld.lld of the pinned Zig into fresh_directory. It takes no OEM input: imports stay
// undefined for the QNX runtime linker, and the Free A9 front module reserves its OEM section.
Json build_qnx_guest_recipe(const std::filesystem::path &root, const std::filesystem::path &zig,
                            const std::filesystem::path &fresh_directory, bool offline,
                            QnxGuestAdapter adapter, const Json &initial_native_state);
// Installs the module set of the adapter pinned in pins (its qnxGuestModules entry) into
// .local/toolchains/qnx-guest/<digest> and returns that directory. It downloads the CI-built
// release assets; while they cannot be downloaded, build (by default the recipe with the installed
// pinned Zig) produces them instead. Both paths are checked against the same pins, and an installed
// set is checked on every call.
std::filesystem::path
install_qnx_guest_modules(const std::filesystem::path &root, QnxGuestAdapter adapter,
                          const Json &pins, bool offline, const DownloadTransport &transport = {},
                          const std::function<void(const std::filesystem::path &)> &build = {});
// Writes the OEM startup baseline and the packed OEM sysmgrtool into the reserved section of the
// Free A9 front module as plain data.
std::string qnx_qinggan09_front_oem_data(std::string_view front, std::string_view startup_baseline,
                                         std::string_view sysmgrtool_original);
// Stages the installed module set into fresh_directory, checks its imports against the OEM
// libc.so.4 and libscreen.so.1 and fills the Free A9 OEM section.
Json build_qnx_guest_modules(const std::filesystem::path &root, std::string_view libc,
                             std::string_view screen, const std::filesystem::path &fresh_directory,
                             bool offline = false,
                             QnxGuestAdapter adapter = QnxGuestAdapter::qinggan09,
                             const Json &initial_native_state = Json::object(),
                             std::string_view startup_baseline = {},
                             std::string_view sysmgrtool_original = {});
} // namespace voyemu
