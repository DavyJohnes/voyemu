#pragma once
#include "error.hpp"
#include "model.hpp"
#include "owner_lock.hpp"
#include "qnx_host_prebuilt.hpp"
#include "quickboot.hpp"
namespace voyemu {
// A retained, pinned Windows engine is host data, never part of a guest machine.
inline Json install_cached_windows_qnx_host(const std::filesystem::path &root,
                                            const Json &component, std::uint64_t reserve,
                                            const Json &prebuilt = nullptr, bool offline = false) {
    const auto require = [](bool ok, const char *message) {
        if (!ok) {
            throw VoyemuError("QNX_HOST", message);
        }
    };

    require(root.is_absolute() && std::filesystem::canonical(root) == root,
            "canonical Windows engine project root required");
    const auto &runtime = component.at("verified_runtime");
    const auto &files = runtime.at("files");
    const auto pin = sha256_bytes(component.dump());
    const auto archive = prebuilt.is_null() ? std::string{}
                                            : prebuilt.at("sha256").get<std::string>();
    const auto parent = root / ".local/toolchains",
               target = parent / ("qnx-host-windows-11.1.2-" + qnx_host_cache_key(pin, archive));
    const auto capability = runtime.at("qnx_file_backend");
    require(component.at("version") == "11.1.2" &&
                    capability == Json{{"schema", 1},
                                       {"version", "11.1.2"},
                                       {"engine_sha256",
                                        files.at("qemu-system-aarch64.exe").at("sha256")}},
            "Windows file backend recipe differs");
    const auto verify = [&](const std::filesystem::path &directory) {
        const auto relative_root = directory.lexically_relative(root);
        require(!relative_root.empty() && !relative_root.is_absolute(),
                "engine directory escapes root");
        auto ancestor = root;

        for (const auto &part : relative_root) {
            require(part != "..", "engine directory escapes root");
            ancestor /= part;
            require(!std::filesystem::is_symlink(std::filesystem::symlink_status(ancestor)),
                    "linked Windows engine directory rejected");
        }

        for (const auto &[relative, spec] : files.items()) {
            const std::filesystem::path name(relative);
            require(!name.empty() && !name.is_absolute() && name.lexically_normal() == name,
                    "runtime path is not relative");

            for (const auto &part : name) {
                require(part != "..", "runtime path escapes cache");
            }

            auto path = directory;

            for (const auto &part : name) {
                path /= part;
                require(!std::filesystem::is_symlink(std::filesystem::symlink_status(path)),
                        "linked Windows engine payload rejected");
            }

            require(std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) &&
                            std::filesystem::hard_link_count(path) == 1 &&
                            std::filesystem::file_size(path) ==
                                    spec.at("bytes").get<std::uint64_t>(),
                    "Windows engine payload extent differs");
            const auto identity = cache_file_identity(path);
            require(sha256_file(path) == spec.at("sha256").get<std::string>() &&
                            cache_file_identity(path) == identity,
                    "Windows engine payload SHA differs");
        }

        for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
            require(!entry.is_symlink(), "linked Windows engine entry rejected");

            if (entry.is_directory()) {
                continue;
            }

            const auto relative = entry.path().lexically_relative(directory).generic_string();
            require(files.contains(relative) || relative == "qnx-host.json",
                    "unexpected Windows engine payload");
        }
    };
    const auto result = [&](bool cached) {
        return Json{{"schema", 1},
                    {"ready", true},
                    {"cached", cached},
                    {"directory", target.string()},
                    {"executable", (target / "qemu-system-aarch64.exe").string()},
                    {"version", "11.1.2"},
                    {"qnx_file_backend", capability},
                    {"guest_verified", false}};
    };

    if (std::filesystem::exists(target)) {
        const auto receipt = read_private_json(target / "qnx-host.json", 128 * 1024);
        require(receipt.at("component_sha256") == pin && receipt.at("recipe") == component &&
                        receipt.at("qnx_file_backend") == capability &&
                        receipt.value("archive_sha256", std::string{}) == archive,
                "installed Windows engine receipt differs");
        verify(target);

        return result(true);
    }

    const auto cache = root / ".local/toolchains/downloads/qnx-host/windows-file-backend-11.1.2";

    if (prebuilt.is_null()) {
        verify(cache);
    }

    require(std::filesystem::space(root).available >= reserve + (64ULL << 20),
            "insufficient Windows engine install reserve");
    ensure_private_directory(parent);
    const auto lock = owner_lock(parent / "qnx-host.lock");
    require(lock->held(), "installation already owned");
    const auto stage = root / ".local/build/tmp" / ("qnx-host-windows-" + random_token());
    struct Stage {
        std::filesystem::path path;

        ~Stage() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } owned{stage};

    if (prebuilt.is_null()) {
        ensure_private_directory(stage);

        for (const auto &[relative, spec] : files.items()) {
            (void)spec;
            const auto destination = stage / relative;
            ensure_private_directory(destination.parent_path());
            require(std::filesystem::copy_file(cache / relative, destination),
                    "exclusive Windows engine copy failed");
        }
    } else {
        extract_prebuilt_qnx_host(root, prebuilt, stage, offline, reserve, "windows-x64");
    }

    verify(stage);
    Json receipt{{"schema", 1},
                 {"component_sha256", pin},
                 {"recipe", component},
                 {"qnx_file_backend", capability}};

    if (!archive.empty()) {
        receipt["archive_sha256"] = archive;
    }

    write_private_json(stage / "qnx-host.json", receipt);
    require(!std::filesystem::exists(target), "Windows engine destination appeared");
    std::filesystem::rename(stage, target);

    return result(false);
}
} // namespace voyemu
