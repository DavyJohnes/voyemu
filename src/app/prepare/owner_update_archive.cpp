#include "owner_update_archive.hpp"
#include "check.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include "zip_archive.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <miniz.h>
#include <set>
#include <thread>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef __APPLE__
#include <archive.h>
#include <archive_entry.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace voyemu {
namespace {
constexpr std::uint64_t bound = 16ULL << 30;

constexpr Check need{"OWNER_ARCHIVE"};

void name(const std::string &value) {
    need(!value.empty() && value.size() <= 512 && value.front() != '/' && value.back() != '/',
         "selected relative path required");

    for (unsigned char c : value) {
        need(c > 32 && c < 127 && c != '\\' && c != ':' && c != '*' && c != '?' && c != '[' &&
                     c != ']',
             "unambiguous ASCII path required");
    }

    const std::filesystem::path path(value);
    need(!path.is_absolute() && path.lexically_normal() == path, "normalized path required");

    for (const auto &part : path) {
        need(part != ".." && part != ".", "traversal rejected");
    }
}

void ordinary(const std::filesystem::path &path) {
    ordinary_path(path, need, PathForm::CanonicalAncestors, "absolute normalized path required",
                  "aliased path rejected");
}

void space(const std::filesystem::path &path, std::uint64_t reserve) {
    need(std::filesystem::space(path).available >= reserve, "disk reserve reached");
}

Json directory_identity(const std::filesystem::path &path) {
    ordinary(path);
#ifdef _WIN32
    const auto handle = CreateFileW(
            path.c_str(), FILE_READ_ATTRIBUTES,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    need(handle != INVALID_HANDLE_VALUE, "directory identity open failed");
    BY_HANDLE_FILE_INFORMATION info{};
    const bool valid = GetFileInformationByHandle(handle, &info) &&
                       (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                       !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT);
    CloseHandle(handle);
    need(valid, "ordinary directory required");

    return {{"volume", info.dwVolumeSerialNumber},
            {"high", info.nFileIndexHigh},
            {"low", info.nFileIndexLow}};
#else
    struct stat info{};
    need(!::lstat(path.c_str(), &info) && S_ISDIR(info.st_mode) && info.st_uid == ::geteuid(),
         "owned directory required");

    return {{"device", info.st_dev}, {"inode", info.st_ino}};
#endif
}

struct Member {
    std::string name, sha;
    std::uint64_t bytes;
    std::filesystem::path file;
};

void exact(const Member &member) {
    ordinary(member.file);
    need(std::filesystem::is_regular_file(member.file) &&
                 std::filesystem::hard_link_count(member.file) == 1 &&
                 std::filesystem::file_size(member.file) == member.bytes,
         "member extent/type differs");
    const auto identity = cache_file_identity(member.file);
    need(sha256_file(member.file) == member.sha && cache_file_identity(member.file) == identity,
         "member SHA/identity differs");
}

void copy(std::istream &input, std::ostream &output, std::uint64_t bytes,
          const std::filesystem::path &stage, std::uint64_t reserve) {
    std::array<char, 65536> buffer{};

    while (bytes) {
        interruption::check();
        const auto count =
                static_cast<std::streamsize>(std::min<std::uint64_t>(bytes, buffer.size()));
        input.read(buffer.data(), count);
        need(input.gcount() == count, "short selected payload");
        output.write(buffer.data(), count);
        need(bool(output), "output write failed");
        bytes -= count;
        space(stage, reserve);
    }
}

#ifdef __APPLE__
// Selected members of one pinned gzip TAR layer; ZIP layers use the portable pinned reader.
void extract(const std::filesystem::path &source, const std::vector<Member> &members,
             const std::filesystem::path &stage, std::uint64_t reserve) {
    auto *reader = archive_read_new();

    need(reader, "archive allocation failed");
    struct Cleanup {
        archive *value;

        ~Cleanup() {
            archive_read_free(value);
        }
    } cleanup{reader};
    need(archive_read_support_format_tar(reader) == ARCHIVE_OK, "archive format unavailable");
    need(archive_read_support_filter_gzip(reader) == ARCHIVE_OK, "gzip filter unavailable");
    need(archive_read_open_filename(reader, source.c_str(), 65536) == ARCHIVE_OK,
         "archive open failed");
    std::map<std::string, const Member *> selected;

    for (const auto &item : members) {
        selected.emplace(item.name, &item);
    }

    std::set<std::string> seen;
    archive_entry *header = nullptr;
    unsigned count = 0;
    std::uint64_t expanded = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);

    for (;;) {
        need(std::chrono::steady_clock::now() < deadline, "archive deadline reached");
        const auto status = archive_read_next_header(reader, &header);

        if (status == ARCHIVE_EOF) {
            break;
        }

        need(status == ARCHIVE_OK && ++count <= 100000, "archive header/count failed");
        const auto *raw = archive_entry_pathname_utf8(header);
        need(raw, "UTF8 archive name required");
        need(archive_entry_size_is_set(header) && archive_entry_size(header) >= 0 &&
                     static_cast<std::uint64_t>(archive_entry_size(header)) <=
                             (32ULL << 30) - expanded,
             "expanded archive bound exceeded");
        expanded += static_cast<std::uint64_t>(archive_entry_size(header));
        const auto found = selected.find(raw);

        if (found == selected.end()) {
            std::array<char, 65536> ignored{};
            std::uint64_t skipped = 0;

            for (;;) {
                const auto got = archive_read_data(reader, ignored.data(), ignored.size());
                need(got >= 0 && std::chrono::steady_clock::now() < deadline,
                     "archive skipped payload/deadline failed");

                if (!got) {
                    break;
                }

                need(static_cast<std::uint64_t>(got) <=
                             static_cast<std::uint64_t>(archive_entry_size(header)) - skipped,
                     "skipped payload extent differs");
                skipped += got;
            }

            continue;
        }

        const auto &item = *found->second;
        need(seen.insert(item.name).second && archive_entry_filetype(header) == AE_IFREG &&
                     !archive_entry_hardlink(header) && !archive_entry_symlink(header) &&
                     !archive_entry_is_encrypted(header) && archive_entry_size_is_set(header) &&
                     archive_entry_size(header) >= 0 &&
                     static_cast<std::uint64_t>(archive_entry_size(header)) == item.bytes,
             "selected duplicate/type/extent rejected");
        std::ofstream output(item.file, std::ios::binary);
        need(bool(output), "selected output open failed");
        std::array<char, 65536> buffer{};
        std::uint64_t written = 0;

        for (;;) {
            interruption::check();
            const auto got = archive_read_data(reader, buffer.data(), buffer.size());
            need(got >= 0 && std::chrono::steady_clock::now() < deadline,
                 "archive payload/deadline failed");

            if (!got) {
                break;
            }

            need(static_cast<std::uint64_t>(got) <= item.bytes - written, "selected payload grew");
            output.write(buffer.data(), got);
            need(bool(output), "selected output failed");
            written += got;
            space(stage, reserve);
        }

        output.close();
        need(bool(output) && written == item.bytes, "selected payload truncated");
        exact(item);
    }

    need(seen.size() == members.size(), "selected member missing");
}
#else
class GzipReader {
    std::ifstream input_;
    mz_stream stream_{};
    std::array<unsigned char, 65536> input_buffer_{};
    std::uint64_t compressed_left_{}, expanded_{};
    std::uint32_t crc_ = MZ_CRC32_INIT, expected_crc_{}, expected_size_{};
    bool ended_ = false;

