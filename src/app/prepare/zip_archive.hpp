#pragma once
#include "zip_layout.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace voyemu {
// The SHA-256 of a large pinned input such as an owner update archive. A file whose identity, its
// volume, inode, size and modification and change times, is unchanged since this process hashed it
// is not hashed again. advance receives the bytes of a hash that runs; without it a hash of at
// least 256 MiB shows its own console meter.
std::string pinned_file_sha256(const std::filesystem::path &file,
                               const std::function<void(std::uint64_t)> &advance = {});
struct ZipEntry {
    std::string name;
    std::uint64_t bytes;
    bool directory;
};
std::vector<ZipEntry> pinned_zip_inventory(const std::filesystem::path &,
                                           const std::string &archive_sha256,
                                           ZipNamePolicy policy = ZipNamePolicy::portable);
// Reads a declared file only; verifies complete archive and selected entry pins.
std::string read_pinned_zip_entry(const std::filesystem::path &, const std::string &archive_sha256,
                                  const std::string &name, const std::string &entry_sha256,
                                  std::size_t maximum = 32 * 1024 * 1024);
// Reads bytes whose identity is covered by the complete pinned archive.
std::string read_pinned_zip_entry(const std::filesystem::path &, const std::string &archive_sha256,
                                  const std::string &name, std::size_t maximum,
                                  ZipNamePolicy policy = ZipNamePolicy::portable);
struct ZipExtraction {
    std::string name;
    std::uint64_t offset, bytes;
};
// Streams declared members of one pinned ZIP into a fresh output file of the exact declared extent,
// each member at its own offset. Decoded content is verified against the entry CRC, all-zero blocks
// are not written so sparse scatter targets keep their holes, and the archive pin is checked twice.
// Entry SHA256 belongs to the caller, which re-reads the published file. advance receives the byte
// count of each decoded block, for a progress meter.
void extract_pinned_zip_members(const std::filesystem::path &source,
                                const std::string &source_sha256,
                                const std::vector<ZipExtraction> &members,
                                const std::filesystem::path &output, std::uint64_t output_bytes,
                                std::uint64_t reserve_bytes,
                                const std::function<bool()> &cancelled = {},
                                const std::function<void(std::uint64_t)> &advance = {});
struct ZipTreeEntry {
    std::string name;
    std::filesystem::path output;
    std::uint64_t bytes;
};
// Extract selected regular members to fresh paths under an owned staging tree. The exact name
// policy admits case aliases, which then extract only to a case-sensitive volume.
void extract_pinned_zip_tree(const std::filesystem::path &source, const std::string &source_sha256,
                             const std::vector<ZipTreeEntry> &members, std::uint64_t reserve_bytes,
                             ZipNamePolicy policy = ZipNamePolicy::portable);
// The target text of each symbolic link member by member name, at most 4096 bytes each and checked
// against its CRC.
std::map<std::string, std::string> pinned_zip_link_targets(const std::filesystem::path &source,
                                                           const std::string &source_sha256);
struct ZipReplacement {
    std::string name, original_sha256, bytes, sha256;
};
struct ZipAddition {
    std::string name, bytes, sha256;
};
// Rewrites a bounded APK candidate. Entry payloads are preserved, compression may change.
// Original APK signatures are invalidated; alignment/signing must follow separately.
std::string rewrite_pinned_zip(const std::filesystem::path &source,
                               const std::string &source_sha256,
                               const std::vector<ZipReplacement> &replacements,
                               const std::vector<std::string> &removals,
                               const std::vector<ZipAddition> &additions,
                               const std::filesystem::path &destination,
                               std::uint64_t reserve_bytes = 12ULL << 30);
} // namespace voyemu
