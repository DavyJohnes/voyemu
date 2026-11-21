#pragma once
#include "model.hpp"
#include <cstdint>
#include <string>
#include <string_view>
namespace voyemu {
// The embedded project file src/app/toolchain.json; the only source of host tool and SDK pins.
const Json &embedded_toolchain_manifest();
// The SHA-256 of file, relative to the SDK root, pinned in sdk.runtimeFilePins for host, such as
// "darwin/arm64" or "win32/x64".
std::string native_runtime_file_pin(std::string_view host, const std::string &file);
// The size of file pinned in sdk.runtimeFilePins.<host>.fileBytes.
std::uint64_t native_runtime_file_bytes(std::string_view host, const std::string &file);
// The pinned official NDK archive of this host, whose inventory an external NDK must match.
const Json &native_ndk_archive();
} // namespace voyemu