    unsigned byte() {
        char c;

        input_.get(c);
        need(bool(input_), "gzip header truncated");

        return static_cast<unsigned char>(c);
    }

    static std::uint32_t little(const unsigned char *p) {
        return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) |
               (std::uint32_t(p[3]) << 24);
    }

public:
    explicit GzipReader(const std::filesystem::path &path) : input_(path, std::ios::binary) {
        need(bool(input_), "gzip open failed");
        const auto size = std::filesystem::file_size(path);
        need(size >= 18, "gzip extent too small");
        std::uint32_t header_crc = MZ_CRC32_INIT;
        std::uint64_t header_size = 0;
        auto header = [&]() {
            need(++header_size <= 65536, "gzip header bound exceeded");
            const auto c = byte();
            const unsigned char b = c;
            header_crc = static_cast<std::uint32_t>(mz_crc32(header_crc, &b, 1));

            return c;
        };
        need(header() == 0x1f && header() == 0x8b && header() == 8,
             "gzip signature/method rejected");
        const auto flags = header();
        need(!(flags & 0xe0), "gzip reserved flags rejected");

        for (unsigned i = 0; i < 6; ++i) {
            header();
        }

        if (flags & 4) {
            const auto lo = header(), hi = header();
            const auto n = lo | (hi << 8);

            for (unsigned i = 0; i < n; ++i) {
                header();
            }
        }

        for (const unsigned flag : {8u, 16u}) {
            if (flags & flag) {
                while (header() != 0) {}
            }
        }

        if (flags & 2) {
            const auto lo = byte(), hi = byte();
            header_size += 2;
            need((lo | (hi << 8)) == (header_crc & 0xffff), "gzip header CRC differs");
        }

        need(header_size <= size - 8, "gzip header exceeds extent");
        compressed_left_ = size - header_size - 8;
        input_.seekg(static_cast<std::streamoff>(size - 8));
        std::array<unsigned char, 8> trailer{};
        input_.read(reinterpret_cast<char *>(trailer.data()), 8);
        need(input_.gcount() == 8, "gzip trailer missing");
        expected_crc_ = little(trailer.data());
        expected_size_ = little(trailer.data() + 4);
        input_.clear();
        input_.seekg(static_cast<std::streamoff>(header_size));
        need(mz_inflateInit2(&stream_, -MZ_DEFAULT_WINDOW_BITS) == MZ_OK,
             "gzip inflate init failed");
    }

