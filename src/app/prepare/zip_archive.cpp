#include "zip_archive.hpp"
#include "check.hpp"
#include "disk_space.hpp"
#include "interruption.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "quickboot.hpp"
#include "zip_layout.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <map>
#include <miniz.h>
#include <mutex>
#include <set>
#include <stdexcept>
namespace voyemu {
namespace {
bool valid_sha(const std::string &value) {
    return sha256_text(value);
}

struct ZipData {
    std::vector<ZipEntry> entries;
    std::string selected;
    bool found = false;
};

void safe_name(std::string_view name) {
    if (name.empty() || name.size() > 1024 || name.front() == '/' ||
        name.find('\\') != std::string_view::npos || name.find(':') != std::string_view::npos ||
        name.find("//") != std::string_view::npos) {
        throw VoyemuError("ZIP_PATH", "unsafe entry name");
    }

    for (const unsigned char byte : name) {
        if (byte < 32 || byte == 127) {
            throw VoyemuError("ZIP_PATH", "control character");
        }
    }

    if (name.back() == '/') {
        name.remove_suffix(1);
    }

    for (std::size_t start = 0; start < name.size();) {
        const auto end = name.find('/', start);
        const auto part = name.substr(start, end == std::string_view::npos ? name.size() - start
                                                                           : end - start);

        if (part.empty() || part == "." || part == "..") {
            throw VoyemuError("ZIP_PATH", "invalid path component");
        }

        if (end == std::string_view::npos) {
            break;
        }

        start = end + 1;
    }
}

// The failures of decode_zip_member as each caller reports them.
struct ZipDecodeErrors {
    const char *extent, *corrupt, *growth_code, *growth;
};

// Decodes the payload of member from input in process with the vendored miniz decoder and checks
// its CRC. The structural index already validated the local header, so the payload extent, method
// and CRC come from the pinned archive; no host tool, temporary file or process launch is
// involved.
std::string decode_zip_member(std::ifstream &input, const ZipMember &member,
                              const ZipDecodeErrors &errors) {
    input.clear();
    input.seekg(static_cast<std::streamoff>(member.data_offset));
    const auto size = static_cast<std::size_t>(member.bytes);
    std::string data;

    if (member.method == 0) {
        data.assign(size, '\0');
        input.read(data.data(), static_cast<std::streamsize>(size));

        if (static_cast<std::size_t>(input.gcount()) != size) {
            throw VoyemuError("ZIP_DATA", errors.extent);
        }
    } else {
        std::string compressed(static_cast<std::size_t>(member.compressed_bytes), '\0');
        input.read(compressed.data(), static_cast<std::streamsize>(compressed.size()));

        if (static_cast<std::size_t>(input.gcount()) != compressed.size()) {
            throw VoyemuError("ZIP_DATA", errors.extent);
        }

        std::vector<unsigned char> out(size ? size : 1);
        mz_stream stream{};
        stream.next_in = reinterpret_cast<const unsigned char *>(compressed.data());
        stream.avail_in = static_cast<unsigned>(compressed.size());
        stream.next_out = out.data();
        stream.avail_out = static_cast<unsigned>(out.size());

        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            throw VoyemuError("ZIP_DATA", "decoder initialization failed");
        }

        const auto status = mz_inflate(&stream, MZ_FINISH);
        const auto consumed = stream.total_in;
        const auto produced = stream.total_out;
        mz_inflateEnd(&stream);

        if (status == MZ_BUF_ERROR) {
            throw VoyemuError(errors.growth_code, errors.growth);
        }

        if (status != MZ_STREAM_END || produced != size || consumed != compressed.size()) {
            throw VoyemuError("ZIP_DATA", errors.corrupt);
        }

        data.assign(reinterpret_cast<const char *>(out.data()), size);
    }

    if (mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(data.data()),
                 static_cast<std::size_t>(data.size())) != member.crc) {
        throw VoyemuError("ZIP_DATA", errors.corrupt);
    }

    return data;
}

// Decodes exactly one declared ordinary entry selected by a reader.
std::string unpack_selected_entry(const std::filesystem::path &file, const ZipMember &member,
                                  std::size_t maximum) {
    const auto size = static_cast<std::size_t>(member.bytes);

    if (size > maximum || member.compressed_bytes > maximum + 65536 ||
        (member.method == 0 && member.compressed_bytes != member.bytes)) {
        throw VoyemuError("ZIP_BOUND", "selected entry exceeds bound");
    }

    std::ifstream input(file, std::ios::binary);

    if (!input) {
        throw VoyemuError("ZIP_DATA", "archive unavailable");
    }

    return decode_zip_member(input, member,
                             {"selected entry extent mismatch", "decoding or CRC failure",
                              "ZIP_DATA", "decoding or CRC failure"});
}

