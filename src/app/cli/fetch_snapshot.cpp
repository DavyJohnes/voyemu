#include "fetch_snapshot.hpp"
#include "check.hpp"
#include "error.hpp"
#include "ext4_image.hpp"
#include "fetch.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "project_files.hpp"
#include "quickboot.hpp"
#include "zip_archive.hpp"
#include <map>
#include <set>
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

// The exact pinned snapshot recipe of the profile: one archive with raw ext4 images.
void snapshot_recipe(const Json &acquisition) {
    const auto &archive = acquisition.at("archive");
    const auto &images = acquisition.at("images");

    need(sha256_text(archive.at("sha256").get_ref<const std::string &>()) &&
                 archive.at("bytes").is_number_unsigned() &&
                 archive.at("file").get_ref<const std::string &>().find('/') == std::string::npos,
         "snapshot archive pin differs");
    need(!images.empty() && images.contains("system") && images.contains("vendor"),
         "snapshot image recipes required");

    for (auto it = images.begin(); it != images.end(); ++it) {
        const auto &image = it.value();
        need(it.key() == "system" || it.key() == "vendor" || it.key() == "vbmeta",
             "snapshot image role differs");
        need(image.at("file").get_ref<const std::string &>() == it.key() + ".img" &&
                     sha256_text(image.at("sha256").get_ref<const std::string &>()) &&
                     image.at("bytes").is_number_unsigned(),
             "snapshot image recipe differs");
    }
}

// Image and path of each original, exactly as declared by the profile.
std::vector<Input> snapshot_original_records(const Json &definition) {
    std::vector<Input> records;
    std::set<std::string> paths;
    std::uint64_t total = 0;

    for (const auto &input : original_input_plan(definition)) {
        const auto item = definition.at("original").at(input.name);
        const auto &acquisition = item.at("acquisition");
        const auto image = acquisition.at("image").get<std::string>();
        need(image == "system" || image == "vendor", "snapshot partition mismatch");
        const auto path = acquisition.at("path").get<std::string>();
        need(path.size() < 1024 && path.starts_with('/') &&
                     std::filesystem::path(path).lexically_normal().generic_string() == path &&
                     path.find("..") == std::string::npos &&
                     path.find_first_of("\\\r\n\t:") == std::string::npos,
             "invalid input path");
        const auto bytes = item.at("bytes").get<std::uint64_t>();
        need(bytes > 0 && bytes <= (512ULL << 20), "bounded original extent required");
        total += bytes;
        need(paths.insert(path).second && records.size() < 256 && total <= (2ULL << 30),
             "bounded input set required");
        records.push_back({input.file, input.sha256, image, path, bytes});
    }

    need(!records.empty(), "originals required");

    return records;
}
} // namespace

Json import_snapshot_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                    const std::filesystem::path &snapshot, std::uint64_t reserve) {
    need(valid_profile_name(profile) && root.is_absolute() &&
                 std::filesystem::canonical(root) == root,
         "canonical root and safe profile required");
    ordinary(snapshot);
    const auto source = root / "profiles" / std::string(profile) / "profile.json";
    const auto profile_pin = sha256_file(source);
    const auto definition = read_private_json(source, 512 * 1024);
    const auto &acquisition = definition.at("acquisition");
    need(acquisition.at("format") == "pinned-snapshot-raw-images" &&
                 acquisition.at("diff") == false,
         "snapshot acquisition recipe required");
    snapshot_recipe(acquisition);
    const auto archive_pin = acquisition.at("archive").at("sha256").get<std::string>();
    need(sha256_file(snapshot) == archive_pin &&
                 std::filesystem::file_size(snapshot) ==
                         acquisition.at("archive").at("bytes").get<std::uint64_t>(),
         "snapshot archive pin differs");
    const auto archive_epoch = cache_file_identity(snapshot);
    const auto records = snapshot_original_records(definition);
    const auto destination = root / ".local/profiles" / std::string(profile);

    (void)reserve;
    bool cached = true;

    for (const auto &input : records) {
        if (!matches(destination / "originals" / input.file, input.pin, input.bytes)) {
            cached = false;
        }
    }

    auto unchanged = [&] {
        need(sha256_file(source) == profile_pin && cache_file_identity(snapshot) == archive_epoch,
             "acquisition source changed");
    };
    Json result = {{"schema", 1},
                   {"profile", profile},
                   {"source", "partition-snapshot"},
                   {"archive_sha256", archive_pin},
                   {"cached", cached},
                   {"native", true},
                   {"guest_started", false},
                   {"frame_verified", false},
                   {"preparation_ready", false}};

    if (cached) {
        unchanged();

        return result;
    }

    const auto stage = root / ".local/build/tmp" / ("snapshot-owner-" + random_token());
    ensure_private_directory(stage);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{stage};
    ensure_private_directory(stage / "publish/originals");
    std::map<std::filesystem::path, Json> moved;

    for (const auto *name : {"system", "vendor"}) {
        std::vector<Input> missing;

        for (const auto &input : records) {
            if (input.image == name &&
                !matches(destination / "originals" / input.file, input.pin, input.bytes)) {
                missing.push_back(input);
            }
        }

        if (missing.empty()) {
            continue;
        }

        const auto &image = acquisition.at("images").at(name);
        const auto image_dir = stage / (std::string(name) + "-image");
        ensure_private_directory(image_dir);
        extract_pinned_zip_members(snapshot, archive_pin,
                                   {{image.at("file"), 0, image.at("bytes").get<std::uint64_t>()}},
                                   image_dir / image.at("file"),
                                   image.at("bytes").get<std::uint64_t>(), reserve);
        need(sha256_file(image_dir / image.at("file")) ==
                     image.at("sha256").get_ref<const std::string &>(),
             "snapshot image differs");
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
                paths.push_back(input.path);
                bytes += input.bytes;
            }

            const auto extracted = stage / ("extract-" + std::to_string(index++));
            extract_pinned_ext4_files(image_dir / image.at("file"),
                                      image.at("sha256").get<std::string>(), paths, extracted,
                                      reserve, std::string_view(name) == "vendor" ? "vendor" : "");

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
    }

    unchanged();

    try {
        for (const auto &input : records) {
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

        for (const auto &input : records) {
            need(matches(destination / "originals" / input.file, input.pin, input.bytes),
                 "published original missing");
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