    ~GzipReader() {
        mz_inflateEnd(&stream_);
    }

    std::size_t read(char *out, std::size_t maximum) {
        if (ended_ || !maximum) {
            return 0;
        }

        stream_.next_out = reinterpret_cast<unsigned char *>(out);
        stream_.avail_out = static_cast<unsigned>(maximum);

        while (stream_.avail_out && !ended_) {
            if (!stream_.avail_in && compressed_left_) {
                const auto n = static_cast<std::size_t>(
                        std::min<std::uint64_t>(compressed_left_, input_buffer_.size()));
                input_.read(reinterpret_cast<char *>(input_buffer_.data()),
                            static_cast<std::streamsize>(n));
                need(static_cast<std::size_t>(input_.gcount()) == n,
                     "gzip compressed data truncated");
                compressed_left_ -= n;
                stream_.next_in = input_buffer_.data();
                stream_.avail_in = static_cast<unsigned>(n);
            }

            const auto before_in = stream_.avail_in, before_out = stream_.avail_out;
            const auto status = mz_inflate(&stream_, MZ_NO_FLUSH);
            need(status == MZ_OK || status == MZ_STREAM_END, "gzip deflate rejected");

            if (status == MZ_STREAM_END) {
                need(!compressed_left_ && !stream_.avail_in,
                     "gzip trailing or concatenated data rejected");
                ended_ = true;
            } else {
                need(before_in != stream_.avail_in || before_out != stream_.avail_out,
                     "gzip deflate stalled");
            }
        }

        const auto count = maximum - stream_.avail_out;
        need(count <= (32ULL << 30) - expanded_, "gzip expanded bound exceeded");
        expanded_ += count;
        crc_ = static_cast<std::uint32_t>(
                mz_crc32(crc_, reinterpret_cast<const unsigned char *>(out), count));

        if (ended_) {
            need(crc_ == expected_crc_ && static_cast<std::uint32_t>(expanded_) == expected_size_,
                 "gzip CRC/ISIZE differs");
        }

        return count;
    }

    void exact(char *out, std::size_t size) {
        while (size) {
            const auto count = read(out, size);
            need(count, "gzip output truncated");
            out += count;
            size -= count;
        }
    }
};

std::string tar_text(const char *p, std::size_t n) {
    const auto end = std::find(p, p + n, '\0');

    return std::string(p, end);
}

std::uint64_t tar_number(const char *p, std::size_t n) {
    std::uint64_t value = 0;
    std::size_t i = 0;

    while (i < n && (p[i] == ' ' || p[i] == '\0')) {
        ++i;
    }

    for (; i < n && p[i] && p[i] != ' '; ++i) {
        need(p[i] >= '0' && p[i] <= '7' && value <= (UINT64_MAX - 7) / 8,
             "tar octal field rejected");
        value = value * 8 + (p[i] - '0');
    }

    for (; i < n; ++i) {
        need(p[i] == ' ' || p[i] == '\0', "tar numeric suffix rejected");
    }

    return value;
}

