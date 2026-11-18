#pragma once
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include "zip_archive.hpp"
#include <algorithm>
namespace voyemu {
inline constexpr Check passion_generated_need{"PASSION_GENERATED_HELPER"};

inline bool passion_generated_requested(const Json &definition, const std::string &reference) {
    if (definition.value("id", std::string{}) != "passion-11-russian-20260403-2.1.4.80" ||
        (reference != "patched:CarSettings" && reference != "patched:LY_Multimedia")) {
        return false;
    }

    const bool sound = reference == "patched:CarSettings";
    const auto &patches = definition.at("patches");
    const auto name = sound ? "CarSettings" : "LY_Multimedia";

    if (!patches.contains(name)) {
        return false;
    }

    const auto &steps = patches.at(name).at("steps");
    const Json step = sound ? "passion-api30-settings-sound.json"
                            : "passion-api30-usb-media-session.json";

    return std::find(steps.begin(), steps.end(), step) != steps.end();
}

inline void passion_generated_helper_artifact(const std::filesystem::path &root,
                                              const std::string &reference, const Json &artifact) {
    const bool sound = reference == "patched:CarSettings";

    passion_generated_need(sound || reference == "patched:LY_Multimedia",
                           "declared generated helper artifact required");
    passion_generated_need(artifact.is_object() && artifact.size() == 4,
                           "exact generated helper artifact descriptor required");
    const std::filesystem::path file = artifact.at("file").get<std::string>();
    passion_generated_need(file.is_absolute() && file == file.lexically_normal(),
                           "absolute ordinary artifact required");
    bool inside = false;

    for (const auto &path : os::ancestors(file)) {
        passion_generated_need(!std::filesystem::is_symlink(path),
                               "symlink artifact ancestor rejected");

        if (path == root / ".local") {
            inside = true;
            break;
        }
    }

    passion_generated_need(inside && std::filesystem::is_regular_file(file) &&
                                   std::filesystem::hard_link_count(file) == 1 &&
                                   std::filesystem::file_size(file) <= (256ULL << 20),
                           "owned bounded artifact required");
    const auto epoch = cache_file_identity(file);
    const auto pin = sha256_file(file);
    passion_generated_need(artifact.at("sha256") == pin &&
                                   artifact.at("bytes") == std::filesystem::file_size(file),
                           "generated helper archive differs");
    const auto &helper = artifact.at("generated_helper");
    const auto dex_pin = helper.at("sha256").get<std::string>();
    passion_generated_need(
            helper.is_object() && helper.size() == 6 &&
                    helper.at("contract") == (sound ? "passion-settings-sound-helper-v1"
                                                    : "passion-usb-media-session-helper-v1") &&
                    helper.at("artifact") == reference && helper.at("archive_sha256") == pin &&
                    helper.at("entry") == (sound ? "classes7.dex" : "classes4.dex") &&
                    sha256_text(dex_pin) && helper.at("bytes").is_number_unsigned() &&
                    helper.at("bytes").get<std::uint64_t>() > 0 &&
                    helper.at("bytes").get<std::uint64_t>() <= (64ULL << 20),
            "generated helper receipt contract differs");
    const auto dex = read_pinned_zip_entry(file, pin, helper.at("entry").get<std::string>(),
                                           dex_pin);
    passion_generated_need(dex.size() == helper.at("bytes").get<std::uint64_t>() &&
                                   cache_file_identity(file) == epoch,
                           "actual generated helper DEX differs");
}

inline void passion_generated_helper_graph(const std::filesystem::path &root,
                                           const Json &definition, const Json &artifacts) {
    for (const auto *reference : {"patched:CarSettings", "patched:LY_Multimedia"}) {
        if (passion_generated_requested(definition, reference)) {
            passion_generated_need(artifacts.contains(reference) &&
                                           artifacts.at(reference).contains("generated_helper"),
                                   "requested generated helper receipt required");
        }
    }

    for (auto at = artifacts.begin(); at != artifacts.end(); ++at) {
        if (at.value().contains("generated_helper")) {
            passion_generated_need(passion_generated_requested(definition, at.key()),
                                   "undeclared generated helper receipt rejected");
            passion_generated_helper_artifact(root, at.key(), at.value());
        }
    }
}
} // namespace voyemu
