#pragma once
#include "check.hpp"
#include "error.hpp"
#include "model.hpp"
#include "process.hpp"
#include "qnx_host.hpp"
#include "qnx_process_ram.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
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
#ifdef _WIN32
    struct Disk {
        HANDLE handle = INVALID_HANDLE_VALUE;
        std::filesystem::path path;
        BY_HANDLE_FILE_INFORMATION identity{};
    };
    std::array<Disk, 2> disks_;

    void release() noexcept {
        for (auto &disk : disks_) {
            if (disk.handle != INVALID_HANDLE_VALUE) {
                CloseHandle(disk.handle);
                disk.handle = INVALID_HANDLE_VALUE;
            }
        }
    }

    static bool owned(HANDLE file) {
        HANDLE token = nullptr;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            return false;
        }

        alignas(TOKEN_USER) std::array<unsigned char, 256> user{};
        DWORD bytes = 0;
        const bool found = GetTokenInformation(token, TokenUser, user.data(),
                                               static_cast<DWORD>(user.size()), &bytes) != 0;
        CloseHandle(token);

        if (!found) {
            return false;
        }

        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const auto status = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                            &owner, nullptr, nullptr, nullptr, &descriptor);
        const auto sid = reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;
        const bool valid = status == ERROR_SUCCESS && owner && IsValidSid(owner) &&
                           IsValidSid(sid) && EqualSid(owner, sid);

        if (descriptor) {
            LocalFree(descriptor);
        }

        return valid;
    }

    static bool information(HANDLE file, BY_HANDLE_FILE_INFORMATION &value) {
        return file != INVALID_HANDLE_VALUE && GetFileType(file) == FILE_TYPE_DISK &&
               GetFileInformationByHandle(file, &value) && value.nNumberOfLinks == 1 &&
               !(value.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                                           FILE_ATTRIBUTE_READONLY)) &&
               owned(file);
    }

    static bool same(const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b) {
        return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber &&
               a.nFileIndexHigh == b.nFileIndexHigh && a.nFileIndexLow == b.nFileIndexLow &&
               a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
               CompareFileTime(&a.ftCreationTime, &b.ftCreationTime) == 0;
    }

    static bool current(const Disk &disk) {
        BY_HANDLE_FILE_INFORMATION retained{}, path{};

        if (!information(disk.handle, retained) || !same(disk.identity, retained)) {
            return false;
        }

        const HANDLE file = CreateFileW(disk.path.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                                        nullptr);
        const bool valid = information(file, path) && same(disk.identity, path);

        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }

        return valid;
    }
#else
    struct Disk {
        int fd = -1;
        std::filesystem::path path;
        struct stat identity{};
    };
    std::array<Disk, 2> disks_;

    void release() noexcept {
        for (auto &disk : disks_) {
            if (disk.fd >= 0) {
                ::close(disk.fd);
                disk.fd = -1;
            }
        }
    }
#endif
public:
    explicit QnxPersistentStorage(const Json &specification) {
        const auto &storage = specification.at("persistent_storage");

        if (!storage.is_object() || storage.size() != 3 || storage.value("schema", 0) != 1 ||
            !storage.contains("system") || !storage.contains("userdata")) {
            throw VoyemuError("QNX_PERSISTENCE_STORAGE", "exact storage schema required");
        }

#ifdef _WIN32
        const auto capability = specification.value("qnx_file_backend", Json::object());
        const auto engine = specification.at("qemu").at("executable").get<std::string>();
        const auto engine_sha = sha256_file(engine);
        const bool diagnostic = specification.contains("diagnostic_native");

        if (diagnostic ? !qnx_native_diagnostic_file_backend_supported(specification, engine_sha)
                       : !qnx_file_backend_supported(capability, engine_sha)) {
            throw VoyemuError("QNX_PERSISTENCE_UNSUPPORTED",
                              "verified file-backed engine capability differs");
        }

        try {
            const auto expected =
                    qnx_persistent_ram_launch(diagnostic ? QnxEngine::hvf : QnxEngine::tcg_file,
                                              storage.at("system").get<std::string>(),
                                              storage.at("userdata").get<std::string>(), true);
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
                disk.path = storage.at(role.first).get<std::string>();

                if (!disk.path.is_absolute() || disk.path.lexically_normal() != disk.path ||
                    std::filesystem::canonical(disk.path) != disk.path) {
                    throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                      "canonical absolute disk required");
                }

                disk.handle =
                        CreateFileW(disk.path.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

                if (!information(disk.handle, disk.identity) ||
                    (std::uint64_t(disk.identity.nFileSizeHigh) << 32 |
                     disk.identity.nFileSizeLow) != role.second) {
                    throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                      "exact private owned disk extent required");
                }
            }

            if (disks_[0].identity.dwVolumeSerialNumber ==
                        disks_[1].identity.dwVolumeSerialNumber &&
                disks_[0].identity.nFileIndexHigh == disks_[1].identity.nFileIndexHigh &&
                disks_[0].identity.nFileIndexLow == disks_[1].identity.nFileIndexLow) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE", "independent disks required");
            }
        } catch (...) {
            release();
            throw;
        }
