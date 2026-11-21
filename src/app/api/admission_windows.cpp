#include "../platform/wine_native_backend_windows.hpp"
#include "admission.hpp"
#include "error.hpp"
#include "process.hpp"
#include <cstdlib>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>

#include <memory>
#include <vector>

namespace voyemu::api {
namespace {
[[noreturn]] void fail() {
    throw VoyemuError("ADMISSION_STATE", "private receipt unavailable");
}

constexpr std::size_t payload_limit = 128 * 1024;
constexpr std::size_t record_limit = 8 * 1024 * 1024;

std::string digest(const std::string &text) {
    return sha256_bytes(text);
}

std::string key(const std::string &scope, const std::string &id) {
    if (scope.empty() || id.empty() || scope.size() > 4096 || id.size() > 4096) {
        throw VoyemuError("INVALID_REQUEST", "invalid admission key");
    }

    return digest(Json::array({scope, id}).dump());
}

struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;

    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}

    ~Handle() {
        if (value != INVALID_HANDLE_VALUE) {
            CloseHandle(value);
        }
    }

    Handle(const Handle &) = delete;
};
struct Identity {
    std::vector<std::byte> user;
    BYTE system[SECURITY_MAX_SID_SIZE]{}, administrators[SECURITY_MAX_SID_SIZE]{};

    Identity() {
        Handle token;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token.value)) {
            fail();
        }

        DWORD size = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &size);

        if (!size) {
            fail();
        }

        user.resize(size);

        if (!GetTokenInformation(token.value, TokenUser, user.data(), size, &size)) {
            fail();
        }

        size = sizeof(system);

        if (!CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &size)) {
            fail();
        }

        size = sizeof(administrators);

        if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators, &size)) {
            fail();
        }
    }

    PSID sid() {
        return reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;
    }

    bool trusted(PSID value) {
        return EqualSid(value, sid()) || EqualSid(value, system) || EqualSid(value, administrators);
    }
};

bool is_system_volume_root(const BY_HANDLE_FILE_INFORMATION &info) {
    wchar_t windows[MAX_PATH]{};
    const auto size = GetSystemWindowsDirectoryW(windows, MAX_PATH);

    if (!size || size >= MAX_PATH) {
        return false;
    }

    wchar_t root[MAX_PATH]{};

    if (!GetVolumePathNameW(windows, root, MAX_PATH)) {
        return false;
    }

    const Handle system_root(CreateFileW(
            root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    BY_HANDLE_FILE_INFORMATION system_info{};

    return system_root.value != INVALID_HANDLE_VALUE &&
           GetFileInformationByHandle(system_root.value, &system_info) &&
           info.dwVolumeSerialNumber == system_info.dwVolumeSerialNumber &&
           info.nFileIndexHigh == system_info.nFileIndexHigh &&
           info.nFileIndexLow == system_info.nFileIndexLow;
}

void check(HANDLE file, bool directory = false, bool parent = false,
           const std::filesystem::path *volume = nullptr, bool volume_root = false) {
    BY_HANDLE_FILE_INFORMATION info{};

    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
        bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory ||
        (!directory && info.nNumberOfLinks != 1)) {
        throw VoyemuError("ADMISSION_STATE", "ordinary non-reparse storage required");
    }

    // Wine reports the anonymous logon owner for every node of the DOS volume mapped to the Unix
    // root, so on a Wine host that whole volume is exempt; ordinary non-reparse storage,
    // single-link files and every other volume keep their owner and DACL checks.
    if (volume && wine_native_host() && wine_native_unix_path(*volume) == "/") {
        return;
    }

    Identity identity;
    PSID owner = nullptr;
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;

    if (GetSecurityInfo(file, SE_FILE_OBJECT,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                        &acl, nullptr, &descriptor) != ERROR_SUCCESS) {
        fail();
    }

    struct Release {
        PSECURITY_DESCRIPTOR value;

        ~Release() {
            if (value) {
                LocalFree(value);
            }
        }
    } release{descriptor};

    const bool system_root = volume_root && is_system_volume_root(info);
    PSID installer = nullptr;

    if (system_root &&
        !ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464",
                                &installer)) {
        fail();
    }

    const Release installer_release{installer};
    const bool installer_owns_root = owner && installer && EqualSid(owner, installer);

    if (!owner || !acl ||
        (parent ? !identity.trusted(owner) && !installer_owns_root
                : !EqualSid(owner, identity.sid()))) {
        throw VoyemuError("ADMISSION_STATE", "storage owner or DACL differs");
    }

    for (DWORD i = 0; i < acl->AceCount; ++i) {
        void *raw = nullptr;

        if (!GetAce(acl, i, &raw)) {
            fail();
        }

        const auto *header = static_cast<const ACE_HEADER *>(raw);

        if (header->AceFlags & INHERIT_ONLY_ACE) {
            continue;
        }

        if (header->AceType == ACCESS_DENIED_ACE_TYPE) {
            continue;
        }

        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE) {
            fail();
        }

        const auto *ace = static_cast<const ACCESS_ALLOWED_ACE *>(raw);
        auto mutation = GENERIC_ALL | GENERIC_WRITE | WRITE_DAC | WRITE_OWNER | DELETE |
                        FILE_DELETE_CHILD | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
                        FILE_WRITE_ATTRIBUTES;

        // Windows allows ordinary users to create subdirectories at the system volume
        // root. Every descendant is opened and checked separately; replacement, deletion
        // and all other foreign mutations remain forbidden at the root.
        if (system_root) {
            mutation &= ~FILE_ADD_SUBDIRECTORY;
        }

        const auto trustee = const_cast<DWORD *>(&ace->SidStart);

        if (!identity.trusted(trustee) && !(installer && EqualSid(trustee, installer)) &&
            (!parent || (ace->Mask & mutation))) {
            throw VoyemuError("ADMISSION_STATE", parent ? "writable foreign parent ACL"
                                                        : "foreign access in private receipt ACL");
        }
    }
}

