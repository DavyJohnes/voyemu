#pragma once
#include "download.hpp"
#include "model.hpp"
#include <functional>
namespace voyemu {
// Builds an owned API28/API30 ARM64 native component with a pinned NDK clang; never invokes a shell
// or interpreter. Guest libraries named by guestLibs link as stubs that carry only their soname.
Json build_native_component(const std::filesystem::path &source,
                            const std::filesystem::path &compiler,
                            const std::string &compiler_sha256,
                            const std::filesystem::path &destination,
                            std::uint64_t reserve_bytes = 20ULL << 30,
                            std::function<std::uint64_t()> available_space = {});
// The source tree of a native component: its directory, or with sourceFiles a fresh copy in work
// that adds each declared file of profiles/common/components, named like an APK component's.
std::filesystem::path native_component_sources(const std::filesystem::path &root,
                                               const std::filesystem::path &component,
                                               const std::filesystem::path &work);
// The recipe of the release assets: materializes and builds every native component of every profile
// with the pinned NDK clang into output as <profile>-<component>-<output>; returns the SHA-256 of
// each by <profile>/<component>.
Json build_native_component_recipe(const std::filesystem::path &root,
                                   const std::filesystem::path &compiler,
                                   const std::string &compiler_sha256,
                                   const std::filesystem::path &output, bool offline);
// Installs the native component of a profile pinned in pins (androidNativeComponents of
// toolchain.json) into .local/toolchains/android-native/<sha256>/ and returns the receipt of a
// fresh handoff directory in .local/build that holds the output. It downloads the CI-built asset;
// while it cannot be downloaded, build (by default the recipe with the pinned NDK of the developer
// tools) produces it, and without that NDK it stops with ANDROID_NATIVE_ASSET_MISSING. Both paths
// are checked against the same pin.
std::filesystem::path
install_native_component(const std::filesystem::path &root, const std::string &profile,
                         const std::string &component, const Json &pins, bool offline,
                         const DownloadTransport &transport = {},
                         const std::function<void(const std::filesystem::path &)> &build = {});
} // namespace voyemu