void extract(const std::filesystem::path &source, const std::vector<Member> &members,
             const std::filesystem::path &stage, std::uint64_t reserve) {
    GzipReader input(source);
    std::map<std::string, const Member *> selected;

    for (const auto &item : members) {
        selected.emplace(item.name, &item);
    }

    std::set<std::string> seen;
    std::map<std::string, std::string> global, local;
    std::uint64_t expanded = 0;
    unsigned count = 0, zeros = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    std::array<char, 512> header{};
    std::array<char, 65536> buffer{};
    auto consume = [&](std::uint64_t bytes, std::ostream *output, std::string *text) {
        while (bytes) {
            interruption::check();
            need(std::chrono::steady_clock::now() < deadline, "archive deadline reached");
            const auto n = static_cast<std::size_t>(std::min<std::uint64_t>(bytes, buffer.size()));
            input.exact(buffer.data(), n);

            if (output) {
                output->write(buffer.data(), static_cast<std::streamsize>(n));
                need(bool(*output), "selected output failed");
                space(stage, reserve);
            }

            if (text) {
                text->append(buffer.data(), n);
            }

            bytes -= n;
        }
    };

    while (zeros < 2) {
        input.exact(header.data(), header.size());

        if (std::all_of(header.begin(), header.end(), [](char c) {
                return c == 0;
            })) {
            ++zeros;
            continue;
        }

        need(!zeros && ++count <= 100000, "tar end marker or count invalid");
        std::uint64_t checksum = 0;

        for (std::size_t i = 0; i < header.size(); ++i) {
            checksum += (i >= 148 && i < 156) ? 32 : static_cast<unsigned char>(header[i]);
        }

        need(tar_number(header.data() + 148, 8) == checksum, "tar header checksum differs");
        need(std::string_view(header.data() + 257, 5) == "ustar", "ustar header required");
        const auto type = header[156];
        auto path = tar_text(header.data(), 100);
        const auto prefix = tar_text(header.data() + 345, 155);

        if (!prefix.empty()) {
            path = prefix + "/" + path;
        }

        auto size = tar_number(header.data() + 124, 12);

        if (type == 'x' || type == 'g') {
            need(size <= 1024 * 1024, "pax metadata bound exceeded");
            std::string metadata;
            consume(size, nullptr, &metadata);
            const auto padding = (512 - size % 512) % 512;
            consume(padding, nullptr, nullptr);
            auto &attributes = type == 'g' ? global : local;
            need(type == 'g' || local.empty(), "consecutive pax headers rejected");
            std::size_t at = 0;

            while (at < metadata.size()) {
                const auto space_at = metadata.find(' ', at);
                need(space_at != std::string::npos && space_at > at, "pax record length missing");
                std::uint64_t length = 0;

                for (auto i = at; i < space_at; ++i) {
                    need(metadata[i] >= '0' && metadata[i] <= '9' &&
                                 length <= (UINT64_MAX - 9) / 10,
                         "pax length invalid");
                    length = length * 10 + (metadata[i] - '0');
                }

                need(length > space_at - at + 2 && length <= metadata.size() - at &&
                             metadata[at + length - 1] == '\n',
                     "pax record extent invalid");
                const auto equal = metadata.find('=', space_at + 1);
                need(equal != std::string::npos && equal < at + length - 1,
                     "pax record key invalid");
                const auto key = metadata.substr(space_at + 1, equal - space_at - 1),
                           value = metadata.substr(equal + 1, at + length - equal - 2);
                need(!key.starts_with("GNU.sparse") && key != "SCHILY.realsize" &&
                             key != "linkpath",
                     "unsupported pax payload semantics");

                if (key == "path" || key == "size") {
                    attributes[key] = value;
                }

                at += static_cast<std::size_t>(length);
            }

            continue;
        }

        auto attr = [&](const std::string &key) -> std::string {
            if (local.contains(key)) {
                return local.at(key);
            }

            if (global.contains(key)) {
                return global.at(key);
            }

            return {};
        };
        const auto pax_path = attr("path"), pax_size = attr("size");

        if (!pax_path.empty()) {
            path = pax_path;
        }

        if (!pax_size.empty()) {
            size = 0;

            for (char c : pax_size) {
                need(c >= '0' && c <= '9' && size <= (UINT64_MAX - 9) / 10, "pax size invalid");
                size = size * 10 + (c - '0');
            }
        }

        local.clear();
        need(size <= (32ULL << 30) - expanded, "tar expanded bound exceeded");
        expanded += size;
        const auto found = selected.find(path);
        std::ofstream output;

        if (found != selected.end()) {
            const auto &item = *found->second;
            need(seen.insert(path).second && (type == '0' || type == '\0') && size == item.bytes,
                 "selected duplicate/type/extent rejected");
            output.open(item.file, std::ios::binary | std::ios::trunc);
            need(bool(output), "selected output open failed");
        }

        consume(size, found == selected.end() ? nullptr : &output, nullptr);

        if (found != selected.end()) {
            output.close();
            need(bool(output), "selected output close failed");
            exact(*found->second);
        }

        consume((512 - size % 512) % 512, nullptr, nullptr);
    }

    need(local.empty() && seen.size() == members.size(),
         "selected member missing or dangling pax header");

    for (;;) {
        interruption::check();
        need(std::chrono::steady_clock::now() < deadline, "archive deadline reached");
        const auto got = input.read(buffer.data(), buffer.size());

        if (!got) {
            break;
        }

        need(std::all_of(buffer.begin(), buffer.begin() + got,
                         [](char c) {
                             return c == 0;
                         }),
             "nonzero trailing tar data");
    }
}
#endif
} // namespace

