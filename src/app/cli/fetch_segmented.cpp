#include "fetch_segmented.hpp"
#include "check.hpp"
#include "cli_messages.hpp"
#include "error.hpp"
#include "ext4_image.hpp"
#include "fetch.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "project_files.hpp"
#include "qnx_qinggan11.hpp"
#include "qnx_update_import.hpp"
#include "quickboot.hpp"
#include "update_image.hpp"
#include "zip_archive.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#ifdef __APPLE__
#include <stdio.h>
#endif
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace voyemu {
namespace {
constexpr Check need{"FETCH_UPDATE"};

void ordinary(const std::filesystem::path &path) {
    ordinary_path(path, need, PathForm::Normalized, "normalized absolute path required",
                  "linked path rejected");
}

// advance receives the byte count of each hashed block, for a progress meter.
bool matches(const std::filesystem::path &file, const std::string &pin, std::uint64_t bytes,
             const std::function<void(std::uint64_t)> &advance = {}) {
    ordinary(file);

    if (!std::filesystem::exists(file)) {
        return false;
    }

    need(std::filesystem::is_regular_file(file) && std::filesystem::hard_link_count(file) == 1 &&
                 std::filesystem::file_size(file) == bytes,
         "retained input extent/type differs");
    const auto epoch = cache_file_identity(file);
    need(sha256_file(file, advance) == pin && cache_file_identity(file) == epoch,
         "retained input SHA differs");

    return true;
}

void publish(const std::filesystem::path &from, const std::filesystem::path &to) {
    need(os::rename_exclusive(from, to), "exclusive publication failed");
}

struct Input {
    std::string file, pin, image, path;
    std::uint64_t bytes;
};

bool pinned(const Json &value) {
    return value.is_string() && sha256_text(value.get_ref<const std::string &>());
}

// One image of the shared update layout: <directory>/<base>/<base>.patchN segments in order, each
// at most 512 MiB, together the exact pinned image extent.
void segmented_recipe(const Json &image, const std::string &directory, const std::string &base) {
    need(image.at("file") == base + ".img" && pinned(image.at("sha256")) &&
                 image.at("bytes").is_number_unsigned() && image.at("bytes") > 0 &&
                 image.at("update").at("kind") == "zero-base-zstd-segments",
         "image recipe differs");
    const auto &entries = image.at("update").at("entries");
    const auto &sizes = image.at("update").at("segment_bytes");
    need(!entries.empty() && entries.size() <= 64 && sizes.size() == entries.size(),
         "image recipe differs");
    std::uint64_t total = 0;

    for (std::size_t i = 0; i < entries.size(); ++i) {
        need(entries.at(i) == directory + "/" + base + "/" + base + ".patch" + std::to_string(i) &&
                     sizes.at(i).is_number_unsigned() && sizes.at(i) > 0 &&
                     sizes.at(i) <= (512ULL << 20),
             "ordered image segment differs");
        total += sizes.at(i).get<std::uint64_t>();
    }

    need(total == image.at("bytes"), "image segments differ from image extent");
}

// Image and path of each original.
Json segmented_original_records(const Json &definition, const Json &images) {
    Json records = Json::array();
    std::uint64_t total = 0;

    for (const auto &input : original_input_plan(definition)) {
        const auto &item = definition.at("original").at(input.name);
        const auto image = item.at("acquisition").at("image").get<std::string>();
        auto path = item.at("acquisition").at("path").get<std::string>();
        need(images.contains(image) && path.starts_with("/" + image + "/"),
             "partition/path mismatch");
        const bool tree = item.value("type", std::string{}) == "tree";
        const auto child = tree ? input.file.substr(item.at("file").get<std::string>().size() + 1)
                                : std::string{};

        if (tree) {
            path += "/" + child;
        }

        need(path.size() < 1024 && path.back() != '/' && path.find("//") == std::string::npos &&
                     std::filesystem::path(path).lexically_normal().generic_string() == path &&
                     path.find("..") == std::string::npos &&
                     path.find_first_of("\\\r\n\t:") == std::string::npos,
             "invalid input path");
        const auto &size = tree ? item.at("bytes").at(child) : item.at("bytes");
        need(size.is_number_unsigned(), "unsigned original extent required");
        const auto bytes = size.get<std::uint64_t>();
        need(bytes > 0 && bytes <= (512ULL << 20), "bounded original extent required");
        total += bytes;
        need(records.size() < 256 && total <= (2ULL << 30), "bounded input set required");
        records.push_back({{"file", input.file},
                           {"sha256", input.sha256},
                           {"bytes", bytes},
                           {"image", image},
                           {"path", path}});
    }

    need(!records.empty(), "originals required");

    return {{"images", definition.at("acquisition").at("images")}, {"originals", records}};
}
} // namespace

Json segmented_acquisition_plan(const Json &definition, const Json &qnx) {
    const auto &owner = definition.at("ownerUpdate");
    const auto &recipe = definition.at("acquisition");

    need(pinned(owner.at("sha256")) && owner.at("bytes").is_number_unsigned() &&
                 qnx.at("update").at("sha256") == owner.at("sha256") &&
                 recipe.at("format") == "pinned-zip-zero-base-zstd-segments" &&
                 recipe.at("diff") == false,
         "exact full owner update required");
    // The acquisition recipe names the QNX originals the update must restore; profile-qnx.json
    // pins exactly those.
    const auto &roles = recipe.at("qnxOriginals");
    need(roles.is_array() && !roles.empty() && roles.size() == qnx.at("originals").size(),
         "declared QNX original roles required");

    std::set<std::string> declared;

    for (const auto &role : roles) {
        need(role.is_string() && declared.insert(role.get<std::string>()).second &&
                     qnx.at("originals").contains(role.get<std::string>()) &&
                     pinned(qnx.at("originals").at(role.get<std::string>()).at("sha256")),
             "declared QNX original missing");
    }

    // A qinggan11 guest adapter takes exactly the pinned originals of its OEM build.
    const auto adapter = qnx.value("hardware_adapter", std::string{});

    if (!adapter.empty() && qnx_adapter(adapter).platform == QnxGuestPlatform::qinggan11) {
        need(qnx_qinggan11_adapter_originals_match(qnx_adapter(adapter).adapter,
                                                   qnx.at("originals")),
             "exact QNX original roles required");
    }

    for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
        need(it.key().ends_with(".img") && it.key().find('/') == std::string::npos,
             "QNX original role differs");
        segmented_recipe(it.value(), "qnx", it.key().substr(0, it.key().size() - 4));
    }

    need(!recipe.at("images").empty(), "image recipes required");

    for (auto it = recipe.at("images").begin(); it != recipe.at("images").end(); ++it) {
        need(it.key() == "system" || it.key() == "vendor" || it.key() == "resc",
             "image role differs");
        segmented_recipe(it.value(), "android", "la_" + it.key());
    }

    return segmented_original_records(definition, recipe.at("images"));
}