ZipData scan(const std::filesystem::path &file, const std::string &pin, const std::string &selected,
             std::size_t maximum, ZipNamePolicy policy = ZipNamePolicy::portable) {
    if (!valid_sha(pin)) {
        throw VoyemuError("ZIP_PIN", "lowercase SHA256 required");
    }

    if (pinned_file_sha256(file) != pin) {
        throw VoyemuError("ZIP_PIN", "archive SHA256 mismatch");
    }

    if (policy == ZipNamePolicy::exact) {
        (void)zip_layout(file, policy);
    }

    // Metadata uses the portable structural index shared with pinned_zip_inventory; only the
    // selected payload needs the vendored in-process decompressor.
    const auto layout = zip_layout(file, policy);

    if (layout.size() > 100000) {
        throw VoyemuError("ZIP_BOUND", "invalid entry count or size");
    }

    ZipData result;
    std::set<std::string> names;
    std::uint64_t total = 0;
    const ZipMember *chosen = nullptr;

    for (const auto &item : layout) {
        safe_name(item.name);

        if (!names.insert(item.name).second) {
            throw VoyemuError("ZIP_DUPLICATE", "duplicate entry");
        }

        const auto kind = item.mode & 0170000;

        if (item.symlink || (kind && kind != (item.directory ? 0040000 : 0100000))) {
            throw VoyemuError("ZIP_TYPE", "only ordinary unencrypted files/directories allowed");
        }

        if (item.bytes > (16ULL << 30) || item.bytes > (32ULL << 30) - total) {
            throw VoyemuError("ZIP_BOUND", "uncompressed extent exceeds limit");
        }

        total += item.bytes;

        if (item.name == selected) {
            chosen = &item;
        }
    }

    if (!chosen) {
        return result;
    }

    if (chosen->directory || chosen->bytes > maximum) {
        throw VoyemuError("ZIP_BOUND", "selected entry exceeds bound");
    }

    result.selected = unpack_selected_entry(file, *chosen, maximum);
    result.found = true;

    return result;
}

template <class Write, class Progress>
void stream_zip_member(std::ifstream &input, const ZipMember &member, Write write,
                       Progress progress) {
    std::array<char, 65536> compressed{}, decoded{};
    std::uint64_t remaining = member.compressed_bytes, produced = 0;
    std::uint32_t crc = MZ_CRC32_INIT;
    auto emit = [&](const char *block, std::size_t count) {
        if (count > member.bytes - produced) {
            throw VoyemuError("ZIP_BOUND", "decoded member grew");
        }

        crc = static_cast<std::uint32_t>(
                mz_crc32(crc, reinterpret_cast<const unsigned char *>(block), count));
        write(block, count, produced);
        produced += count;
    };

    input.clear();
    input.seekg(static_cast<std::streamoff>(member.data_offset));
    auto take = [&] {
        interruption::check();

        if (!remaining) {
            return std::size_t{};
        }

        const auto chunk =
                static_cast<std::size_t>(std::min<std::uint64_t>(remaining, compressed.size()));
        input.read(compressed.data(), static_cast<std::streamsize>(chunk));

        if (static_cast<std::size_t>(input.gcount()) != chunk) {
            throw VoyemuError("ZIP_DATA", "compressed member truncated");
        }

        remaining -= chunk;

        return chunk;
    };

    if (member.method == 0) {
        while (true) {
            progress();
            const auto chunk = take();

            if (!chunk) {
                break;
            }

            emit(compressed.data(), chunk);
        }
    } else {
        mz_stream stream{};

        if (mz_inflateInit2(&stream, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) {
            throw VoyemuError("ZIP_DATA", "decoder initialization failed");
        }

        struct End {
            mz_stream *stream;

            ~End() {
                mz_inflateEnd(stream);
            }
        } end{&stream};

        while (true) {
            progress();

            // With all input consumed the decoder may still hold output, so it runs once more.
            if (!stream.avail_in && remaining) {
                const auto chunk = take();
                stream.next_in = reinterpret_cast<const unsigned char *>(compressed.data());
                stream.avail_in = static_cast<unsigned>(chunk);
            }

            stream.next_out = reinterpret_cast<unsigned char *>(decoded.data());
            stream.avail_out = static_cast<unsigned>(decoded.size());
            const auto before = stream.avail_in;
            const auto status = mz_inflate(&stream, MZ_NO_FLUSH);

            if (status == MZ_BUF_ERROR && !stream.avail_in && !remaining) {
                throw VoyemuError("ZIP_DATA", "compressed member truncated");
            }

            if (status != MZ_OK && status != MZ_STREAM_END) {
                throw VoyemuError("ZIP_DATA", "decoding or CRC failure");
            }

            const auto count = decoded.size() - stream.avail_out;

            if (count) {
                emit(decoded.data(), count);
            }

            if (status == MZ_STREAM_END) {
                if (remaining || stream.avail_in) {
                    throw VoyemuError("ZIP_DATA", "trailing compressed data");
                }

                break;
            }

            if (!count && stream.avail_in == before) {
                throw VoyemuError("ZIP_DATA", "decoding stalled");
            }
        }
    }

    if (produced != member.bytes) {
        throw VoyemuError("ZIP_DATA", "decoded member extent differs");
    }

    if (crc != member.crc) {
        throw VoyemuError("ZIP_DATA", "decoded member CRC differs");
    }
}
} // namespace

