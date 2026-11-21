#pragma once
#include "check.hpp"
#include "error.hpp"
#include "model.hpp"
#include "os/durable_disk.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "qnx_host.hpp"
#include "qnx_process_ram.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace voyemu {
inline bool qnx_file_backend_supported(const Json &capability, std::string_view engine_sha) {
    return capability.is_object() && capability.size() == 3 && capability.contains("schema") &&
           capability.at("schema").is_number_integer() && capability.at("schema") == 1 &&
           capability.contains("version") && capability.at("version").is_string() &&
           capability.at("version") ==
                   qnx_host_catalog().at("windows_file_backend").at("version") &&
           engine_sha.size() == 64 && capability.contains("engine_sha256") &&
           capability.at("engine_sha256").is_string() &&
           capability.at("engine_sha256") == engine_sha;
}

inline bool qnx_native_diagnostic_file_backend_supported(const Json &specification,
                                                         std::string_view engine_sha) {
    try {
        const auto &native = specification.at("diagnostic_native");
        const auto engine = specification.at("qemu").at("executable").get<std::string>();

        if (!native.is_object() || native.size() != 6 || !native.at("schema").is_number_integer() ||
            native.at("schema") != 1 || native.at("persistent_backend") != "macos-file-numa-v1" ||
            native.at("executable") != engine || native.at("engine_sha256") != engine_sha ||
            !sha256_text(engine_sha) || native.at("launcher").get<std::string>().empty()) {
            return false;
        }

        const auto &qmp = specification.at("qmp");

        if (!qmp.is_object() || qmp.at("host") != "127.0.0.1" ||
            !qmp.at("port").is_number_integer() || !native.at("qmp_port").is_number_integer() ||
            qmp.at("port").get<std::int64_t>() < 1024 ||
            qmp.at("port").get<std::int64_t>() > 65535 || native.at("qmp_port") != qmp.at("port")) {
            return false;
        }

        unsigned matches = 0;

        for (const auto &pin : specification.at("pins")) {
            if (pin.at("path") == engine) {
                if (pin.at("sha256") != engine_sha) {
                    return false;
                }

                ++matches;
            }
        }

        if (matches != 1) {
            return false;
        }

        std::ifstream image(engine, std::ios::binary);
        unsigned char header[8]{};
        image.read(reinterpret_cast<char *>(header), 8);

        return image.gcount() == 8 && header[0] == 0xcf && header[1] == 0xfa && header[2] == 0xed &&
               header[3] == 0xfe && header[4] == 12 && header[5] == 0 && header[6] == 0 &&
               header[7] == 1;
    } catch (const std::exception &) {
        return false;
    }
}

class QnxPersistentStorage {
    friend struct SessionPersistenceFixture;
    std::array<os::DurableDisk, 2> disks_;

public:
    explicit QnxPersistentStorage(const Json &specification) {
        const auto &storage = specification.at("persistent_storage");

        if (!storage.is_object() || storage.size() != 3 || storage.value("schema", 0) != 1 ||
            !storage.contains("system") || !storage.contains("userdata")) {
            throw VoyemuError("QNX_PERSISTENCE_STORAGE", "exact storage schema required");
        }

        auto engine = host_qnx_engine;

        // Windows keeps the guest RAM in a file only with the verified file backend of the engine.
        if (os::host_windows) {
            const auto capability = specification.value("qnx_file_backend", Json::object());
            const auto executable = specification.at("qemu").at("executable").get<std::string>();
            const auto engine_sha = sha256_file(executable);
            const bool diagnostic = specification.contains("diagnostic_native");

            if (diagnostic
                        ? !qnx_native_diagnostic_file_backend_supported(specification, engine_sha)
                        : !qnx_file_backend_supported(capability, engine_sha)) {
                throw VoyemuError("QNX_PERSISTENCE_UNSUPPORTED",
                                  "verified file-backed engine capability differs");
            }

            engine = diagnostic ? QnxEngine::hvf : QnxEngine::tcg_file;
        }

        const auto expected = qnx_persistent_ram_launch(
                engine, storage.at("system").get<std::string>(),
                storage.at("userdata").get<std::string>(), os::host_windows);
        const auto args = specification.at("qemu").at("args").get<std::vector<std::string>>();

        for (std::size_t i = 0; i < expected.options.size(); i += 2) {
            unsigned matches = 0;

            for (std::size_t j = 0; j + 1 < args.size(); ++j) {
                if (args[j] == expected.options[i] && args[j + 1] == expected.options[i + 1]) {
                    ++matches;
                }
            }

            if (matches != 1) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "exact persistent NUMA mapping required");
            }
        }

        std::size_t index = 0;

        for (const auto &role :
             {std::pair{"system", 1025ULL << 20}, std::pair{"userdata", 32ULL << 20}}) {
            auto &disk = disks_[index++];
            const std::filesystem::path path = storage.at(role.first).get<std::string>();

            if (!path.is_absolute() || path.lexically_normal() != path ||
                std::filesystem::canonical(path) != path) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE", "canonical absolute disk required");
            }

            if (!disk.open(path, role.second)) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "exact private owned disk extent required");
            }
        }

        if (disks_[0].same_file(disks_[1])) {
            throw VoyemuError("QNX_PERSISTENCE_STORAGE", "independent disks required");
        }
    }

    QnxPersistentStorage(const QnxPersistentStorage &) = delete;
    QnxPersistentStorage &operator=(const QnxPersistentStorage &) = delete;

    void flush() {
        for (auto &disk : disks_) {
            if (!disk.current()) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "disk replaced; owned runtime retained");
            }

            if (!disk.flush()) {
                throw VoyemuError("QNX_PERSISTENCE_FLUSH",
                                  "durable disk flush failed; owned runtime retained");
            }

            if (!disk.current()) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "disk replaced during flush; owned runtime retained");
            }
        }
    }
};
} // namespace voyemu