Json import_segmented_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                     const std::filesystem::path &update, std::uint64_t reserve) {
    ordinary(update);
    const auto started = std::chrono::steady_clock::now();
    const auto profile_name = profile_display_name(root, profile);
    const auto source = root / "profiles" / std::string(profile) / "profile.json",
               qsource = source.parent_path() / "profile-qnx.json";
    const auto profile_pin = sha256_file(source), qnx_pin = sha256_file(qsource);
    const auto definition = read_private_json(source, 512 * 1024),
               qnx = read_private_json(qsource, 128 * 1024);
    const auto &recipe = definition.at("acquisition");
    const auto plan = segmented_acquisition_plan(definition, qnx);
    const auto archive_pin = definition.at("ownerUpdate").at("sha256").get<std::string>();
    const auto destination = root / ".local/profiles" / std::string(profile);
    ordinary(destination);
    // An import that writes announces itself; a rerun checks the archive and the originals.
    bool present = true;

    for (const auto &record : plan.at("originals")) {
        present = present && std::filesystem::exists(destination / "originals" /
                                                     record.at("file").get<std::string>());
    }

    for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
        present = present && std::filesystem::exists(destination / "qnx" / it.key());
    }

    verify_owner_archive(root, profile, update,
                         definition.at("ownerUpdate").at("bytes").get<std::uint64_t>(), archive_pin,
                         !present, "FETCH_UPDATE", "FETCH_UPDATE");
    const auto archive_epoch = cache_file_identity(update);
    const auto inventory = pinned_zip_inventory(update, archive_pin);
    std::set<std::string> names;

    for (const auto &entry : inventory) {
        need(names.insert(entry.name).second, "duplicate ZIP member");
    }

    std::vector<Input> inputs;
    Json records = Json::array();

    for (const auto &image : plan.at("images")) {
        for (const auto &member : image.at("update").at("entries")) {
            need(names.contains(member.get<std::string>()), "missing image segment");
        }
    }

    for (const auto &record : plan.at("originals")) {
        inputs.push_back({record.at("file"), record.at("sha256"), record.at("image"),
                          record.at("path"), record.at("bytes")});
        records.push_back(record);
    }

    need(!inputs.empty(), "originals required");
    // Hashing the retained originals of a rerun takes seconds, so it shows its bytes.
    const auto present_bytes = [](const std::filesystem::path &file) -> std::uint64_t {
        return std::filesystem::is_regular_file(std::filesystem::symlink_status(file))
                       ? std::filesystem::file_size(file)
                       : 0;
    };
    std::uint64_t retained = 0;

    for (const auto &input : inputs) {
        retained += present_bytes(destination / "originals" / input.file);
    }

    for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
        retained += present_bytes(destination / "qnx" / it.key());
    }

    std::unique_ptr<progress::Meter> checking;

    if (retained) {
        progress::block();
        checking = std::make_unique<progress::Meter>(
                "verify", progress::Arguments{{"label", "originals"},
                                              {"version", profile_name},
                                              {"profile_name", profile_name}});
        checking->length(retained);
    }

    const auto checked = [&checking](std::uint64_t block) {
        if (checking) {
            checking->advance(block);
        }
    };
    bool cached = true;

    for (const auto &input : inputs) {
        if (!matches(destination / "originals" / input.file, input.pin, input.bytes, checked)) {
            cached = false;
        }
    }

    for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
        if (!matches(destination / "qnx" / it.key(), it.value().at("sha256"),
                     it.value().at("bytes"), checked)) {
            cached = false;
        }
    }

    auto unchanged = [&] {
        need(sha256_file(source) == profile_pin && sha256_file(qsource) == qnx_pin &&
                     cache_file_identity(update) == archive_epoch,
             "acquisition source changed");
    };
    Json result = {{"schema", 1},
                   {"profile", profile},
                   {"source", "owner-update"},
                   {"archive_sha256", archive_pin},
                   {"originals", records},
                   {"cached", cached},
                   {"native", true},
                   {"guest_started", false},
                   {"frame_verified", false},
                   {"preparation_ready", false}};

    if (cached) {
        unchanged();

        if (checking) {
            checking->finish("fetch_cached");
        }

        return result;
    }

    checking.reset();

    const auto host = std::string(os::dependency_system);
    const auto codec_spec = project_file_json(root, "src/app/toolchain.json", 256 * 1024)
                                    .at("qnxUpdateCodec")
                                    .at(host);
    const auto codec = root / codec_spec.at("file").get<std::string>();
    ordinary(codec);

    // deps installs the codec with the Android SDK tools.
    if (!std::filesystem::exists(codec)) {
        throw cli::LocalizedError("FETCH_UPDATE", "error.deps_missing",
                                  {{"profile", std::string(profile)}});
    }

    need(matches(codec, codec_spec.at("sha256").get<std::string>(),
                 std::filesystem::file_size(codec)),
         "run voyemu deps for pinned codec");
    ensure_private_directory(root / ".local/build/tmp");
    const auto stage = root / ".local/build/tmp" / ("qinggan11-731-owner-" + random_token());
    ensure_private_directory(stage);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{stage};
    ensure_private_directory(stage / "publish/originals");

    for (const auto *name : {"system", "vendor", "resc"}) {
        std::vector<Input> missing;

        for (const auto &input : inputs) {
            if (input.image == name &&
                !matches(destination / "originals" / input.file, input.pin, input.bytes)) {
                missing.push_back(input);
            }
        }

        if (missing.empty()) {
            continue;
        }

        const auto &image = recipe.at("images").at(name);
        const auto image_dir = stage / (std::string(name) + "-image");
        progress::block();
        progress::Meter unpacking("fetch_image", {{"image", std::string(name)}});
        unpacking.length(image.at("bytes").get<std::uint64_t>());
        import_android_update_image(update, archive_pin,
                                    Json{{image.at("file").get<std::string>(), image}}, codec,
                                    codec_spec.at("sha256").get<std::string>(), image_dir, reserve,
                                    [&unpacking](std::uint64_t block) {
                                        unpacking.advance(block);
                                    });
        unsigned index = 0;
        std::size_t at = 0;

        while (at < missing.size()) {
            std::vector<Input> batch;
            std::vector<std::string> paths;
            std::uint64_t bytes = 0;

            while (at < missing.size() &&
                   (batch.empty() || bytes + missing[at].bytes <= (512ULL << 20))) {
                const auto &input = missing[at++];
                batch.push_back(input);
                paths.push_back(input.image == "resc" ? input.path.substr(5) : input.path);
                bytes += input.bytes;
            }

            const auto extracted = stage / ("extract-" + std::to_string(index++));
            extract_pinned_ext4_files(image_dir / image.at("file").get<std::string>(),
                                      image.at("sha256"), paths, extracted, reserve,
                                      std::string_view(name) == "vendor" ? "vendor" : "");

            for (std::size_t i = 0; i < batch.size(); ++i) {
                const auto &input = batch[i];
                const auto actual = extracted / paths[i].substr(1);
                need(matches(actual, input.pin, input.bytes), "extracted original missing");
                const auto target = stage / "publish/originals" / input.file;
                ensure_private_directory(target.parent_path());
                std::filesystem::rename(actual, target);
            }

            std::filesystem::remove_all(extracted);
        }

        std::filesystem::remove_all(image_dir);
        progress::report("fetch_extracted", {{"count", std::to_string(missing.size())}});
    }

    bool missing_qnx = false;

    for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
        if (!std::filesystem::exists(destination / "qnx" / it.key())) {
            missing_qnx = true;
        }
    }

    if (missing_qnx) {
        std::uint64_t images = 0;

        for (const auto &original : qnx.at("originals")) {
            images += original.at("bytes").get<std::uint64_t>();
        }

        progress::block();
        progress::Meter unpacking("fetch_qnx",
                                  {{"count", std::to_string(qnx.at("originals").size())}});
        unpacking.length(images);
        import_qnx_update(update, archive_pin, qnx.at("originals"), codec,
                          codec_spec.at("sha256").get<std::string>(), stage / "qnx", reserve,
                          [&unpacking](std::uint64_t block) {
                              unpacking.advance(block);
                          });
    }

    unchanged();
    std::map<std::filesystem::path, Json> moved;
    try {
        for (const auto &input : inputs) {
            const auto from = stage / "publish/originals" / input.file;

            if (!std::filesystem::exists(from)) {
                continue;
            }

            const auto target = destination / "originals" / input.file;
            ordinary(target);
            need(!std::filesystem::exists(target), "input appeared during acquisition");
            ensure_private_directory(target.parent_path());
            publish(from, target);
            moved[target] = cache_file_identity(target);
        }

        if (missing_qnx) {
            for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
                const auto target = destination / "qnx" / it.key();

                if (matches(target, it.value().at("sha256"), it.value().at("bytes"))) {
                    continue;
                }

                ensure_private_directory(target.parent_path());
                publish(stage / "qnx" / it.key(), target);
                moved[target] = cache_file_identity(target);
            }
        }

        std::uint64_t published = 0;

        for (const auto &input : inputs) {
            published += input.bytes;
        }

        for (const auto &original : qnx.at("originals")) {
            published += original.at("bytes").get<std::uint64_t>();
        }

        progress::block();
        progress::Meter saving("fetch_publish", {});
        saving.length(published);
        const auto saved = [&saving](std::uint64_t block) {
            saving.advance(block);
        };

        for (const auto &input : inputs) {
            need(matches(destination / "originals" / input.file, input.pin, input.bytes, saved),
                 "published original missing");
        }

        for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
            need(matches(destination / "qnx" / it.key(), it.value().at("sha256"),
                         it.value().at("bytes"), saved),
                 "published QNX original missing");
        }

        unchanged();
    } catch (...) {
        for (const auto &[file, identity] : moved) {
            try {
                if (cache_file_identity(file) == identity) {
                    std::error_code ignored;
                    std::filesystem::remove(file, ignored);
                }
            } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
            }
        }

        throw;
    }

    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
    progress::block();
    progress::report("fetch_saved",
                     {{"android", std::to_string(inputs.size())},
                      {"qnx", std::to_string(qnx.at("originals").size())},
                      {"time", progress::duration_text(static_cast<std::uint64_t>(seconds),
                                                       progress::language())}});

    return result;
}
} // namespace voyemu
