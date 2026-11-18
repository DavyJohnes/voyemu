#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace voyemu {
struct QnxLoaderSegment {
    std::filesystem::path file;
    std::uint64_t offset, bytes;
    std::string sha256;
};
std::vector<QnxLoaderSegment> split_qnx_loader_image(const std::filesystem::path &original,
                                                     std::string_view sha256,
                                                     const std::filesystem::path &fresh_directory,
                                                     std::uint64_t part_bytes = 1ULL << 30,
                                                     std::uint64_t reserve_bytes = 20ULL << 30);
struct QnxIfs2Gpt {
    std::string cache, primary, backup;
};
QnxIfs2Gpt qnx_passion_gpt(std::uint64_t ifs_bytes);
void validate_qnx_passion_gpt(const QnxIfs2Gpt &images, std::uint64_t ifs_bytes);
QnxIfs2Gpt qnx_qinggan11_611_gpt(std::string_view ifs_image, std::string_view sha256);
std::string qnx_qinggan11_611_loader_image(std::string_view ifs_image, std::string_view sha256);
void validate_qnx_qinggan11_611_gpt(const QnxIfs2Gpt &images, std::string_view ifs_image,
                                    std::string_view sha256);
QnxIfs2Gpt qnx_qinggan11_gpt(std::string_view ifs_image, std::string_view sha256);
std::string qnx_qinggan11_loader_image(std::string_view ifs_image, std::string_view sha256);
void validate_qnx_qinggan11_gpt(const QnxIfs2Gpt &images, std::string_view ifs_image,
                                std::string_view sha256);
std::string qnx_synthetic_gpt();
std::string qnx_synthetic_gpt_for_ifs2(std::uint64_t bytes);
// Wraps the unchanged OEM partition in the known QNX RAM-disk GPT layout.
std::filesystem::path prepare_qnx_system_disk(const std::filesystem::path &original,
                                              std::string_view sha256,
                                              const std::filesystem::path &fresh_directory,
                                              std::uint64_t reserve_bytes = 20ULL << 30);
} // namespace voyemu