static Json import_selected(const std::filesystem::path &file, const Json &owner,
                            const Json &system, const Json &originals,
                            const std::filesystem::path &output, std::uint64_t reserve,
                            bool system_only) {
    ordinary(file);
    ordinary(output);
    need(std::filesystem::is_regular_file(file) && std::filesystem::hard_link_count(file) == 1,
         "ordinary owner archive required");
    need(!std::filesystem::exists(output) && std::filesystem::is_directory(output.parent_path()),
         "fresh output required");
    const auto source_identity = cache_file_identity(file);
    const auto archive_sha = owner.at("sha256").get<std::string>();
    const auto archive_bytes = owner.at("bytes").get<std::uint64_t>();
    const auto &inner = owner.at("inner");
    const auto inner_name = inner.at("file").get<std::string>(),
               inner_sha = inner.at("sha256").get<std::string>();
    const auto inner_bytes = inner.at("bytes").get<std::uint64_t>();
    name(inner_name);
    need(sha256_text(archive_sha) && sha256_text(inner_sha) && archive_bytes > 0 &&
                 archive_bytes <= bound && inner_bytes > 0 && inner_bytes <= bound,
         "exact archive pins/bounds required");
    need(std::filesystem::file_size(file) == archive_bytes &&
                 pinned_file_sha256(file) == archive_sha,
         "owner archive pin differs");
    const auto bytes = system.at("bytes").get<std::uint64_t>();
    const auto system_sha = system.at("sha256").get<std::string>();
    need(bytes > 0 && bytes <= (8ULL << 30) && sha256_text(system_sha) &&
                 system.at("segments").is_array() && !system.at("segments").empty() &&
                 system.at("segments").size() <= 64,
         "bounded scatter recipe required");
    std::vector<Member> members;
    std::vector<std::uint64_t> offsets;
    std::set<std::string> names;
    std::uint64_t total = 0, last_end = 0;

    for (const auto &part : system.at("segments")) {
        const auto member = part.at("member").get<std::string>(),
                   sha = part.at("sha256").get<std::string>();
        name(member);
        const auto offset = part.at("offset").get<std::uint64_t>(),
                   size = part.at("bytes").get<std::uint64_t>();
        need(names.insert(member).second && sha256_text(sha) && size > 0 && offset >= last_end &&
                     offset <= bytes && size <= bytes - offset && size <= bound - total,
             "scatter overlap/extent/pin rejected");
        members.push_back({member, sha, size, {}});
        offsets.push_back(offset);
        last_end = offset + size;
        total += size;
    }

    need(originals.is_object() && (system_only ? originals.empty() : !originals.empty()) &&
                 originals.size() <= 16,
         "bounded QNX raw roles required");

    for (auto it = originals.begin(); it != originals.end(); ++it) {
        name(it.key());
        need(std::filesystem::path(it.key()).filename() == it.key() &&
                     it.value().at("file") == it.key() &&
                     it.value().at("acquisition").at("kind") == "raw-tar-member",
             "exact raw role required");
        const auto member = it.value().at("acquisition").at("member").get<std::string>(),
                   sha = it.value().at("sha256").get<std::string>();
        name(member);
        const auto size = it.value().at("bytes").get<std::uint64_t>();
        need(names.insert(member).second && sha256_text(sha) && size > 0 && size <= bound - total,
             "raw member pin/bound rejected");
        members.push_back({member, sha, size, {}});
        total += size;
    }

    need(members.size() <= 64 && reserve <= UINT64_MAX - inner_bytes - 2 * total - bytes,
         "disk bound overflow");
    space(output.parent_path(), reserve + inner_bytes + 2 * total + bytes);
    need(std::filesystem::create_directory(output), "exclusive output create failed");
    ensure_private_directory(output);
    const auto identity = directory_identity(output);
    bool published = false;
    struct Cleanup {
        std::filesystem::path path;
        Json identity;
        bool &published;

        ~Cleanup() {
            if (published) {
                return;
            }

            try {
                if (directory_identity(path) == identity) {
                    std::filesystem::remove_all(path);
                }
            } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
            }
        }
    } cleanup{output, identity, published};
    const auto stage = output / ".stage";
    ensure_private_directory(stage);
    ensure_private_directory(output / "qnx");
    const Member nested_member{inner_name, inner_sha, inner_bytes, stage / "update.tgz"};
    extract_pinned_zip_members(file, archive_sha, {{inner_name, 0, inner_bytes}},
                               nested_member.file, inner_bytes, reserve);
    exact(nested_member);

    for (std::size_t i = 0; i < members.size(); ++i) {
        members[i].file = stage / ("member-" + std::to_string(i));
    }

    extract(nested_member.file, members, stage, reserve);
    const auto image = output / "system.img";
    std::ofstream assembled(image, std::ios::binary);
    need(bool(assembled), "scatter output open failed");
    assembled.seekp(static_cast<std::streamoff>(bytes - 1));
    assembled.put('\0');
    need(bool(assembled), "scatter extent create failed");

    for (std::size_t i = 0; i < offsets.size(); ++i) {
        std::ifstream input(members[i].file, std::ios::binary);
        assembled.seekp(static_cast<std::streamoff>(offsets[i]));
        copy(input, assembled, members[i].bytes, stage, reserve);
    }

    assembled.close();
    exact({"system.img", system_sha, bytes, image});
    std::size_t index = offsets.size();
    Json raw = Json::object();

    for (auto it = originals.begin(); it != originals.end(); ++it, ++index) {
        const auto target = output / "qnx" / it.key();
        std::filesystem::rename(members[index].file, target);
        exact({members[index].name, members[index].sha, members[index].bytes, target});
        raw[it.key()] = {{"file", (std::filesystem::path("qnx") / it.key()).generic_string()},
                         {"bytes", members[index].bytes},
                         {"sha256", members[index].sha}};
    }

    need(cache_file_identity(file) == source_identity && pinned_file_sha256(file) == archive_sha,
         "owner archive changed");
    std::filesystem::remove_all(stage);
    Json receipt = {{"schema", 1},
                    {"kind", "native-owner-zip-tgz-raw-scatter"},
                    {"archive_sha256", archive_sha},
                    {"inner_sha256", inner_sha},
                    {"system", {{"file", "system.img"}, {"bytes", bytes}, {"sha256", system_sha}}},
                    {"qnx", raw},
                    {"native", true},
                    {"guest_started", false},
                    {"vehicle_modified", false}};
    write_private_json(output / "receipt.json", receipt);
    published = true;

    return receipt;
}

