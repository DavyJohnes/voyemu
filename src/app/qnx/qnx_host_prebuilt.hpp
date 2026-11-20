#pragma once
#include "check.hpp"
#include "download.hpp"
#include "error.hpp"
#include "model.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "zip_archive.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>
namespace voyemu {
// Pin {url, sha256} of the engine that the qnx-host workflow built from the recipe section of
// qnx-host.json and published, or null while that release is pending; deps on macOS and Linux then
// builds the engine from the recipe.
inline Json qnx_host_prebuilt_pin(const Json &catalog, const std::string &section) {
    if (!catalog.contains("prebuilt") || !catalog.at("prebuilt").contains(section)) {
        return nullptr;
    }

    return catalog.at("prebuilt").at(section);
}

// Why deps cannot install the engine of a host that users never build from its recipe, empty when
// it can: Windows installs only the published prebuilt engine or an engine already cached.
inline std::string qnx_host_asset_missing(const Json &prebuilt, bool cached) {
    if (!prebuilt.is_null() || cached) {
        return {};
    }

    return "the QNX engine of this host comes from its CI-built release asset, which is not "
           "published yet; machines run without the cluster";
}

// Source builds keep their recipe-only identity; published installs also name the selected asset.
// Receipts retain the complete digests so a matching path prefix never accepts another pin.
inline std::string qnx_host_cache_key(const std::string &component_sha256,
                                      const std::string &archive_sha256 = {}) {
    const Check require{"QNX_HOST"};

    require(sha256_text(component_sha256) &&
                    (archive_sha256.empty() || sha256_text(archive_sha256)),
            "engine cache identity differs");

    return component_sha256.substr(0, 12) +
           (archive_sha256.empty() ? "" : "-" + archive_sha256.substr(0, 12));
}

// Stable archive metadata names the companion without embedding this ZIP's hash or release tag.
inline std::string qnx_host_source_info(const std::string &host) {
    const Check require{"QNX_HOST"};

    require(host == "macos-arm64" || host == "linux-x64" || host == "windows-x64",
            "known source companion host required");
    const auto companion = "qnx-host-" + host + "-sources.tar";

    return "QNX host engine: " + host + "\nCorresponding sources: " + companion +
           "\nChecksum: " + companion + ".sha256\n\n" +
           "Download both files beside this binary ZIP from the same release.\n" +
           "Build instructions, the frozen recipe snapshot, patches and upstream source archives\n" +
           "are included in the source companion.\n";
}

// Downloads the pinned engine asset, a ZIP of the installed engine files, and extracts its regular
// members into the fresh directory destination. The caller checks the files against its recipe;
// version names the engine on the console.
inline void extract_prebuilt_qnx_host(const std::filesystem::path &root, const Json &pin,
                                      const std::filesystem::path &destination, bool offline,
                                      std::uint64_t reserve, const std::string &host,
                                      const std::string &version = {}) {
    const auto info = qnx_host_source_info(host);
    const auto sha = pin.at("sha256").get<std::string>();
    const DownloadPin download{pin.at("url").get<std::string>(),
                               sha,
                               512ULL << 20,
                               DownloadEncoding::raw,
                               {},
                               "qnx_host",
                               version};
    const auto archive = download_pinned(download, root / ".local/toolchains/downloads/qnx-host",
                                         offline, reserve)
                                 .file;

    progress::report("extract", download_names(download));

    if (std::filesystem::exists(std::filesystem::symlink_status(destination))) {
        throw VoyemuError("QNX_HOST", "prebuilt engine destination exists");
    }

    const Check require{"QNX_HOST"};
    require(read_pinned_zip_entry(archive, sha, "SOURCE-INFO.txt", info.size()) == info,
            "engine source information differs");
    ensure_private_directory(destination);
    std::vector<ZipTreeEntry> members;

    for (const auto &entry : pinned_zip_inventory(archive, sha)) {
        if (entry.directory || entry.name == "SOURCE-INFO.txt") {
            continue;
        }

        members.push_back({entry.name, destination / entry.name, entry.bytes});
        ensure_private_directory(members.back().output.parent_path());
    }

    if (members.empty()) {
        throw VoyemuError("QNX_HOST", "prebuilt engine asset is empty");
    }

    extract_pinned_zip_tree(archive, sha, members, reserve);
}

// The default macOS/Linux published-engine path. Source builds never enter this installer.
inline Json install_prebuilt_qnx_host(const std::filesystem::path &root, const Json &component,
                                      const Json &prebuilt, const std::string &host, bool offline,
                                      std::uint64_t reserve) {
    const Check require{"QNX_HOST"};

    require(root.is_absolute() && std::filesystem::canonical(root) == root,
            "canonical project root required");
    require(host == "macos" || host == "linux", "published POSIX engine host differs");
    const auto pin = sha256_bytes(component.dump());
    const auto archive = prebuilt.at("sha256").get<std::string>();
    const auto parent = root / ".local/toolchains";
    const auto target = parent /
                        ("qnx-host-" + host + "-11.1.2-" + qnx_host_cache_key(pin, archive));
    const auto verify = [&](const std::filesystem::path &directory, bool installed) {
        auto ancestor = root;

        for (const auto &part : directory.lexically_relative(root)) {
            require(part != "..", "engine directory escapes root");
            ancestor /= part;
            require(!std::filesystem::is_symlink(std::filesystem::symlink_status(ancestor)),
                    "linked installed engine directory rejected");
        }

        const auto binary = directory / "qemu-system-aarch64";
        require(std::filesystem::is_regular_file(std::filesystem::symlink_status(binary)) &&
                        std::filesystem::hard_link_count(binary) == 1,
                "ordinary installed engine required");
        const auto receipt = read_private_json(directory / "qnx-host.json");
        require(receipt.at("component_sha256") == pin && receipt.at("recipe") == component &&
                        receipt.at("executable_sha256") == sha256_file(binary) &&
                        (!installed || receipt.at("archive_sha256") == archive),
                "installed engine differs from receipt or selected archive");

        return receipt;
    };
    const auto result = [&](bool cached) {
        return Json{{"schema", 1},
                    {"ready", true},
                    {"cached", cached},
                    {"executable", (target / "qemu-system-aarch64").string()},
                    {"directory", target.string()},
                    {"version", component.at("version")},
                    {"guest_verified", false}};
    };

    if (std::filesystem::exists(target)) {
        verify(target, true);

        return result(true);
    }

    ensure_private_directory(parent);
    const auto lock = parent / "qnx-host.lock";
    require(std::filesystem::create_directory(lock), "installation already owned");
    struct Lock {
        std::filesystem::path path;

        ~Lock() {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } held{lock};
    const auto stage = root / ".local/build/tmp" / ("qnx-host-prebuilt-" + random_token());
    struct Stage {
        std::filesystem::path path;

        ~Stage() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } owned{stage};
    extract_prebuilt_qnx_host(root, prebuilt, stage, offline, reserve,
                              host == "macos" ? "macos-arm64" : "linux-x64",
                              component.value("version", std::string{}));
    auto receipt = verify(stage, false);
    std::filesystem::permissions(stage / "qemu-system-aarch64",
                                 std::filesystem::perms::owner_all |
                                         std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec,
                                 std::filesystem::perm_options::replace);
    receipt["archive_sha256"] = archive;
    write_private_json(stage / "qnx-host.json", receipt);
    require(!std::filesystem::exists(target), "destination appeared during installation");
    std::filesystem::rename(stage, target);

    return result(false);
}

// Writes the release asset of the installed engine in directory to the new file output: a stored
// ZIP of its regular files in name order with a fixed time and no owner, so equal engines give
// equal assets. SOURCE-INFO.txt is synthetic archive metadata. The installation receipt
// qnx-host.json is left out unless receipt is set.
inline void write_qnx_host_asset(const std::filesystem::path &directory,
                                 const std::filesystem::path &output, bool receipt,
                                 const std::string &host) {
    const auto info = qnx_host_source_info(host);
    std::vector<std::string> names;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_symlink() || (!entry.is_directory() && !entry.is_regular_file())) {
            throw VoyemuError("QNX_HOST", "ordinary engine files required");
        }

        const auto name = entry.path().lexically_relative(directory).generic_string();

        if (name == "SOURCE-INFO.txt") {
            throw VoyemuError("QNX_HOST", "source information is archive metadata");
        }

        if (entry.is_regular_file() && (receipt || name != "qnx-host.json")) {
            names.push_back(name);
        }
    }

