#pragma once
#include "check.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "os/process.hpp"
#include "process.hpp"
#include "profile_runtime.hpp"
#include <array>
#include <climits>
#include <memory>
#ifndef _WIN32
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#include "private_state_security.hpp"
#endif
namespace voyemu {
// Keep the supervisor lease through the derived configuration refresh; stale records remain
// evidence.
class InactiveProfileLease {
    Json active_, runtime_;
#ifndef _WIN32
    struct File {
        int fd = -1;

        ~File() {
            if (fd >= 0) {
                ::close(fd);
            }
        }
    } lock_;

    static void require(bool yes) {
        if (!yes) {
            throw VoyemuError("PORTABLE_RUNTIME", "inactive profile identity is unverified");
        }
    }

    static void ordinary(const std::filesystem::path &path, bool directory = false) {
        struct stat s{};

        require(path.is_absolute() && path.lexically_normal() == path &&
                std::filesystem::canonical(path) == path && ::lstat(path.c_str(), &s) == 0 &&
                s.st_uid == ::geteuid() && !(s.st_mode & 0077) &&
                (directory ? S_ISDIR(s.st_mode) : (S_ISREG(s.st_mode) && s.st_nlink == 1)));
    }

    static void absent(const Json &pid) {
        require(pid.is_number_integer());
        const auto value = pid.get<std::int64_t>();
        require(value == 0 || (value > 1 && value <= INT_MAX &&
                               ::kill(static_cast<pid_t>(value), 0) < 0 && errno == ESRCH));
    }

    static void unused(const std::filesystem::path &path) {
        ordinary(path);
        require(os::file_use(path) == os::FileUse::unused);
    }
#else
    struct Lock {
        HANDLE value = INVALID_HANDLE_VALUE;
        std::uint64_t volume = 0, index = 0;

        ~Lock() {
            if (value != INVALID_HANDLE_VALUE) {
                CloseHandle(value);
            }
        }
    } lock_;
    struct Handle {
        HANDLE value = INVALID_HANDLE_VALUE;

        ~Handle() {
            if (value != INVALID_HANDLE_VALUE) {
                CloseHandle(value);
            }
        }
    };

    static void require(bool yes) {
        if (!yes) {
            throw VoyemuError("PORTABLE_RUNTIME", "inactive profile identity is unverified");
        }
    }

    static bool trusted(PSID sid) {
        Handle token;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
            return false;
        }

        DWORD bytes = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);

        if (!bytes || bytes > 65536) {
            return false;
        }

        std::vector<unsigned char> user(bytes);

        if (!GetTokenInformation(token.value, TokenUser, user.data(), bytes, &bytes)) {
            return false;
        }

        BYTE system[SECURITY_MAX_SID_SIZE]{};
        DWORD system_size = sizeof system;
        BYTE administrators[SECURITY_MAX_SID_SIZE]{};
        DWORD administrators_size = sizeof administrators;

        if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &system_size) ||
            !CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators,
                                &administrators_size)) {
            return false;
        }

        return EqualSid(sid, reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid) ||
               EqualSid(sid, system) || EqualSid(sid, administrators);
    }

    static bool owned(PSID sid) {
        Handle token;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
            return false;
        }

        DWORD bytes = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &bytes);

        if (!bytes || bytes > 65536) {
            return false;
        }

        std::vector<unsigned char> user(bytes);

        if (!GetTokenInformation(token.value, TokenUser, user.data(), bytes, &bytes)) {
            return false;
        }

        return EqualSid(sid, reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid);
    }

    // The DACL analogue of the macOS 0077 check: only the current user, SYSTEM and Administrators
    // may hold any access.
    static void object(HANDLE file, bool directory) {
        BY_HANDLE_FILE_INFORMATION info{};

        require(GetFileType(file) == FILE_TYPE_DISK && GetFileInformationByHandle(file, &info) &&
                !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
                (directory || info.nNumberOfLinks == 1));
        PSID owner = nullptr;
        PACL acl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        require(GetSecurityInfo(file, SE_FILE_OBJECT,
                                OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner,
                                nullptr, &acl, nullptr, &descriptor) == ERROR_SUCCESS);
        struct Release {
            PSECURITY_DESCRIPTOR value;

            ~Release() {
                if (value) {
                    LocalFree(value);
                }
            }
        } release{descriptor};
        require(owner && acl && owned(owner));

        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void *raw = nullptr;
            require(GetAce(acl, i, &raw));
            const auto *header = static_cast<const ACE_HEADER *>(raw);

            if (header->AceFlags & INHERIT_ONLY_ACE) {
                continue;
            }

            if (header->AceType == ACCESS_DENIED_ACE_TYPE) {
                continue;
            }

            require(header->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                    trusted(const_cast<DWORD *>(
                            &static_cast<const ACCESS_ALLOWED_ACE *>(raw)->SidStart)));
        }
    }

    static void components(const std::filesystem::path &path) {
        require(path.is_absolute() && path.lexically_normal() == path);

        for (const auto &at : os::ancestors(path)) {
            const auto attributes = GetFileAttributesW(at.c_str());
            require(attributes != INVALID_FILE_ATTRIBUTES &&
                    !(attributes & FILE_ATTRIBUTE_REPARSE_POINT));
        }
    }

    static void ordinary(const std::filesystem::path &path, bool directory = false) {
        components(path);
        Handle handle;
        handle.value = CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                                           (directory ? FILE_FLAG_BACKUP_SEMANTICS : 0),
                                   nullptr);
        require(handle.value != INVALID_HANDLE_VALUE);
        object(handle.value, directory);
    }

    static void absent(const Json &pid) {
        require(pid.is_number_integer());
        const auto value = pid.get<std::int64_t>();
        require(value >= 0 && value <= UINT32_MAX);
        Handle process;
        process.value = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                    static_cast<DWORD>(value));

        if (!process.value) {
            require(GetLastError() == ERROR_INVALID_PARAMETER);

            return;
        }

        require(WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0);
    }

    // Exclusive open is the Windows holder probe: success proves no foreign handle, sharing
    // violation proves a holder.
    static void unused(const std::filesystem::path &path) {
        ordinary(path);
        Handle handle;
        handle.value = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        require(handle.value != INVALID_HANDLE_VALUE);
    }

    static bool endpoint_absent(const std::filesystem::path &endpoint) {
        if (WaitNamedPipeW(endpoint.c_str(), 1)) {
            return false;
        }

        return GetLastError() == ERROR_FILE_NOT_FOUND;
    }