struct PrivateSecurity {
    Identity identity;
    PACL acl = nullptr;
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};

    PrivateSecurity() {
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = FILE_ALL_ACCESS;
        access.grfAccessMode = SET_ACCESS;
        access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_USER;
        access.Trustee.ptstrName = static_cast<LPWSTR>(identity.sid());

        if (SetEntriesInAclW(1, &access, nullptr, &acl) != ERROR_SUCCESS) {
            fail();
        }

        if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorOwner(&descriptor, identity.sid(), FALSE) ||
            !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) ||
            !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED)) {
            LocalFree(acl);
            acl = nullptr;
            fail();
        }
    }

    ~PrivateSecurity() {
        if (acl) {
            LocalFree(acl);
        }
    }
};
struct Directory {
    std::filesystem::path path;
    std::vector<std::unique_ptr<Handle>> ancestry;

    Directory(const std::filesystem::path &directory, bool create) : path(directory) {
        if (!path.is_absolute() || path.filename().empty()) {
            fail();
        }

        PrivateSecurity security;
        auto current = path.root_path();
        const std::filesystem::path drive_root = path.root_path();
        auto claim = [&](bool leaf, bool volume_root = false) {
            auto file = std::make_unique<Handle>(CreateFileW(
                    current.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | (volume_root ? 0 : FILE_FLAG_OPEN_REPARSE_POINT),
                    nullptr));

            if (file->value == INVALID_HANDLE_VALUE) {
                fail();
            }

            check(file->value, true, !leaf, &drive_root, volume_root);
            ancestry.push_back(std::move(file));
        };
        // Resolve the DOS drive mapping once, then reject reparses in every path component.
        claim(false, true);

        for (const auto &component : path.relative_path()) {
            current /= component;

            if (create && !CreateDirectoryW(current.c_str(), &security.attributes) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                fail();
            }

            claim(current == path);
        }
    }
};

std::string read(HANDLE file) {
    check(file);
    LARGE_INTEGER size{}, start{};

    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > record_limit ||
        !SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) {
        fail();
    }

    std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t offset = 0;

    while (offset < text.size()) {
        DWORD count = 0;

        if (!ReadFile(file, text.data() + offset, static_cast<DWORD>(text.size() - offset), &count,
                      nullptr) ||
            !count) {
            fail();
        }

        offset += count;
    }

    return text;
}

void write(HANDLE file, const std::string &text) {
    if (text.size() > record_limit) {
        fail();
    }

    LARGE_INTEGER start{};

    if (!SetFilePointerEx(file, start, nullptr, FILE_BEGIN)) {
        fail();
    }

    std::size_t offset = 0;

    while (offset < text.size()) {
        DWORD count = 0;

        if (!WriteFile(file, text.data() + offset, static_cast<DWORD>(text.size() - offset), &count,
                       nullptr) ||
            !count) {
            fail();
        }

        offset += count;
    }

    if (!SetEndOfFile(file) || !FlushFileBuffers(file)) {
        fail();
    }
}

struct Locked {
    Directory dir;
    bool created = false;
    Handle reservation;

