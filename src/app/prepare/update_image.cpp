#include "update_image.hpp"
#include "check.hpp"
#include "error.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "qnx_update.hpp"
#include "quickboot.hpp"
#include "zip_archive.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <map>
#include <set>
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid update image"};

void ordinary(const std::filesystem::path &file) {
    ordinary_path(file, need, PathForm::Normalized, "UPDATE_IMAGE_PATH", "UPDATE_IMAGE_LINK");
}

std::string basename(std::string_view value) {
    const auto slash = value.rfind('/');

    return std::string(value.substr(slash == std::string_view::npos ? 0 : slash + 1));
}

void space(const std::filesystem::path &directory, std::uint64_t reserve) {
    need(std::filesystem::space(directory).available >= reserve, "UPDATE_IMAGE_DISK_RESERVE");
}
} // namespace

Json import_android_update_image(const std::filesystem::path &file, std::string_view archive_sha256,
                                 const Json &originals, const std::filesystem::path &codec,
                                 std::string_view codec_sha256, const std::filesystem::path &output,
                                 std::uint64_t reserve) {
    ordinary(file);
    ordinary(output);
    need(sha256_text(archive_sha256) && originals.is_object() && originals.size() == 1,
         "UPDATE_IMAGE_PINS");
    need(std::filesystem::is_regular_file(file) && std::filesystem::hard_link_count(file) == 1,
         "UPDATE_IMAGE_ARCHIVE");
    need(!std::filesystem::exists(output) && std::filesystem::is_directory(output.parent_path()),
         "UPDATE_IMAGE_FRESH_OUTPUT");
    const auto epoch = cache_file_identity(file);
    const auto inventory = pinned_zip_inventory(file, std::string(archive_sha256));
    std::map<std::string, ZipEntry> entries;

    for (const auto &entry : inventory) {
        need(entries.emplace(entry.name, entry).second, "UPDATE_IMAGE_DUPLICATE");
    }

    std::set<std::string> selected;
    std::uint64_t needed = reserve;
    std::vector<std::string> roles;

    for (auto it = originals.begin(); it != originals.end(); ++it) {
        need(it.key() == "la_system.img" || it.key() == "la_vendor.img" ||
                     it.key() == "la_resc.img",
             "UPDATE_IMAGE_ROLE");
        roles.push_back(it.key());
    }

    for (const auto &role : roles) {
        need(originals.contains(role), "UPDATE_IMAGE_ROLE");
        const auto &definition = originals.at(role);
        need(definition.at("file") == role &&
                     sha256_text(definition.at("sha256").get<std::string>()) &&
                     definition.at("bytes").is_number_unsigned(),
             "UPDATE_IMAGE_IMAGE_PIN");
        const auto bytes = definition.at("bytes").get<std::uint64_t>();
        need(bytes > 0 && bytes <= (4ULL << 30) &&
                     needed <= std::numeric_limits<std::uint64_t>::max() - bytes,
             "UPDATE_IMAGE_IMAGE_BOUNDS");
        needed += bytes;
        const auto &update = definition.at("update");
        const auto kind = update.at("kind").get<std::string>();
        const auto &names = update.at("entries");
        need((kind == "raw" || kind == "zero-base-zstd" || kind == "zero-base-zstd-segments") &&
                     names.is_array() && !names.empty() && names.size() <= 64 &&
                     (kind != "raw" || names.size() == 1),
             "UPDATE_IMAGE_RECIPE");
        std::string directory;
        std::uint64_t packed = 0;
        unsigned index = 0;

        for (const auto &name : names) {
            need(name.is_string(), "UPDATE_IMAGE_ENTRY_NAME");
            const auto value = name.get<std::string>();
            const auto found = entries.find(value);
            need(found != entries.end() && !found->second.directory &&
                         selected.insert(value).second && found->second.bytes > 0 &&
                         found->second.bytes <= (4ULL << 30) - packed,
                 "UPDATE_IMAGE_ENTRY_PIN");
            packed += found->second.bytes;

            if (kind == "raw") {
                need(basename(value) == role && packed == bytes, "UPDATE_IMAGE_RAW_ROLE");
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
                     "UPDATE_IMAGE_FRAGMENT_ORDER");
            }

            ++index;
        }

        if (kind != "raw") {
            const auto prefix = (directory.empty() ? std::string{} : directory + "/") +
                                std::string(role).substr(0, std::string(role).size() - 4) +
                                ".patch";

            for (const auto &[name, entry] : entries) {
                if (!entry.directory && name.starts_with(prefix)) {
                    need(selected.contains(name), "UPDATE_IMAGE_UNDECLARED_FRAGMENT");
                }
            }
        }

        if (kind == "zero-base-zstd-segments") {
            const auto &sizes = update.at("segment_bytes");
            need(sizes.is_array() && sizes.size() == names.size(), "UPDATE_IMAGE_SEGMENT_COUNT");
            std::uint64_t total = 0;

            for (const auto &size : sizes) {
                need(size.is_number_unsigned(), "UPDATE_IMAGE_SEGMENT_SIZE");
                const auto value = size.get<std::uint64_t>();
                need(value > 0 && value <= (512ULL << 20) && value <= bytes - total,
                     "UPDATE_IMAGE_SEGMENT_SIZE");
                total += value;
            }

            need(total == bytes, "UPDATE_IMAGE_SEGMENT_EXTENT");
        }

        need(needed <= std::numeric_limits<std::uint64_t>::max() - packed,
             "UPDATE_IMAGE_SPACE_OVERFLOW");
        needed += packed;
    }

    need(std::filesystem::space(output.parent_path()).available >= needed,
         "UPDATE_IMAGE_DISK_PREFLIGHT");
    need(std::filesystem::create_directory(output), "UPDATE_IMAGE_CREATE");
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
    Json records = Json::object();

    for (auto item = originals.begin(); item != originals.end(); ++item) {
        const auto &definition = item.value();
        const auto &update = definition.at("update");

        if (update.at("kind") == "zero-base-zstd-segments") {
            std::vector<ZipTreeEntry> parts;
            unsigned index = 0;

            for (const auto &name : update.at("entries")) {
                const auto value = name.get<std::string>();
                parts.push_back({value, stage / (item.key() + ".patch" + std::to_string(index++)),
                                 entries.at(value).bytes});
            }

            extract_pinned_zip_tree(file, std::string(archive_sha256), parts, reserve);
            std::vector<QnxUpdateSegment> segments;

            for (unsigned segment = 0; segment < parts.size(); ++segment) {
                segments.push_back({parts[segment].output, sha256_file(parts[segment].output),
                                    update.at("segment_bytes").at(segment).get<std::uint64_t>(),
                                    segment});
            }

            const auto bytes = definition.at("bytes").get<std::uint64_t>();
            const auto image_pin = definition.at("sha256").get<std::string>();
            restore_qnx_zero_base_segments(segments, bytes, image_pin, codec, codec_sha256,
                                           output / item.key());

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
        std::vector<ZipExtraction> fragments;
        std::uint64_t stream_bytes = 0;

        for (const auto &name : update.at("entries")) {
            const auto value = name.get<std::string>();
            fragments.push_back({value, stream_bytes, entries.at(value).bytes});
            stream_bytes += entries.at(value).bytes;
        }

        extract_pinned_zip_members(file, std::string(archive_sha256), fragments, patch,
                                   stream_bytes, reserve);
        const auto target = output / item.key();
        const auto bytes = definition.at("bytes").get<std::uint64_t>();
        const auto image_pin = definition.at("sha256").get<std::string>();

        if (update.at("kind") == "raw") {
            need(std::filesystem::file_size(patch) == bytes && sha256_file(patch) == image_pin,
                 "UPDATE_IMAGE_RAW_SHA");
            std::filesystem::rename(patch, target);
        } else {
            restore_qnx_zero_base_patch(patch, sha256_file(patch), bytes, image_pin, codec,
                                        codec_sha256, target);
            std::filesystem::remove(patch);
        }

        records[item.key()] = {{"file", item.key()},
                               {"bytes", bytes},
                               {"sha256", image_pin},
                               {"source", "owner-update"},
                               {"update", update}};
        space(output, reserve);
    }

    need(cache_file_identity(file) == epoch && sha256_file(file) == archive_sha256,
         "UPDATE_IMAGE_ARCHIVE_CHANGED");
    std::filesystem::remove(stage);
    const Json receipt = {{"schema", 1},
                          {"kind", "native-android-update-import"},
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