#endif
public:
    InactiveProfileLease(const std::filesystem::path &root, std::string_view profile,
                         const MachineIdentity &machine, const Json &active, const Json &runtime,
                         const Json &storage)
        : active_(active), runtime_(runtime) {
        require(machine.valid_for(profile));
#ifndef _WIN32
        const auto id = active.at("session_id").get<std::string>();
        require(hex_text(id, 32) && active.at("schema") == 1 &&
                active.at("profile") == std::string(profile));
        const auto directory = native_session_directory(root, profile, machine, id);
        ordinary(directory, true);
        require(active.at("directory") == directory.string() && runtime.at("session_id") == id &&
                runtime.at("endpoint") == active.at("endpoint"));
        const auto owner_path = directory / "session-owner.json";
        ordinary(owner_path);
        const auto owner = read_private_json(owner_path, 16384);
        require(owner.at("schema") == 1 && owner.at("session_id") == id &&
                owner.at("directory") == directory.string() &&
                owner.at("origin") == "voyemu-native-supervisor");
        const auto active_dir = native_current_active_directory(root, profile, machine);
        ordinary(active_dir, true);
        const auto lock_path = active_dir / (std::string(profile) + ".supervisor.lock");
        ordinary(lock_path);
        lock_.fd = ::open(lock_path.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW);
        require(lock_.fd >= 0);
        struct stat held{}, path{};
        require(::fstat(lock_.fd, &held) == 0 && ::lstat(lock_path.c_str(), &path) == 0 &&
                S_ISREG(held.st_mode) && held.st_uid == ::geteuid() && held.st_nlink == 1 &&
                !(held.st_mode & 0077) && held.st_dev == path.st_dev &&
                held.st_ino == path.st_ino && ::flock(lock_.fd, LOCK_EX | LOCK_NB) == 0);
        const auto verify = [&] {
            ordinary(lock_path);
            struct stat lock_now{}, held_now{};
            require(::lstat(lock_path.c_str(), &lock_now) == 0 &&
                    ::fstat(lock_.fd, &held_now) == 0 && lock_now.st_dev == held.st_dev &&
                    lock_now.st_ino == held.st_ino && held_now.st_dev == held.st_dev &&
                    held_now.st_ino == held.st_ino && S_ISREG(held_now.st_mode) &&
                    held_now.st_uid == ::geteuid() && held_now.st_nlink == 1 &&
                    !(held_now.st_mode & 0077));
            const auto endpoint = std::filesystem::path(active.at("endpoint").get<std::string>());
            require(endpoint.is_absolute() && endpoint.lexically_normal() == endpoint &&
                    endpoint.filename() == id + ".sock");
            ordinary(endpoint.parent_path(), true);
            struct stat socket{};
            errno = 0;
            require(::lstat(endpoint.c_str(), &socket) < 0 && errno == ENOENT);
            absent(owner.at("creator_pid"));

            if (owner.contains("qnx_pid")) {
                absent(owner.at("qnx_pid"));
            }

            for (const char *key : {"android_pid", "qnx_pid"}) {
                if (runtime.contains(key)) {
                    absent(runtime.at(key));
                }
            }

            if (owner.contains("android_processes")) {
                const auto &android = owner.at("android_processes");

                for (const char *key : {"sdk_pid", "adb_pid", "frontend_pid"}) {
                    if (android.contains(key)) {
                        absent(android.at(key));
                    }
                }

                for (const char *key : {"console_port", "adb_server_port", "grpc_port"}) {
                    if (android.contains(key)) {
                        const auto port = android.at(key).get<unsigned>();
                        require(port >= 1024 && port < 65535 && tcp_port_free(port));

                        if (std::string_view(key) == "console_port") {
                            require(tcp_port_free(port + 1));
                        }
                    }
                }
            }

            const auto ram = directory / "qnx-ram.bin";

            if (std::filesystem::exists(ram)) {
                unused(ram);
            }

            if (storage.is_object() && !storage.empty()) {
                for (const char *key : {"system", "userdata"}) {
                    unused(storage.at(key).get<std::string>());
                }
            }

            require(read_private_json(active_dir / (std::string(profile) + ".json"), 4096) ==
                            active &&
                    read_private_json(directory / "runtime.json", 1024 * 1024) == runtime &&
                    read_private_json(owner_path, 16384) == owner);
        };
        verify();
        verify();
#else
        const auto id = active.at("session_id").get<std::string>();
        require(hex_text(id, 32) && active.at("schema") == 1 &&
                active.at("profile") == std::string(profile));
        const auto directory = native_session_directory(root, profile, machine, id);
        ordinary(directory, true);
        require(same_directory_tail(
                        std::filesystem::path(active.at("directory").get<std::string>()), directory,
                        3) &&
                runtime.at("session_id") == id && runtime.at("endpoint") == active.at("endpoint"));
        const auto owner_path = directory / "session-owner.json";
        ordinary(owner_path);
        const auto owner = read_private_json(owner_path, 16384);
        require(owner.at("schema") == 1 && owner.at("session_id") == id &&
                same_directory_tail(std::filesystem::path(owner.at("directory").get<std::string>()),
                                    directory, 3) &&
                owner.at("origin") == "voyemu-native-supervisor");
        const auto active_dir = native_current_active_directory(root, profile, machine);
        ordinary(active_dir, true);
        const auto lock_path = active_dir / (std::string(profile) + ".supervisor.lock");
        components(lock_path);
        lock_.value = detail::open_private_state_lock(
                lock_path, GENERIC_READ | GENERIC_WRITE | READ_CONTROL, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
        require(lock_.value != INVALID_HANDLE_VALUE);
        BY_HANDLE_FILE_INFORMATION held{};
        require(GetFileType(lock_.value) == FILE_TYPE_DISK &&
                GetFileInformationByHandle(lock_.value, &held) && held.nNumberOfLinks == 1);
        lock_.volume = held.dwVolumeSerialNumber;
        lock_.index = (std::uint64_t(held.nFileIndexHigh) << 32) | held.nFileIndexLow;
        object(lock_.value, false);
        const auto verify = [&] {
            components(lock_path);
            BY_HANDLE_FILE_INFORMATION now{};
            require(GetFileType(lock_.value) == FILE_TYPE_DISK &&
                    GetFileInformationByHandle(lock_.value, &now) && now.nNumberOfLinks == 1 &&
                    now.dwVolumeSerialNumber == lock_.volume &&
                    ((std::uint64_t(now.nFileIndexHigh) << 32) | now.nFileIndexLow) == lock_.index);
            object(lock_.value, false);
            const auto endpoint = std::filesystem::path(active_.at("endpoint").get<std::string>());
            require(endpoint.string() == "\\\\.\\pipe\\voyemu-" + id);
            require(endpoint_absent(endpoint));
            absent(owner.at("creator_pid"));

            if (owner.contains("qnx_pid")) {
                absent(owner.at("qnx_pid"));
            }

            for (const char *key : {"android_pid", "qnx_pid"}) {
                if (runtime_.contains(key)) {
                    absent(runtime_.at(key));
                }
            }

            if (owner.contains("android_processes")) {
                const auto &android = owner.at("android_processes");

                for (const char *key : {"sdk_pid", "adb_pid", "frontend_pid"}) {
                    if (android.contains(key)) {
                        absent(android.at(key));
                    }
                }

                for (const char *key : {"console_port", "adb_server_port", "grpc_port"}) {
                    if (android.contains(key)) {
                        const auto port = android.at(key).get<unsigned>();
                        require(port >= 1024 && port < 65535 && tcp_port_free(port));

                        if (std::string_view(key) == "console_port") {
                            require(tcp_port_free(port + 1));
                        }
                    }
                }
            }

            const auto ram = directory / "qnx-ram.bin";

            if (std::filesystem::exists(ram)) {
                unused(ram);
            }

            if (storage.is_object() && !storage.empty()) {
                for (const char *key : {"system", "userdata"}) {
                    unused(storage.at(key).get<std::string>());
                }
            }

            require(read_private_json(active_dir / (std::string(profile) + ".json"), 4096) ==
                            active_ &&
                    read_private_json(directory / "runtime.json", 1024 * 1024) == runtime_ &&
                    read_private_json(owner_path, 16384) == owner);
        };
        verify();
        verify();
#endif
    }

    bool matches(const Json &active, const Json &runtime) const {
        return active == active_ && runtime == runtime_;
    }
};
} // namespace voyemu
