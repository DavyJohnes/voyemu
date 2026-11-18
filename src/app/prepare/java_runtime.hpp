#pragma once
#include "download.hpp"
#include "model.hpp"
#include <filesystem>
#include <functional>
namespace voyemu {
// Home of the pinned JDK of the developer tools of this host, installed or not.
std::filesystem::path java_developer_jdk(const std::filesystem::path &root);
// Links the java-runtime archives of every host into output with src/prepare/JavaRuntimeBuild.java
// on the pinned JDK of the developer tools of this host; the recipe downloads the pinned jmods.
void build_java_runtimes(const std::filesystem::path &root, const std::filesystem::path &output);
// Installs the Java runtime of this host, pinned in pins (javaRuntime of toolchain.json), into
// .local/toolchains/java-runtime/<sha256>/ and returns its java executable. It downloads the
// CI-built release asset; while that asset cannot be downloaded, build (by default
// build_java_runtimes) links it into the given directory instead. Both paths are checked against
// the same pin; without a recipe JDK it stops with JAVA_RUNTIME_ASSET_MISSING.
std::filesystem::path
install_java_runtime(const std::filesystem::path &root, const Json &pins, bool offline,
                     std::uint64_t reserve, const DownloadTransport &transport = {},
                     const std::function<void(const std::filesystem::path &)> &build = {});
} // namespace voyemu
