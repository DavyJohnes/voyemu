#pragma once
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "profile_identity.hpp"
#include <set>
#include <stdexcept>
namespace voyemu {
// Whether acquisition.qnxOriginals names exactly the pinned QNX originals of profile-qnx.json that
// the acquisition imports.
inline bool qnx_acquisition_originals_match(const Json &acquisition, const Json &originals) {
    const auto roles = acquisition.is_object() ? acquisition.value("qnxOriginals", Json()) : Json();

    if (!roles.is_array() || roles.empty() || !originals.is_object() ||
        roles.size() != originals.size()) {
        return false;
    }

    std::set<std::string> declared;

    for (const auto &role : roles) {
        if (!role.is_string() || !declared.insert(role.get<std::string>()).second ||
            !originals.contains(role.get<std::string>()) ||
            !originals.at(role.get<std::string>()).is_object() ||
            !sha256_text(originals.at(role.get<std::string>()).value("sha256", std::string{}))) {
            return false;
        }
    }

    return true;
}

inline std::filesystem::path qnx_originals_directory(const std::filesystem::path &root,
                                                     std::string_view profile) {
    const auto ordinary_qnx = [](const std::filesystem::path &path) {
        for (const auto &at : os::ancestors(path)) {
            if (std::filesystem::is_symlink(std::filesystem::symlink_status(at))) {
                throw VoyemuError("QNX_ORIGINALS_LINK", "linked input ancestor rejected");
            }
        }
    };
    const auto own = root / ".local/profiles" / std::string(profile) / "qnx";

    ordinary_qnx(own);

    if (std::filesystem::exists(own)) {
        return own;
    }

    const auto definition = root / "profiles" / std::string(profile) / "profile.json";

    if (!std::filesystem::is_regular_file(definition)) {
        return own;
    }

    ordinary_qnx(definition);
    const auto data = read_private_json(definition, 1 << 20);

    if (!data.contains("capture") || !data.at("capture").is_object() ||
        !data.at("capture").contains("referenceProfile")) {
        return own;
    }

    const auto reference = runtime_profile_reference(data, profile);

    if (!valid_profile_name(reference)) {
        throw VoyemuError("QNX_ORIGINALS_REFERENCE", "valid explicit reference profile required");
    }

    if (reference == profile) {
        return own;
    }

    const auto source_definition = root / "profiles" / reference / "profile-qnx.json",
               own_definition = definition.parent_path() / "profile-qnx.json";
    ordinary_qnx(source_definition);
    ordinary_qnx(own_definition);
    const auto config = read_private_json(own_definition, 128 * 1024);

    if (config != read_private_json(source_definition, 128 * 1024)) {
        throw VoyemuError("QNX_ORIGINALS_REFERENCE", "QNX definitions differ");
    }

    const auto source = root / ".local/profiles" / reference / "qnx";
    ordinary_qnx(source);

    for (auto it = config.at("originals").begin(); it != config.at("originals").end(); ++it) {
        if (it.value().at("file") != it.key() || it.key().empty() ||
            std::filesystem::path(it.key()).filename() != std::filesystem::path(it.key()) ||
            it.key() == "." || it.key() == "..") {
            throw VoyemuError("QNX_ORIGINALS_REFERENCE", "bounded original role required");
        }

        const auto file = source / it.key();
        ordinary_qnx(file);

        if (!std::filesystem::is_regular_file(file)) {
            throw VoyemuError("QNX_ORIGINALS_REFERENCE_MISSING",
                              "fetch the reference profile originals first");
        }
    }

    return source;
}
} // namespace voyemu