std::vector<ZipEntry> pinned_zip_inventory(const std::filesystem::path &file,
                                           const std::string &pin, ZipNamePolicy policy) {
    if (!valid_sha(pin)) {
        throw VoyemuError("ZIP_PIN", "lowercase SHA256 required");
    }

    const auto epoch = cache_file_identity(file);

    if (pinned_file_sha256(file) != pin) {
        throw VoyemuError("ZIP_PIN", "archive SHA256 mismatch");
    }

    const auto layout = zip_layout(file, policy);

    if (layout.size() > 100000) {
        throw VoyemuError("ZIP_BOUND", "entry count exceeds limit");
    }

    std::vector<ZipEntry> entries;
    std::uint64_t total = 0;

    for (const auto &item : layout) {
        safe_name(item.name);
        const auto kind = item.mode & 0170000;

        if (item.symlink || (kind && kind != (item.directory ? 0040000 : 0100000))) {
            throw VoyemuError("ZIP_TYPE", "ordinary entries required");
        }

        if (item.bytes > (16ULL << 30) || item.bytes > (32ULL << 30) - total) {
            throw VoyemuError("ZIP_BOUND", "uncompressed extent exceeds limit");
        }

        total += item.bytes;
        entries.push_back({item.name, item.bytes, item.directory});
    }

    if (cache_file_identity(file) != epoch || pinned_file_sha256(file) != pin) {
        throw VoyemuError("ZIP_CHANGED", "archive changed during inventory");
    }

    return entries;
}

std::string read_pinned_zip_entry(const std::filesystem::path &file, const std::string &pin,
                                  const std::string &name, const std::string &entry_pin,
                                  std::size_t maximum) {
    safe_name(name);

    if (!maximum || maximum > 32 * 1024 * 1024 || !valid_sha(entry_pin)) {
        throw VoyemuError("ZIP_PIN", "bounded entry SHA256 required");
    }

    auto result = scan(file, pin, name, maximum);

    if (!result.found) {
        throw VoyemuError("ZIP_ENTRY", "declared entry missing");
    }

    if (sha256_bytes(result.selected) != entry_pin) {
        throw VoyemuError("ZIP_PIN", "entry SHA256 mismatch");
    }

    return result.selected;
}

std::string read_pinned_zip_entry(const std::filesystem::path &file, const std::string &pin,
                                  const std::string &name, std::size_t maximum,
                                  ZipNamePolicy policy) {
    safe_name(name);

    if (!maximum || maximum > 64 * 1024 * 1024) {
        throw VoyemuError("ZIP_BOUND", "selected entry limit required");
    }

    const auto epoch = cache_file_identity(file);
    auto result = scan(file, pin, name, maximum, policy);

    if (!result.found) {
        throw VoyemuError("ZIP_ENTRY", "declared entry missing");
    }

    if (cache_file_identity(file) != epoch || pinned_file_sha256(file) != pin) {
        throw VoyemuError("ZIP_CHANGED", "archive changed during entry read");
    }

    return result.selected;
}

