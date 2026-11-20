#include "qnx_update_import.hpp"
#include "check.hpp"
#include "disk_space.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "qnx_qinggan11.hpp"
#include "qnx_update.hpp"
#include "quickboot.hpp"
#include "zip_archive.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#ifdef __APPLE__
#include <archive.h>
#include <archive_entry.h>
#endif
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid QNX update import"};

void ordinary(const std::filesystem::path &file) {
    ordinary_path(file, need, PathForm::Normalized, "QNX_IMPORT_PATH", "QNX_IMPORT_LINK");
}

std::string basename(std::string_view value) {
    const auto slash = value.rfind('/');

    return std::string(value.substr(slash == std::string_view::npos ? 0 : slash + 1));
}

void space(const std::filesystem::path &directory, std::uint64_t reserve) {
    require_disk_space("QNX_IMPORT_DISK_RESERVE", directory, 0, reserve);
}

#ifndef __APPLE__
void append(std::ifstream &source, std::ofstream &output, std::uint64_t bytes,
            const std::filesystem::path &directory, std::uint64_t reserve) {
    std::array<char, 65536> buffer{};
    std::uint64_t copied = 0;

    while (copied < bytes) {
        interruption::check();
        const auto count = static_cast<std::streamsize>(
                std::min<std::uint64_t>(buffer.size(), bytes - copied));
        source.read(buffer.data(), count);
        need(source.gcount() == count, "QNX_IMPORT_SHORT_ENTRY");
        output.write(buffer.data(), count);
        need(bool(output), "QNX_IMPORT_WRITE");
        copied += count;

        if (copied % (64ULL << 20) == 0) {
            space(directory, reserve);
        }
    }

    need(source.peek() == std::char_traits<char>::eof(), "QNX_IMPORT_ENTRY_EXTENT");
}
#endif

#ifdef __APPLE__
void selected_entry(const std::filesystem::path &file, const std::string &selected,
                    std::uint64_t bytes, std::ofstream &output, const std::filesystem::path &stage,
                    std::uint64_t reserve) {
    struct archive *reader = archive_read_new();

    need(reader != nullptr, "QNX_IMPORT_READER");
    struct Cleanup {
        archive *value;

        ~Cleanup() {
            archive_read_free(value);
        }
    } cleanup{reader};
    need(archive_read_support_format_zip(reader) == ARCHIVE_OK &&
                 archive_read_open_filename(reader, file.c_str(), 65536) == ARCHIVE_OK,
         "QNX_IMPORT_ZIP_OPEN");
    archive_entry *header = nullptr;
    bool found = false;

    for (;;) {
        const auto status = archive_read_next_header(reader, &header);

        if (status == ARCHIVE_EOF) {
            break;
        }

        need(status == ARCHIVE_OK, "QNX_IMPORT_ZIP_HEADER");
        const auto *raw = archive_entry_pathname_utf8(header);
        need(raw != nullptr, "QNX_IMPORT_ZIP_NAME");

        if (selected != raw) {
            need(archive_read_data_skip(reader) == ARCHIVE_OK, "QNX_IMPORT_ZIP_SKIP");
            continue;
        }

        need(!found && archive_entry_filetype(header) == AE_IFREG &&
                     !archive_entry_is_encrypted(header) && !archive_entry_hardlink(header) &&
                     !archive_entry_symlink(header) && archive_entry_size_is_set(header) &&
                     archive_entry_size(header) >= 0 &&
                     static_cast<std::uint64_t>(archive_entry_size(header)) == bytes,
             "QNX_IMPORT_ZIP_ENTRY");
        found = true;
        std::array<char, 65536> buffer{};
        std::uint64_t copied = 0;

        for (;;) {
            interruption::check();
            const auto count = archive_read_data(reader, buffer.data(), buffer.size());
            need(count >= 0, "QNX_IMPORT_ZIP_CRC");

            if (!count) {
                break;
            }

            need(static_cast<std::uint64_t>(count) <= bytes - copied, "QNX_IMPORT_ZIP_EXTENT");
            output.write(buffer.data(), count);
            need(bool(output), "QNX_IMPORT_WRITE");
            copied += count;

            if (copied % (64ULL << 20) == 0) {
                space(stage, reserve);
            }
        }

        need(copied == bytes, "QNX_IMPORT_ZIP_TRUNCATED");
    }

    need(found, "QNX_IMPORT_ENTRY_MISSING");
}
#endif
} // namespace