Json import_owner_update_archive(const std::filesystem::path &file, const Json &owner,
                                 const Json &system, const Json &originals,
                                 const std::filesystem::path &output, std::uint64_t reserve) {
    return import_selected(file, owner, system, originals, output, reserve, false);
}

Json import_owner_update_system(const std::filesystem::path &file, const Json &owner,
                                const Json &system, const std::filesystem::path &output,
                                std::uint64_t reserve) {
    return import_selected(file, owner, system, Json::object(), output, reserve, true);
}

Json owner_update_directory_identity(const std::filesystem::path &path) {
    return directory_identity(path);
}

Json import_pinned_zip_image(const std::filesystem::path &file, const Json &source,
                             const Json &image, const std::filesystem::path &output,
                             std::uint64_t reserve, const std::function<bool()> &cancelled,
                             PinnedZipImageKind kind) {
    ordinary(file);
    ordinary(output);
    need(std::filesystem::is_regular_file(file) && std::filesystem::hard_link_count(file) == 1,
         "ordinary split archive required");
    need(!std::filesystem::exists(output) && !std::filesystem::is_symlink(output),
         "fresh split output required");
    need(source.is_object() && source.size() == 2 && image.is_object() && image.size() == 3,
         "exact split source/image descriptors required");
    const auto archive_sha = source.at("sha256").get<std::string>(),
               image_sha = image.at("sha256").get<std::string>();
    const auto archive_bytes = source.at("bytes").get<std::uint64_t>(),
               image_bytes = image.at("bytes").get<std::uint64_t>();
    need(sha256_text(archive_sha) && sha256_text(image_sha) && archive_bytes > 0 &&
                 archive_bytes <= 8ULL << 30 && image_bytes > 0 && image_bytes <= 4ULL << 30,
         "bounded split source/image pins required");
    const auto &segments = image.at("segments");
    need(segments.is_array() && (kind == PinnedZipImageKind::SingleImage
                                         ? segments.size() == 1
                                         : segments.size() >= 2 && segments.size() <= 16),
         "bounded typed image fragments required");
    std::vector<ZipExtraction> fragments;
    std::uint64_t end = 0;

    for (const auto &item : segments) {
        need(item.is_object() && item.size() == 3, "exact split fragment fields required");
        const auto member = item.at("member").get<std::string>();
        name(member);
        const auto offset = item.at("offset").get<std::uint64_t>(),
                   bytes = item.at("bytes").get<std::uint64_t>();
        need(offset == end && bytes > 0 && offset < image_bytes && bytes <= image_bytes - offset,
             "contiguous split fragments required");
        fragments.push_back({member, offset, bytes});
        end += bytes;
    }

    need(end == image_bytes, "complete split image coverage required");
    const auto epoch = cache_file_identity(file);
    need(std::filesystem::file_size(file) == archive_bytes &&
                 pinned_file_sha256(file) == archive_sha && cache_file_identity(file) == epoch,
         "split source pin differs");
    need(!cancelled || !cancelled(), "split import cancelled");
    const auto available = std::filesystem::space(output.parent_path()).available;
    need(available >= reserve && available - reserve >= image_bytes,
         "split image disk budget unavailable");
    need(std::filesystem::create_directory(output), "exclusive split output required");
    ensure_private_directory(output);
    const auto identity = directory_identity(output);
    bool published = false;
    struct Cleanup {
        const std::filesystem::path &path;
        const Json &identity;
        bool &published;

        ~Cleanup() {
            if (!published) {
                try {
                    if (directory_identity(path) == identity) {
                        std::filesystem::remove_all(path);
                    }
                } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
                }
            }
        }
    } cleanup{output, identity, published};
    const auto target = output / "system.img";
    extract_pinned_zip_members(file, archive_sha, fragments, target, image_bytes, reserve,
                               cancelled);
    exact({"system.img", image_sha, image_bytes, target});
    need(cache_file_identity(file) == epoch && pinned_file_sha256(file) == archive_sha,
         "split source changed");
    Json receipt = {
            {"schema", 1},
            {"kind", kind == PinnedZipImageKind::SingleImage ? "native-pinned-zip-single-raw"
                                                             : "native-pinned-zip-split-raw"},
            {"archive_sha256", archive_sha},
            {"system", {{"file", "system.img"}, {"bytes", image_bytes}, {"sha256", image_sha}}},
            {"parts_materialized", false},
            {"native", true},
            {"guest_started", false}};
    write_private_json(output / "receipt.json", receipt);
    published = true;

    return receipt;
}
} // namespace voyemu
