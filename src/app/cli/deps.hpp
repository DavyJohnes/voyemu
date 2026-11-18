#pragma once
#include "download.hpp"
#include "model.hpp"
#include "toolchain_manifest.hpp"
#include <functional>
namespace voyemu {
struct NativeDependency {
    std::string target, revision;
    DownloadPin pin;
    std::vector<std::string> executables;
};
std::string native_dependency_host();
std::vector<NativeDependency> native_dependencies(std::string_view host);
std::vector<NativeDependency> native_runtime_dependencies(std::string_view host);
// SDK identity is its exact component recipes, independent of unrelated toolchain entries.
bool native_dependency_receipt_matches(const Json &receipt, std::string_view host,
                                       const std::vector<NativeDependency> &expected);
// Consumes a separately pinned preparation audit; validates installed runtime bytes only.
enum class RuntimeSdkUse {
    full_audit,
    launch
};
Json verify_native_runtime_dependencies(const std::filesystem::path &sdk, const Json &binding,
                                        const std::vector<NativeDependency> &expected = {},
                                        RuntimeSdkUse use = RuntimeSdkUse::full_audit);
// Reconstructs an upstream-format inventory without accepting it as a trust anchor.
Json inspect_external_ndk_inventory(const std::filesystem::path &ndk,
                                    const std::string &archive_pin, std::uint64_t archive_bytes);
// A file of this host's NDK LLVM prebuilt bin directory, relative to the NDK root.
std::string native_ndk_tool(std::string_view name);
// Returns the selected NDK and verifies a pinned external inventory when configured.
std::filesystem::path native_ndk_directory(const std::filesystem::path &root);
// Add a pinned versioned build-tools component while retaining the verified previous payload.
// The caller owns the SDK installation lock and supplies independently trusted old/new recipes.
Json upgrade_native_dependency_component(const std::filesystem::path &sdk,
                                         const std::filesystem::path &staging,
                                         const std::string &host,
                                         const std::vector<NativeDependency> &previous,
                                         const std::vector<NativeDependency> &expected,
                                         const std::filesystem::path &archive,
                                         std::uint64_t reserve_bytes = 20ULL << 30,
                                         const std::function<void()> &before_publish = {});
Json install_native_dependencies(const std::filesystem::path &root, bool offline = false,
                                 std::uint64_t reserve_bytes = 20ULL << 30);
} // namespace voyemu
