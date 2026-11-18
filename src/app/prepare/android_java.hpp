#pragma once
#include "download.hpp"
#include "model.hpp"
#include <filesystem>
namespace voyemu {
// Installs android-java.zip, the CI-built guest Java of the profiles pinned in pin (androidJava of
// toolchain.json), into .local/toolchains/android-java/<sha256>/ and returns it. While the asset
// cannot be downloaded it returns an empty path, and the Java helper compiles the guest Java on its
// JDK.
std::filesystem::path install_android_java(const std::filesystem::path &root, const Json &pin,
                                           bool offline, std::uint64_t reserve,
                                           const DownloadTransport &transport = {});
} // namespace voyemu
