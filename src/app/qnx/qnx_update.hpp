#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
// Restores a factory zstd patch with an explicitly zero-filled reference image.
// The caller supplies an ordered, complete patch stream and exact owner input pins.
void restore_qnx_zero_base_patch(const std::filesystem::path &patch, std::string_view patch_sha256,
                                 std::uint64_t image_bytes, std::string_view image_sha256,
                                 const std::filesystem::path &library,
                                 std::string_view library_sha256,
                                 const std::filesystem::path &fresh_output);
// First owner acquisition validates the OEM MD5 and derives a new SHA256; no adapter/runtime
// acceptance.
std::string acquire_qnx_zero_base_patch(const std::filesystem::path &patch,
                                        std::string_view patch_sha256, std::uint64_t image_bytes,
                                        std::string_view image_md5,
                                        const std::filesystem::path &library,
                                        std::string_view library_sha256,
                                        const std::filesystem::path &fresh_output);
struct QnxUpdateSegment {
    std::filesystem::path patch;
    std::string sha256;
    std::uint64_t bytes{};
    unsigned index{};
};
// advance receives the byte count of each restored image block, for a progress meter.
void restore_qnx_zero_base_segments(const std::vector<QnxUpdateSegment> &segments,
                                    std::uint64_t image_bytes, std::string_view image_sha256,
                                    const std::filesystem::path &library,
                                    std::string_view library_sha256,
                                    const std::filesystem::path &fresh_output,
                                    const std::function<void(std::uint64_t)> &advance = {});
std::string acquire_qnx_zero_base_segments(const std::vector<QnxUpdateSegment> &segments,
                                           std::uint64_t image_bytes, std::string_view image_md5,
                                           const std::filesystem::path &library,
                                           std::string_view library_sha256,
                                           const std::filesystem::path &fresh_output);
} // namespace voyemu