void extract_pinned_zip_members(const std::filesystem::path &source, const std::string &source_pin,
                                const std::vector<ZipExtraction> &members,
                                const std::filesystem::path &output, std::uint64_t output_bytes,
                                std::uint64_t reserve_bytes, const std::function<bool()> &cancelled,
                                const std::function<void(std::uint64_t)> &advance) {
    if (!valid_sha(source_pin)) {
        throw VoyemuError("ZIP_PIN", "lowercase SHA256 required");
    }

    if (members.empty() || members.size() > 64) {
        throw VoyemuError("ZIP_BOUND", "bounded member list required");
    }

    if (!output.is_absolute() || std::filesystem::exists(output) ||
        std::filesystem::is_symlink(output)) {
        throw VoyemuError("ZIP_OUTPUT", "fresh absolute destination required");
    }

    if (!output_bytes || output_bytes > (32ULL << 30)) {
        throw VoyemuError("ZIP_BOUND", "output extent required");
    }

    std::set<std::string> requested;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;

    for (const auto &item : members) {
        safe_name(item.name);

        if (!requested.insert(item.name).second || !item.bytes || item.offset > output_bytes ||
            item.bytes > output_bytes - item.offset) {
            throw VoyemuError("ZIP_BOUND", "requested member extent required");
        }

        extents.emplace_back(item.offset, item.offset + item.bytes);
    }

    std::sort(extents.begin(), extents.end());

    for (std::size_t index = 1; index < extents.size(); ++index) {
        if (extents[index].first < extents[index - 1].second) {
            throw VoyemuError("ZIP_BOUND", "overlapping requested members");
        }
    }

    const auto epoch = cache_file_identity(source);

    if (pinned_file_sha256(source) != source_pin) {
        throw VoyemuError("ZIP_PIN", "archive SHA256 mismatch");
    }

    const auto parent = output.parent_path();

    require_disk_space("ZIP_SPACE", parent, output_bytes, reserve_bytes);

    const auto layout = zip_layout(source);
    std::map<std::string, const ZipMember *> index;

    for (const auto &item : layout) {
        index.emplace(item.name, &item);
    }

    {
        std::ofstream extent(output, std::ios::binary | std::ios::trunc);

        if (!extent) {
            throw VoyemuError("ZIP_OUTPUT", "output create failed");
        }

        extent.seekp(static_cast<std::streamoff>(output_bytes - 1));
        extent.put('\0');
        extent.close();

        if (!extent) {
            throw VoyemuError("ZIP_OUTPUT", "output extent failed");
        }
    }
    std::ifstream input(source, std::ios::binary);
    std::fstream result(output, std::ios::binary | std::ios::in | std::ios::out);

    if (!input || !result) {
        throw VoyemuError("ZIP_OUTPUT", "output open failed");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    auto progress = [&] {
        if (cancelled && cancelled()) {
            throw VoyemuError("ZIP_CANCEL", "extraction cancelled");
        }

        if (std::chrono::steady_clock::now() >= deadline) {
            throw VoyemuError("ZIP_BOUND", "extraction deadline reached");
        }
    };

    for (const auto &item : members) {
        progress();
        const auto found = index.find(item.name);

        if (found == index.end()) {
            throw VoyemuError("ZIP_ENTRY", "declared member missing");
        }

        const auto &member = *found->second;

        if (member.directory || member.symlink || member.bytes != item.bytes ||
            (member.method != 0 && member.method != 8) ||
            (member.method == 0 && member.compressed_bytes != member.bytes)) {
            throw VoyemuError("ZIP_TYPE", "declared member type/extent differs");
        }

        stream_zip_member(
                input, member,
                [&](const char *block, std::size_t count, std::uint64_t offset) {
                    if (!std::all_of(block, block + count, [](char byte) {
                            return byte == 0;
                        })) {
                        result.seekp(static_cast<std::streamoff>(item.offset + offset));
                        result.write(block, static_cast<std::streamsize>(count));

                        if (!result) {
                            throw VoyemuError("ZIP_OUTPUT", "member write failed");
                        }
                    }

                    if (std::filesystem::space(parent).available < reserve_bytes) {
                        require_disk_space("ZIP_SPACE", parent, 0, reserve_bytes);
                    }

                    if (advance) {
                        advance(count);
                    }
                },
                progress);
    }

    result.close();

    if (!result) {
        throw VoyemuError("ZIP_OUTPUT", "output close failed");
    }

    if (cache_file_identity(source) != epoch || pinned_file_sha256(source) != source_pin) {
        throw VoyemuError("ZIP_CHANGED", "archive changed during extraction");
    }
}

std::map<std::string, std::string> pinned_zip_link_targets(const std::filesystem::path &source,
                                                           const std::string &source_pin) {
    if (!valid_sha(source_pin)) {
        throw VoyemuError("ZIP_PIN", "lowercase SHA256 required");
    }

    const auto epoch = cache_file_identity(source);

    if (pinned_file_sha256(source) != source_pin) {
        throw VoyemuError("ZIP_PIN", "archive SHA256 mismatch");
    }

    std::ifstream input(source, std::ios::binary);

    if (!input) {
        throw VoyemuError("ZIP_DATA", "archive open failed");
    }

    std::map<std::string, std::string> targets;

    for (const auto &member : zip_layout(source, ZipNamePolicy::exact)) {
        if (!member.symlink) {
            continue;
        }

        if (member.bytes > 4096 || (member.method != 0 && member.method != 8) ||
            (member.method == 0 && member.compressed_bytes != member.bytes)) {
            throw VoyemuError("ZIP_TYPE", "bounded link target required");
        }

        std::string target;
        stream_zip_member(
                input, member,
                [&](const char *block, std::size_t count, std::uint64_t) {
                    target.append(block, count);
                },
                [] {});
        targets.emplace(member.name, std::move(target));
    }

    if (cache_file_identity(source) != epoch || pinned_file_sha256(source) != source_pin) {
        throw VoyemuError("ZIP_CHANGED", "archive changed while reading links");
    }

    return targets;
}

void extract_pinned_zip_tree(const std::filesystem::path &source, const std::string &source_pin,
                             const std::vector<ZipTreeEntry> &members, std::uint64_t reserve_bytes,
                             ZipNamePolicy policy) {
    if (!valid_sha(source_pin) || members.size() > 100000) {
        throw VoyemuError("ZIP_PIN", "bounded member list and lowercase SHA256 required");
    }

    const auto epoch = cache_file_identity(source);

    if (pinned_file_sha256(source) != source_pin) {
        throw VoyemuError("ZIP_PIN", "archive SHA256 mismatch");
    }

    const auto layout = zip_layout(source, policy);
    std::map<std::string, const ZipMember *> index;

    for (const auto &item : layout) {
        index.emplace(item.name, &item);
    }

    std::set<std::string> names;
    std::set<std::filesystem::path> paths;
    std::uint64_t total = 0;

    for (const auto &item : members) {
        safe_name(item.name);

        if (item.bytes > (32ULL << 30) - total) {
            throw VoyemuError("ZIP_BOUND", "tree extent exceeds limit");
        }

        total += item.bytes;

        if (!names.insert(item.name).second || !item.output.is_absolute() ||
            !paths.insert(item.output).second || std::filesystem::exists(item.output) ||
            std::filesystem::is_symlink(item.output)) {
            throw VoyemuError("ZIP_OUTPUT", "unique fresh absolute output required");
        }

        require_disk_space("ZIP_SPACE", item.output.parent_path(), total, reserve_bytes);
    }

    std::ifstream input(source, std::ios::binary);

    if (!input) {
        throw VoyemuError("ZIP_DATA", "archive open failed");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(10);
    auto progress = [&] {
        if (std::chrono::steady_clock::now() >= deadline) {
            throw VoyemuError("ZIP_BOUND", "extraction deadline reached");
        }
    };

    for (const auto &item : members) {
        // On a case-insensitive volume a case alias names a file written before it.
        if (std::filesystem::exists(item.output) || std::filesystem::is_symlink(item.output)) {
            throw VoyemuError("ZIP_OUTPUT", "unique fresh absolute output required");
        }

        std::ofstream result(item.output, std::ios::binary | std::ios::trunc);

        if (!result) {
            throw VoyemuError("ZIP_OUTPUT", "file create failed");
        }

        if (item.bytes) {
            result.seekp(static_cast<std::streamoff>(item.bytes - 1));
            result.put('\0');
        }

        progress();
        const auto found = index.find(item.name);

        if (found == index.end()) {
            throw VoyemuError("ZIP_ENTRY", "declared member missing");
        }

        const auto &member = *found->second;

        if (member.directory || member.symlink || member.bytes != item.bytes ||
            (member.method != 0 && member.method != 8) ||
            (member.method == 0 && member.compressed_bytes != member.bytes)) {
            throw VoyemuError("ZIP_TYPE", "declared member type/extent differs");
        }

        stream_zip_member(
                input, member,
                [&](const char *block, std::size_t count, std::uint64_t offset) {
                    if (!std::all_of(block, block + count, [](char byte) {
                            return byte == 0;
                        })) {
                        result.seekp(static_cast<std::streamoff>(offset));
                        result.write(block, static_cast<std::streamsize>(count));

                        if (!result) {
                            throw VoyemuError("ZIP_OUTPUT", "member write failed");
                        }
                    }

                    if (std::filesystem::space(item.output.parent_path()).available <
                        reserve_bytes) {
                        require_disk_space("ZIP_SPACE", item.output.parent_path(), 0,
                                           reserve_bytes);
                    }
                },
                progress);
        result.close();

        if (!result) {
            throw VoyemuError("ZIP_OUTPUT", "file close failed");
        }
    }

    if (cache_file_identity(source) != epoch || pinned_file_sha256(source) != source_pin) {
        throw VoyemuError("ZIP_CHANGED", "archive changed during extraction");
    }
}

std::string rewrite_pinned_zip(const std::filesystem::path &source, const std::string &source_pin,
                               const std::vector<ZipReplacement> &replacements,
                               const std::vector<std::string> &removals,
                               const std::vector<ZipAddition> &additions,
                               const std::filesystem::path &destination,
                               std::uint64_t reserve_bytes) {
    if (!destination.is_absolute() || destination.filename().empty() ||
        destination.filename() == "." || destination.filename() == ".." ||
        std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        throw VoyemuError("ZIP_OUTPUT", "new absolute destination required");
    }

    const auto inventory = pinned_zip_inventory(source, source_pin);
    std::map<std::string, ZipEntry> original;

    for (const auto &entry : inventory) {
        original.emplace(entry.name, entry);
    }

    std::set<std::string> requested, removed;
    std::map<std::string, const ZipReplacement *> changed;

    for (const auto &item : replacements) {
        safe_name(item.name);

        if (!requested.insert(item.name).second || !original.contains(item.name) ||
            original.at(item.name).directory || item.bytes.size() > 32 * 1024 * 1024 ||
            !valid_sha(item.sha256) || sha256_bytes(item.bytes) != item.sha256) {
            throw VoyemuError("ZIP_PATCH", "invalid replacement");
        }

        read_pinned_zip_entry(source, source_pin, item.name, item.original_sha256);
        changed[item.name] = &item;
    }

    for (const auto &name : removals) {
        safe_name(name);

        if (!requested.insert(name).second || !original.contains(name)) {
            throw VoyemuError("ZIP_PATCH", "missing or conflicting removal");
        }

        removed.insert(name);
    }

    for (const auto &item : additions) {
        safe_name(item.name);

        if (!requested.insert(item.name).second || original.contains(item.name) ||
            item.name.ends_with('/') || item.bytes.size() > 32 * 1024 * 1024 ||
            !valid_sha(item.sha256) || sha256_bytes(item.bytes) != item.sha256) {
            throw VoyemuError("ZIP_PATCH", "invalid addition");
        }
    }

    if (inventory.size() - removed.size() + additions.size() > 65535) {
        throw VoyemuError("ZIP_BOUND", "APK candidate requires classic ZIP entry count");
    }

    std::uint64_t estimate = 0;

    for (const auto &entry : inventory) {
        if (!removed.contains(entry.name)) {
            estimate += changed.contains(entry.name) ? changed.at(entry.name)->bytes.size()
                                                     : entry.bytes;
        }
    }

    for (const auto &item : additions) {
        estimate += item.bytes.size();
    }

    if (estimate > (2ULL << 30)) {
        throw VoyemuError("ZIP_BOUND", "APK candidate exceeds 2 GiB payload");
    }

    const auto peak = estimate + (inventory.size() + additions.size()) * 4096ULL + 1024 * 1024;
    auto existing = destination.parent_path();

    while (!std::filesystem::exists(existing) && existing != existing.parent_path()) {
        existing = existing.parent_path();
    }

    require_disk_space("ZIP_SPACE", existing, peak, reserve_bytes);

    // Entry payloads stream through the vendored miniz decoder/encoder while the container is
    // emitted directly, so no host archiver or ZIP64 records are involved.
    ensure_private_directory(destination.parent_path());
    const auto pending = destination.parent_path() /
                         ("." + destination.filename().string() + ".pending-" + random_token());
    struct Cleanup {
        std::filesystem::path pending;
        bool owned = false;

        ~Cleanup() {
            if (owned) {
                std::error_code error;
                std::filesystem::remove(pending, error);
            }
        }
    } cleanup;
    cleanup.pending = pending;
    os::ExclusiveFile output(pending);

    if (!output.open()) {
        throw VoyemuError("ZIP_OUTPUT", "exclusive staging file failed");
    }

    cleanup.owned = true;
    std::uint64_t position = 0;
    auto emit = [&](const void *bytes, std::size_t count) {
        if (!output.write(bytes, count)) {
            throw VoyemuError("ZIP_WRITE", "entry write failed");
        }
    };
    auto little = [](std::uint64_t value, unsigned width) {
        std::string out(width, '\0');

        for (unsigned index = 0; index < width; ++index) {
            out[index] = static_cast<char>(value >> (8 * index));
        }

        return out;
    };
    auto crc_of = [](const std::string &bytes) {
        return static_cast<std::uint32_t>(
                mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char *>(bytes.data()),
                         bytes.size()));
    };
    auto stored_policy = [](const std::string &name) {
        return name == "resources.arsc" || name.ends_with(".dex") ||
               (name.starts_with("lib/") && name.ends_with(".so"));
    };
    struct Central {
        std::string name;
        std::uint32_t crc;
        std::uint64_t compressed, bytes, offset;
        unsigned method, mode;
        bool directory;
    };
    std::vector<Central> records;
    // Emits one entry: local header, payload and (for files) a data descriptor; no seek needed.
    auto entry = [&](const std::string &name, unsigned mode, bool directory,
                     const std::string &payload, std::uint32_t crc) {
        const unsigned method = directory ? 0 : (stored_policy(name) ? 0 : 8);
        const std::uint16_t flags = static_cast<std::uint16_t>(directory ? 0x800 : 0x808);
        const auto start = position;
        const auto header = little(0x04034b50, 4) + little(20, 2) + little(flags, 2) +
                            little(method, 2) + little(0, 2) + little(0, 2) + little(0, 4) +
                            little(0, 4) + little(0, 4) + little(name.size(), 2) + little(0, 2) +
                            name;
        emit(header.data(), header.size());
        position += header.size();
        std::uint64_t compressed = 0;

        if (!directory) {
            if (method == 0) {
                emit(payload.data(), payload.size());
                position += payload.size();
                compressed = payload.size();
            } else {
                mz_stream stream{};
                stream.next_in = reinterpret_cast<const unsigned char *>(payload.data());
                stream.avail_in = static_cast<unsigned>(payload.size());

                if (mz_deflateInit2(&stream, MZ_DEFAULT_LEVEL, MZ_DEFLATED, -MZ_DEFAULT_WINDOW_BITS,
                                    9, MZ_DEFAULT_STRATEGY) != MZ_OK) {
                    throw VoyemuError("ZIP_WRITE", "entry header failed");
                }

                unsigned char buffer[16384];
                int status = MZ_OK;

                do {
                    stream.next_out = buffer;
                    stream.avail_out = sizeof buffer;
                    status = mz_deflate(&stream, MZ_FINISH);

                    if (status != MZ_OK && status != MZ_STREAM_END) {
                        mz_deflateEnd(&stream);
                        throw VoyemuError("ZIP_WRITE", "entry write failed");
                    }

                    const auto produced = sizeof buffer - stream.avail_out;
                    emit(buffer, produced);
                    position += produced;
                } while (status != MZ_STREAM_END);

                compressed = stream.total_out;
                mz_deflateEnd(&stream);
            }

            const auto descriptor = little(0x08074b50, 4) + little(crc, 4) + little(compressed, 4) +
                                    little(payload.size(), 4);
            emit(descriptor.data(), descriptor.size());
            position += descriptor.size();
        } else {
            compressed = 0;
        }

        records.push_back({name, crc, compressed, payload.size(), start, method, mode, directory});
    };
    const auto layout = zip_layout(source);

    if (layout.size() != inventory.size()) {
        throw VoyemuError("ZIP_WRITE", "source header changed");
    }

    for (std::size_t index = 0; index < layout.size(); ++index) {
        if (layout[index].name != inventory[index].name ||
            layout[index].bytes != inventory[index].bytes) {
            throw VoyemuError("ZIP_WRITE", "source entry changed");
        }
    }

    std::ifstream input(source, std::ios::binary);

    if (!input) {
        throw VoyemuError("ZIP_DATA", "archive unavailable");
    }

    auto decode = [&](const ZipMember &member) {
        return decode_zip_member(input, member,
                                 {"source extent mismatch", "source decoding/CRC failure",
                                  "ZIP_BOUND", "source entry grew"});
    };

    for (const auto &member : layout) {
        interruption::check();

        if (removed.contains(member.name)) {
            continue;
        }

        if (member.directory) {
            entry(member.name, member.mode, true, std::string(), 0);
        } else if (changed.contains(member.name)) {
            const auto &bytes = changed.at(member.name)->bytes;
            entry(member.name, member.mode, false, bytes, crc_of(bytes));
        } else {
            const auto payload = decode(member);
            entry(member.name, member.mode, false, payload, member.crc);
        }
    }

    for (const auto &item : additions) {
        entry(item.name, 0644, false, item.bytes, crc_of(item.bytes));
    }

    const auto central_offset = position;

    for (const auto &item : records) {
        const auto external = static_cast<std::uint32_t>(item.mode) << 16;
        const auto header = little(0x02014b50, 4) + little(20, 2) + little(20, 2) +
                            little(item.directory ? 0x800 : 0x808, 2) + little(item.method, 2) +
                            little(0, 2) + little(0, 2) + little(item.crc, 4) +
                            little(item.compressed, 4) + little(item.bytes, 4) +
                            little(item.name.size(), 2) + little(0, 2) + little(0, 2) +
                            little(0, 2) + little(0, 2) + little(external, 4) +
                            little(item.offset, 4) + item.name;
        emit(header.data(), header.size());
        position += header.size();
    }

    const auto central_size = position - central_offset;
    const auto count = static_cast<std::uint16_t>(records.size());
    const auto end = little(0x06054b50, 4) + little(0, 2) + little(0, 2) + little(count, 2) +
                     little(count, 2) + little(central_size, 4) + little(central_offset, 4) +
                     little(0, 2);
    emit(end.data(), end.size());

    if (!output.sync()) {
        throw VoyemuError("ZIP_OUTPUT", "sync failed");
    }

    if (!output.close()) {
        throw VoyemuError("ZIP_OUTPUT", "close failed");
    }

    if (pinned_file_sha256(source) != source_pin) {
        throw VoyemuError("ZIP_PIN", "source changed during rewrite");
    }

    const auto digest = sha256_file(pending);

    if (!os::link_exclusive(pending, destination)) {
        throw VoyemuError("ZIP_OUTPUT", "exclusive publication failed");
    }

    std::error_code cleanup_error;

    if (std::filesystem::remove(pending, cleanup_error)) {
        cleanup.owned = false;
    }

    return digest;
}

namespace {
// The digests that this process computed, by path, with the file identity they belong to.
std::mutex verified_mutex;
std::map<std::string, std::pair<Json, std::string>> verified_files;
} // namespace

std::string pinned_file_sha256(const std::filesystem::path &file,
                               const std::function<void(std::uint64_t)> &advance) {
    Json identity;

    try {
        identity = cache_file_identity(file);
    } catch (const std::exception &) {
        // A file without a stable identity, such as a linked one, is hashed on each use.
        return sha256_file(file, advance);
    }

    const auto key = file.lexically_normal().string();

    {
        std::lock_guard lock(verified_mutex);
        const auto found = verified_files.find(key);

        if (found != verified_files.end() && found->second.first == identity) {
            return found->second.second;
        }
    }

    std::string digest;
    const auto bytes = std::filesystem::file_size(file);

    if (!advance && bytes >= (256ULL << 20)) {
        progress::block();
        progress::Meter meter("verify", {{"name", file.filename().string()}});
        meter.length(bytes);
        digest = sha256_file(file, [&meter](std::uint64_t block) {
            meter.advance(block);
        });
    } else {
        digest = sha256_file(file, advance);
    }

    try {
        if (cache_file_identity(file) == identity) {
            std::lock_guard lock(verified_mutex);
            verified_files[key] = {identity, digest};
        }
    } catch (const std::exception &) { /* Ignored: a file that changed is not remembered. */
    }

    return digest;
}
} // namespace voyemu