#else
        try {
            const auto expected = qnx_persistent_ram_launch(
                    host_qnx_engine, storage.at("system").get<std::string>(),
                    storage.at("userdata").get<std::string>());
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
                disk.path = storage.at(role.first).get<std::string>();

                if (!disk.path.is_absolute() || disk.path.lexically_normal() != disk.path ||
                    std::filesystem::canonical(disk.path) != disk.path) {
                    throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                      "canonical absolute disk required");
                }

                disk.fd = ::open(disk.path.c_str(), O_RDWR | O_NOFOLLOW | O_CLOEXEC);

                if (disk.fd < 0 || ::fstat(disk.fd, &disk.identity) ||
                    !S_ISREG(disk.identity.st_mode) || disk.identity.st_uid != ::geteuid() ||
                    disk.identity.st_nlink != 1 || (disk.identity.st_mode & 0077) ||
                    static_cast<std::uint64_t>(disk.identity.st_size) != role.second) {
                    throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                      "exact private owned disk extent required");
                }
            }

            if (disks_[0].identity.st_dev == disks_[1].identity.st_dev &&
                disks_[0].identity.st_ino == disks_[1].identity.st_ino) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE", "independent disks required");
            }
        } catch (...) {
            release();
            throw;
        }
#endif
    }

    QnxPersistentStorage(const QnxPersistentStorage &) = delete;
    QnxPersistentStorage &operator=(const QnxPersistentStorage &) = delete;

    ~QnxPersistentStorage() {
        release();
    }

    void flush() {
#ifdef _WIN32
        for (auto &disk : disks_) {
            if (!current(disk)) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "disk replaced; owned runtime retained");
            }

            if (!FlushFileBuffers(disk.handle)) {
                throw VoyemuError("QNX_PERSISTENCE_FLUSH",
                                  "durable disk flush failed; owned runtime retained");
            }

            if (!current(disk)) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "disk replaced during flush; owned runtime retained");
            }
        }
#else
        for (auto &disk : disks_) {
            struct stat now{};

            if (::lstat(disk.path.c_str(), &now) || now.st_dev != disk.identity.st_dev ||
                now.st_ino != disk.identity.st_ino || now.st_size != disk.identity.st_size ||
                now.st_nlink != 1) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "disk replaced; owned runtime retained");
            }

            int result;

            do {
                result = ::fsync(disk.fd);
            } while (result && errno == EINTR);

#ifdef __APPLE__
            if (!result) {
                do {
                    result = ::fcntl(disk.fd, F_FULLFSYNC);
                } while (result && errno == EINTR);
            }
#endif

            if (result) {
                throw VoyemuError("QNX_PERSISTENCE_FLUSH",
                                  "durable disk flush failed; owned runtime retained");
            }

            if (::lstat(disk.path.c_str(), &now) || now.st_dev != disk.identity.st_dev ||
                now.st_ino != disk.identity.st_ino || now.st_size != disk.identity.st_size ||
                now.st_nlink != 1) {
                throw VoyemuError("QNX_PERSISTENCE_STORAGE",
                                  "disk replaced during flush; owned runtime retained");
            }
        }
#endif
    }
};
} // namespace voyemu