Json import_qnx_update(const std::filesystem::path &file, std::string_view archive_sha256,
                       const Json &originals, const std::filesystem::path &codec,
                       std::string_view codec_sha256, const std::filesystem::path &output,
                       std::uint64_t reserve, const std::function<void(std::uint64_t)> &advance) {
    ordinary(file);
    ordinary(output);
    need(sha256_text(archive_sha256) && originals.is_object() &&
                 (originals.size() == 4 || qnx_qinggan11_pinned_originals(originals)),
         "QNX_IMPORT_PINS");
    need(std::filesystem::is_regular_file(file) && std::filesystem::hard_link_count(file) == 1,
         "QNX_IMPORT_ARCHIVE");
    need(!std::filesystem::exists(output) && std::filesystem::is_directory(output.parent_path()),
         "QNX_IMPORT_FRESH_OUTPUT");
    const auto epoch = cache_file_identity(file);
    const auto inventory = pinned_zip_inventory(file, std::string(archive_sha256));
    std::map<std::string, ZipEntry> entries;

    for (const auto &entry : inventory) {
        need(entries.emplace(entry.name, entry).second, "QNX_IMPORT_DUPLICATE");
    }

    std::set<std::string> selected;
    std::uint64_t needed = reserve;

    for (const auto *role :
         {"hyp_la.img", "ifs2_la.img", "system_la.img", "ifs2_recovery_la.img"}) {
        if (originals.size() == 3 && std::string_view(role) == "ifs2_recovery_la.img") {
            continue;
        }

        need(originals.contains(role), "QNX_IMPORT_ROLE");
        const auto &definition = originals.at(role);
        need(definition.at("file") == role &&
                     sha256_text(definition.at("sha256").get<std::string>()) &&
                     definition.at("bytes").is_number_unsigned(),
             "QNX_IMPORT_IMAGE_PIN");
        const auto bytes = definition.at("bytes").get<std::uint64_t>();
        need(bytes > 0 && bytes <= (4ULL << 30) &&
                     needed <= std::numeric_limits<std::uint64_t>::max() - bytes,
             "QNX_IMPORT_IMAGE_BOUNDS");
        needed += bytes;
        const auto &update = definition.at("update");
        const auto kind = update.at("kind").get<std::string>();
        const auto &names = update.at("entries");
        need((kind == "raw" || kind == "zero-base-zstd" || kind == "zero-base-zstd-segments") &&
                     names.is_array() && !names.empty() && names.size() <= 64 &&
                     (kind != "raw" || names.size() == 1),
             "QNX_IMPORT_RECIPE");
        std::string directory;
        std::uint64_t packed = 0;
        unsigned index = 0;

        for (const auto &name : names) {
            need(name.is_string(), "QNX_IMPORT_ENTRY_NAME");
            const auto value = name.get<std::string>();
            const auto found = entries.find(value);
            need(found != entries.end() && !found->second.directory &&
                         selected.insert(value).second && found->second.bytes > 0 &&
                         found->second.bytes <= (4ULL << 30) - packed,
                 "QNX_IMPORT_ENTRY_PIN");
            packed += found->second.bytes;

            if (kind == "raw") {
                need(basename(value) == role && packed == bytes, "QNX_IMPORT_RAW_ROLE");
            } else {
                const auto slash = value.rfind('/');
                const auto parent = slash == std::string::npos ? std::string{}
                                                               : value.substr(0, slash);

                if (index == 0) {
                    directory = parent;
                }

                need(parent == directory &&
                             basename(value) ==
                                     std::string(role).substr(0, std::string(role).size() - 4) +
                                             ".patch" + std::to_string(index),
                     "QNX_IMPORT_FRAGMENT_ORDER");
            }

            ++index;
        }

        if (kind != "raw") {
            const auto prefix = (directory.empty() ? std::string{} : directory + "/") +
                                std::string(role).substr(0, std::string(role).size() - 4) +
                                ".patch";

            for (const auto &[name, entry] : entries) {
                if (!entry.directory && name.starts_with(prefix)) {
                    need(selected.contains(name), "QNX_IMPORT_UNDECLARED_FRAGMENT");
                }
            }
        }

        if (kind == "zero-base-zstd-segments") {
            const auto &sizes = update.at("segment_bytes");
            need(sizes.is_array() && sizes.size() == names.size(), "QNX_IMPORT_SEGMENT_COUNT");
            std::uint64_t total = 0;

            for (const auto &size : sizes) {
                need(size.is_number_unsigned(), "QNX_IMPORT_SEGMENT_SIZE");
                const auto value = size.get<std::uint64_t>();
                need(value > 0 && value <= (512ULL << 20) && value <= bytes - total,
                     "QNX_IMPORT_SEGMENT_SIZE");
                total += value;
            }

            need(total == bytes, "QNX_IMPORT_SEGMENT_EXTENT");
        }

        need(needed <= std::numeric_limits<std::uint64_t>::max() - packed,
             "QNX_IMPORT_SPACE_OVERFLOW");
        needed += packed;
    }

    require_disk_space("QNX_IMPORT_DISK_PREFLIGHT", output.parent_path(), needed - reserve,
                       reserve);
    need(std::filesystem::create_directory(output), "QNX_IMPORT_CREATE");
    ensure_private_directory(output);
    const auto stage = output / (".stage-" + random_token());
    std::filesystem::create_directory(stage);
    bool published = false;
    struct Cleanup {
        std::filesystem::path output;
        bool &published;

        ~Cleanup() {
            if (!published) {
                std::error_code error;
                std::filesystem::remove_all(output, error);
            }
        }
    } cleanup{output, published};
#ifndef __APPLE__
    // Validate and stream the entire selected set in one pass over the pinned ZIP.
    std::map<std::string, std::filesystem::path> staged_entries;
    std::vector<ZipTreeEntry> batch;

    for (const auto &name : selected) {
        const auto path = stage / ("entry-" + std::to_string(batch.size()));
        staged_entries.emplace(name, path);
        batch.push_back({name, path, entries.at(name).bytes});
    }
    extract_pinned_zip_tree(file, std::string(archive_sha256), batch, reserve);
#endif
    const auto materialize_entry = [&](const std::string &name, const std::filesystem::path &path) {
#ifdef __APPLE__
        std::ofstream extracted(path, std::ios::binary);
        need(bool(extracted), "QNX_IMPORT_ENTRY_OPEN");
        selected_entry(file, name, entries.at(name).bytes, extracted, stage, reserve);
        extracted.close();
        need(bool(extracted), "QNX_IMPORT_ENTRY_CLOSE");
#else
        std::filesystem::rename(staged_entries.at(name), path);
#endif
    };
    Json records = Json::object();

    for (auto item = originals.begin(); item != originals.end(); ++item) {
        const auto &definition = item.value();
        const auto &update = definition.at("update");

        if (update.at("kind") == "zero-base-zstd-segments") {
            std::vector<QnxUpdateSegment> segments;
            unsigned index = 0;

            for (const auto &name : update.at("entries")) {
                const auto value = name.get<std::string>();
                const auto part = stage / (item.key() + ".patch" + std::to_string(index));
                materialize_entry(value, part);
                segments.push_back({part, sha256_file(part),
                                    update.at("segment_bytes").at(index).get<std::uint64_t>(),
                                    index});
                ++index;
            }

            const auto bytes = definition.at("bytes").get<std::uint64_t>();
            const auto image_pin = definition.at("sha256").get<std::string>();
            restore_qnx_zero_base_segments(segments, bytes, image_pin, codec, codec_sha256,
                                           output / item.key(), advance);

            for (const auto &segment : segments) {
                std::filesystem::remove(segment.patch);
            }

            records[item.key()] = {{"file", item.key()},
                                   {"bytes", bytes},
                                   {"sha256", image_pin},
                                   {"source", "owner-update"},
                                   {"update", update}};
            space(output, reserve);
            continue;
        }

        const auto patch = stage / (item.key() + ".stream");
        const auto &names = update.at("entries");

        if (names.size() == 1) {
            materialize_entry(names.front().get<std::string>(), patch);
        } else {
            std::ofstream stream(patch, std::ios::binary);
            need(bool(stream), "QNX_IMPORT_STREAM_OPEN");

            for (const auto &name : names) {
                const auto value = name.get<std::string>();
#ifdef __APPLE__
                selected_entry(file, value, entries.at(value).bytes, stream, stage, reserve);
#else
                const auto &part = staged_entries.at(value);
                std::ifstream input(part, std::ios::binary);
                need(bool(input), "QNX_IMPORT_ENTRY_OPEN");
                append(input, stream, entries.at(value).bytes, stage, reserve);
                input.close();
                std::filesystem::remove(part);
#endif
            }

            stream.close();
            need(bool(stream), "QNX_IMPORT_STREAM_CLOSE");
        }

        const auto target = output / item.key();
        const auto bytes = definition.at("bytes").get<std::uint64_t>();
        const auto image_pin = definition.at("sha256").get<std::string>();

        if (update.at("kind") == "raw") {
            need(std::filesystem::file_size(patch) == bytes && sha256_file(patch) == image_pin,
                 "QNX_IMPORT_RAW_SHA");
            std::filesystem::rename(patch, target);
        } else {
            restore_qnx_zero_base_patch(patch, sha256_file(patch), bytes, image_pin, codec,
                                        codec_sha256, target);
            std::filesystem::remove(patch);
        }

        if (advance) {
            advance(bytes);
        }

        records[item.key()] = {{"file", item.key()},
                               {"bytes", bytes},
                               {"sha256", image_pin},
                               {"source", "owner-update"},
                               {"update", update}};
        space(output, reserve);
    }

    need(cache_file_identity(file) == epoch && pinned_file_sha256(file) == archive_sha256,
         "QNX_IMPORT_ARCHIVE_CHANGED");
    std::filesystem::remove(stage);
    const Json receipt = {{"schema", 1},
                          {"kind", "native-qnx-update-import"},
                          {"archive_sha256", archive_sha256},
                          {"originals", records},
                          {"vehicle_modified", false},
                          {"guest_started", false},
                          {"native", true}};
    write_private_json(output / "update-import.json", receipt);
    published = true;

    return receipt;
}
} // namespace voyemu
