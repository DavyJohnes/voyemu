#pragma once
#include "os/platform.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
struct QnxRamLaunch {
    std::string machine, backend;
    std::vector<std::string> options;
    std::string accelerator;
};
// How the QNX engine runs the AArch64 guest: HVF on Apple Silicon or single-thread TCG on an
// x86-64 host, with the guest RAM in the shared session file on every host.
enum class QnxEngine {
    hvf,
    tcg_file
};
inline constexpr QnxEngine host_qnx_engine = os::platform_name == "macOS" ? QnxEngine::hvf
                                                                          : QnxEngine::tcg_file;
// The file backend of the Windows engine maps only an existing file of the full RAM extent.
inline constexpr bool host_qnx_ram_precreated = os::host_windows;

// The guest CPU model: the host CPU under HVF, the full TCG model otherwise.
inline const char *qnx_engine_cpu(QnxEngine engine) {
    return engine == QnxEngine::hvf ? "host" : "max";
}

QnxRamLaunch qnx_ram_launch(QnxEngine engine, unsigned memory_mib = 8192);
// On Windows a TCG launch needs the verified file backend of the pinned engine.
QnxRamLaunch qnx_persistent_ram_launch(QnxEngine engine, std::string_view system,
                                       std::string_view userdata,
                                       bool windows_file_backend_verified = false);
// Exclusive sparse session RAM of the exact guest extent, created before the owned QEMU.
void create_qnx_ram(const std::filesystem::path &path, std::uint64_t bytes);
bool qnx_ram_clock_options_current(QnxEngine engine, const std::vector<std::string> &arguments);
} // namespace voyemu
