#pragma once
#include "error.hpp"
#include "os/fs.hpp"
#include "profile_identity.hpp"
#include <stdexcept>
namespace voyemu {
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
