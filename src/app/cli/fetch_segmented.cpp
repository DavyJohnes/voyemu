#include "fetch_segmented.hpp"
#include "check.hpp"
#include "error.hpp"
#include "ext4_image.hpp"
#include "fetch.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "project_files.hpp"
#include "qnx_qinggan11.hpp"
#include "qnx_update_import.hpp"
#include "quickboot.hpp"
#include "update_image.hpp"
#include "zip_archive.hpp"
#include <map>
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

bool matches(const std::filesystem::path &file, const std::string &pin, std::uint64_t bytes) {
    ordinary(file);

    if (!std::filesystem::exists(file)) {
        return false;
    }

    need(std::filesystem::is_regular_file(file) && std::filesystem::hard_link_count(file) == 1 &&
                 std::filesystem::file_size(file) == bytes,
         "retained input extent/type differs");
    const auto epoch = cache_file_identity(file);
    need(sha256_file(file) == pin && cache_file_identity(file) == epoch,
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

Json segmented_acquisition_plan(const Json &definition, const Json &qnx, std::string_view profile) {
    const auto id = profile.empty() ? definition.at("id").get<std::string>() : std::string(profile);
    const bool old = id == "free-11-chinese-20250710-6.11.1";
    // The A9 update has the same layout; its pins live only in profile.json and profile-qnx.json.
    const bool a9 = id == "free-09-chinese-20260106-6.0.3";

    need(old || a9 || id == "free-11-chinese-20260731-7.3.1", "exact profile required");
    const auto &owner = definition.at("ownerUpdate");
    const auto &recipe = definition.at("acquisition");

    if (a9) {
        need(pinned(owner.at("sha256")) && owner.at("bytes").is_number_unsigned() &&
                     qnx.at("update").at("sha256") == owner.at("sha256") &&
                     recipe.at("format") == "pinned-zip-zero-base-zstd-segments" &&
                     recipe.at("diff") == false && qnx.at("originals").size() == 4,
             "exact full owner update required");

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

    const auto archive_pin =
            old ? "ce6d1ffcd158c91383b61a44ea529bf54238512c520b436973fc0f6222047e42"
                : "a1b0c8e26eec79dcd738c140070ec7f31792dbafd63006d8be113c2bbdec4220";
    const std::uint64_t archive_bytes = old ? 4226266936ULL : 4137925521ULL;
    need(owner.at("sha256") == archive_pin && owner.at("bytes").is_number_unsigned() &&
                 owner.at("bytes") == archive_bytes &&
                 qnx.at("update").at("sha256") == archive_pin &&
                 recipe.at("format") == "pinned-zip-zero-base-zstd-segments" &&
                 recipe.at("diff") == false,
         "exact full owner update required");
    need(old ? qnx_qinggan11_611_originals_match(qnx.at("originals"))
             : qnx_qinggan11_originals_match(qnx.at("originals")),
         "exact QNX original roles required");

    for (const auto &pin : (old ? qnx_qinggan11_611_originals : qnx_qinggan11_originals)) {
        const auto &update = qnx.at("originals").at(pin.file).at("update");
        const auto base = std::string(pin.file).substr(0, std::string(pin.file).size() - 4);
        const auto count = (pin.bytes + (512ULL << 20) - 1) / (512ULL << 20);
        need(update.at("kind") == "zero-base-zstd-segments" &&
                     update.at("entries").size() == count &&
                     update.at("segment_bytes").size() == count,
             "QNX segment recipe differs");

        for (std::uint64_t i = 0; i < count; ++i) {
            need(update.at("entries").at(i) ==
                                 "qnx/" + base + "/" + base + ".patch" + std::to_string(i) &&
                         update.at("segment_bytes").at(i).is_number_unsigned() &&
                         update.at("segment_bytes").at(i) ==
                                 std::min<std::uint64_t>(512ULL << 20,
                                                         pin.bytes - i * (512ULL << 20)),
                 "ordered QNX segment differs");
        }
    }

    const Json image_pins =
            old ? Json{{"system",
                        {{"bytes", 4294967296ULL},
                         {"sha256",
                          "ee0c8241b7293b9cd9f936f622c556ac11ab3f92a0f60733e92dca1c1610f5c4"}}},
                       {"vendor",
                        {{"bytes", 2147483648ULL},
                         {"sha256",
                          "3043efb825b6ac90fb3380a84063c16de171bf812e4220200e40364612afdcba"}}},
                       {"resc",
                        {{"bytes", 3221225472ULL},
                         {"sha256",
                          "a5d0bc0c357d99c5f85277c13461c763f5a68bf9cf34ca71d425371b9d8decc2"}}}}
                : Json{{"system",
                        {{"bytes", 4294967296ULL},
                         {"sha256",
                          "c602475a9890bbe725a75ede7c8f667635fc1be69f9db65cae22e752c28fa3cd"}}},
                       {"vendor",
                        {{"bytes", 2147483648ULL},
                         {"sha256",
                          "9d729e9b8660222b686f3e8be3d91e01622537f3ca6b15d1d859f12a656e697b"}}},
                       {"resc",
                        {{"bytes", 3221225472ULL},
                         {"sha256",
                          "cff851763db89a2e31377933467a38612203e4c716c589193053e1619d60dfba"}}}};
    need(recipe.at("images").size() == 3, "three image recipes required");

    for (auto it = image_pins.begin(); it != image_pins.end(); ++it) {
        const auto &image = recipe.at("images").at(it.key());
        const auto count = it.value().at("bytes").get<std::uint64_t>() / (512ULL << 20);
        need(image.at("file") == "la_" + it.key() + ".img" &&
                     image.at("bytes").is_number_unsigned() &&
                     image.at("bytes") == it.value().at("bytes") &&
                     image.at("sha256") == it.value().at("sha256") &&
                     image.at("update").at("kind") == "zero-base-zstd-segments" &&
                     image.at("update").at("entries").size() == count &&
                     image.at("update").at("segment_bytes").size() == count,
             "image recipe differs");

        for (std::uint64_t i = 0; i < count; ++i) {
            const auto member = "android/la_" + it.key() + "/la_" + it.key() + ".patch" +
                                std::to_string(i);
            need(image.at("update").at("entries").at(i) == member &&
                         image.at("update").at("segment_bytes").at(i).is_number_unsigned() &&
                         image.at("update").at("segment_bytes").at(i) == 536870912ULL,
                 "ordered image segment differs");
        }
    }

    return segmented_original_records(definition, image_pins);
}

Json import_segmented_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                     const std::filesystem::path &update, std::uint64_t reserve) {
    const bool old = profile == "free-11-chinese-20250710-6.11.1",
               a9 = profile == "free-09-chinese-20260106-6.0.3";

    need(old || a9 || profile == "free-11-chinese-20260731-7.3.1", "exact profile required");
    ordinary(update);
    const auto source = root / "profiles" / std::string(profile) / "profile.json",
               qsource = source.parent_path() / "profile-qnx.json";
    const auto profile_pin = sha256_file(source), qnx_pin = sha256_file(qsource);
    const auto definition = read_private_json(source, 512 * 1024),
               qnx = read_private_json(qsource, 128 * 1024);
    const auto &recipe = definition.at("acquisition");
    const auto plan = segmented_acquisition_plan(definition, qnx, profile);
    const std::string archive_pin =
            a9    ? definition.at("ownerUpdate").at("sha256").get<std::string>()
            : old ? "ce6d1ffcd158c91383b61a44ea529bf54238512c520b436973fc0f6222047e42"
                  : "a1b0c8e26eec79dcd738c140070ec7f31792dbafd63006d8be113c2bbdec4220";
    need(matches(update, archive_pin,
                 a9    ? definition.at("ownerUpdate").at("bytes").get<std::uint64_t>()
                 : old ? 4226266936ULL
                       : 4137925521ULL),
         "owner archive missing");
    const auto archive_epoch = cache_file_identity(update);
    const auto inventory = pinned_zip_inventory(update, archive_pin);
    std::set<std::string> names;

    for (const auto &entry : inventory) {
        need(names.insert(entry.name).second, "duplicate ZIP member");
    }

    const auto destination = root / ".local/profiles" / std::string(profile);
    ordinary(destination);
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
    bool cached = true;

    for (const auto &input : inputs) {
        if (!matches(destination / "originals" / input.file, input.pin, input.bytes)) {
            cached = false;
        }
    }

    for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
        if (!matches(destination / "qnx" / it.key(), it.value().at("sha256"),
                     it.value().at("bytes"))) {
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

        return result;
    }

    const auto host = std::string(os::dependency_system);
    const auto codec_spec = project_file_json(root, "src/app/toolchain.json", 256 * 1024)
                                    .at("qnxUpdateCodec")
                                    .at(host);
    const auto codec = root / codec_spec.at("file").get<std::string>();
    ordinary(codec);
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
        import_android_update_image(update, archive_pin,
                                    Json{{image.at("file").get<std::string>(), image}}, codec,
                                    codec_spec.at("sha256").get<std::string>(), image_dir, reserve);
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
    }

    bool missing_qnx = false;

    for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
        if (!std::filesystem::exists(destination / "qnx" / it.key())) {
            missing_qnx = true;
        }
    }

    if (missing_qnx) {
        import_qnx_update(update, archive_pin, qnx.at("originals"), codec,
                          codec_spec.at("sha256").get<std::string>(), stage / "qnx", reserve);
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

        for (const auto &input : inputs) {
            need(matches(destination / "originals" / input.file, input.pin, input.bytes),
                 "published original missing");
        }

        for (auto it = qnx.at("originals").begin(); it != qnx.at("originals").end(); ++it) {
            need(matches(destination / "qnx" / it.key(), it.value().at("sha256"),
                         it.value().at("bytes")),
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

    return result;
}
} // namespace voyemu