    Locked(const std::filesystem::path &path, const std::string &name, bool create)
        : dir(path, false) {
        const auto filename = path / (name + ".lock");
        PrivateSecurity security;

        if (create) {
            reservation.value = CreateFileW(
                    filename.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, &security.attributes, CREATE_NEW,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH,
                    nullptr);
            created = reservation.value != INVALID_HANDLE_VALUE;

            if (!created && GetLastError() != ERROR_FILE_EXISTS &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                fail();
            }
        }

        if (!created) {
            reservation.value = CreateFileW(
                    filename.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL,
                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_WRITE_THROUGH,
                    nullptr);
        }

        if (reservation.value == INVALID_HANDLE_VALUE) {
            fail();
        }

        check(reservation.value);
        OVERLAPPED position{};

        if (!LockFileEx(reservation.value, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD,
                        &position)) {
            fail();
        }
    }
};

void atomic_state(Locked &lock, const std::string &name, const Json &state) {
    const auto text = state.dump();

    if (text.size() > record_limit) {
        throw VoyemuError("ADMISSION_EXTENT", "receipt exceeds 8 MiB");
    }

    PrivateSecurity security;
    const auto pending = lock.dir.path / (name + ".pending"),
               target = lock.dir.path / (name + ".json");
    {
        Handle file(CreateFileW(pending.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0,
                                &security.attributes, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                                        FILE_FLAG_WRITE_THROUGH,
                                nullptr));

        if (file.value == INVALID_HANDLE_VALUE) {
            fail();
        }

        check(file.value);
        write(file.value, text);
    }

    if (!MoveFileExW(pending.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        fail();
    }
}

AdmissionResult existing(Locked &lock, const std::string &name,
                         const std::optional<std::string> &expected) {
    AdmissionResult result{false, name.substr(0, 32), "unknown", std::nullopt};
    Json reservation;

    try {
        reservation = Json::parse(read(lock.reservation.value));
    } catch (const Json::exception &) {
        return result;
    }

    if (!reservation.is_object() || reservation.value("version", 0) != 1 ||
        !reservation.contains("digest") || !reservation["digest"].is_string()) {
        return result;
    }

    if (expected && reservation["digest"] != *expected) {
        throw VoyemuError("REQUEST_CONFLICT", "request_id already used with a different payload");
    }

    result.status = "pending";
    const auto filename = lock.dir.path / (name + ".json");
    Handle terminal(CreateFileW(filename.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));

    if (terminal.value == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return result;
        }

        fail();
    }

    try {
        auto state = Json::parse(read(terminal.value));

        if (state.at("operation_id") != result.operation_id ||
            state.at("digest") != reservation["digest"]) {
            fail();
        }

        if (state.at("status") == "terminal") {
            result.response = state.at("response");
            result.status = "terminal";
        } else if (state.at("status") != "pending") {
            fail();
        }
    } catch (...) {
        fail();
    }

    return result;
}
} // namespace

AdmissionStore::AdmissionStore(std::filesystem::path directory)
    : directory_(std::filesystem::absolute(directory).lexically_normal()) {
    Directory directory_handle(directory_, true);
}

AdmissionResult AdmissionStore::begin(const std::string &scope, const std::string &request_id,
                                      const Json &payload) {
    auto name = key(scope, request_id);
    auto canonical = payload.dump();

    if (canonical.size() > payload_limit) {
        throw VoyemuError("INVALID_REQUEST", "action payload too large");
    }

    auto hash = digest(canonical);
    Locked lock(directory_, name, true);

    if (!lock.created) {
        return existing(lock, name, hash);
    }

    write(lock.reservation.value, Json{{"version", 1}, {"digest", hash}}.dump());
    atomic_state(
            lock, name,
            Json{{"operation_id", name.substr(0, 32)}, {"digest", hash}, {"status", "pending"}});

    return {true, name.substr(0, 32), "pending", std::nullopt};
}

std::optional<AdmissionResult> AdmissionStore::lookup(const std::string &scope,
                                                      const std::string &request_id) {
    const auto name = key(scope, request_id);
    Directory directory(directory_, false);
    const auto path = directory_ / (name + ".lock");
    const auto attributes = GetFileAttributesW(path.c_str());

    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND) {
            return std::nullopt;
        }

        fail();
    }

    Locked lock(directory_, name, false);

    return existing(lock, name, std::nullopt);
}

void AdmissionStore::complete(const std::string &scope, const std::string &request_id,
                              const std::string &operation_id, const Json &response) {
    auto name = key(scope, request_id);
    Locked lock(directory_, name, false);
    auto result = existing(lock, name, std::nullopt);

    if (result.operation_id != operation_id || result.status == "unknown") {
        fail();
    }

    if (result.response) {
        if (*result.response != response) {
            fail();
        }

        return;
    }

    auto reservation = Json::parse(read(lock.reservation.value));
    atomic_state(lock, name,
                 Json{{"operation_id", operation_id},
                      {"digest", reservation.at("digest")},
                      {"status", "terminal"},
                      {"response", response}});
}
} // namespace voyemu::api
