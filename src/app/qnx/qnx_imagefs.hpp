#pragma once
#include <string>
#include <string_view>
namespace voyemu {
// Extracts a declared regular file from a checksum-verified QNX ImageFS image.
std::string read_qnx_imagefs_file(std::string_view image, std::string_view relative);
// Replaces an existing bounded slot; ELF payloads can be page aligned in that slot.
std::string replace_qnx_imagefs_file(std::string_view image, std::string_view relative,
                                     std::string_view original_sha256, std::string_view payload,
                                     unsigned alignment = 1, bool retain_size = false);
// Extends a pinned existing slot into validated unused zero padding; no file is moved.
std::string replace_qnx_imagefs_file_in_zero_padding(std::string_view image,
                                                     std::string_view relative,
                                                     std::string_view original_sha256,
                                                     std::string_view payload, unsigned max_growth);
std::string rename_qnx_imagefs_file(std::string_view image, std::string_view before,
                                    std::string_view after);
// Moves the first declared member's tail out of the final staging block the
// guest overwrites: the member grows by `padding` zero bytes, every later
// member and the extent follow; no payload byte changes.
std::string pad_qnx_imagefs_first_member(std::string_view image, unsigned padding);
// Resolves an exact two-file duplicate path by SHA; the other payload remains under the original
// name.
std::string rename_qnx_imagefs_duplicate_file(std::string_view image, std::string_view before,
                                              std::string_view after,
                                              std::string_view payload_sha256);
} // namespace voyemu