    if (names.empty() || std::filesystem::exists(std::filesystem::symlink_status(output))) {
        throw VoyemuError("QNX_HOST", "engine files and a new asset path required");
    }

    names.push_back("SOURCE-INFO.txt");
    std::sort(names.begin(), names.end());
    std::string local, central;
    const auto word = [](std::string &out, std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; i++) {
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xff));
        }
    };

    for (const auto &name : names) {
        std::string bytes;

        if (name == "SOURCE-INFO.txt") {
            bytes = info;
        } else {
            std::ifstream in(directory / name, std::ios::binary);

            if (!in) {
                throw VoyemuError("QNX_HOST", "engine file unreadable: " + name);
            }

            bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }

        std::uint32_t crc = ~0U;

        for (const unsigned char c : bytes) {
            crc ^= c;

            for (unsigned i = 0; i < 8; i++) {
                crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
            }
        }

        crc = ~crc;

        if (local.size() + bytes.size() + name.size() + 30 > 0xffffffffULL) {
            throw VoyemuError("QNX_HOST", "engine asset exceeds 4 GiB");
        }

        // Version 2.0, no flags, stored, 1980-01-01 00:00, then CRC-32 and sizes.
        std::string fields;
        word(fields, 20, 2);
        word(fields, 0, 2);
        word(fields, 0, 2);
        word(fields, 0, 2);
        word(fields, 0x21, 2);
        word(fields, crc, 4);
        word(fields, bytes.size(), 4);
        word(fields, bytes.size(), 4);
        word(fields, name.size(), 2);
        word(fields, 0, 2);
        word(central, 0x02014b50, 4);
        word(central, 20, 2);
        central += fields;
        // No comment, disk 0, no attributes.
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 4);
        word(central, local.size(), 4);
        central += name;
        word(local, 0x04034b50, 4);
        local += fields + name + bytes;
    }

    std::string end;
    word(end, 0x06054b50, 4);
    word(end, 0, 4);
    word(end, names.size(), 2);
    word(end, names.size(), 2);
    word(end, central.size(), 4);
    word(end, local.size(), 4);
    word(end, 0, 2);
    std::ofstream out(output, std::ios::binary);
    out << local << central << end;
    out.close();

    if (!out) {
        throw VoyemuError("QNX_HOST", "engine asset write failed");
    }
}
} // namespace voyemu
