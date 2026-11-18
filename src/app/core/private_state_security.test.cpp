#include "private_state_security.hpp"
#include "model.hpp"
#include "process.hpp"
#include <array>
#include <iostream>
using namespace voyemu;
namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

struct Acl {
    PACL value = nullptr;

    ~Acl() {
        if (value) {
            LocalFree(value);
        }
    }
};

void grant(Acl &target, PACL initial, PSID sid, DWORD rights) {
    EXPLICIT_ACCESSW entry{};

    entry.grfAccessPermissions = rights;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.ptstrName = static_cast<LPWSTR>(sid);
    check(SetEntriesInAclW(1, &entry, initial, &target.value) == ERROR_SUCCESS,
          "fixture ACL creation");
}

void world_readback(HANDLE file, PSID world, DWORD mask) {
    PACL acl = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;

    check(GetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl,
                          nullptr, &descriptor) == ERROR_SUCCESS,
          "fixture ACL readback");
    bool found = false;

    if (acl) {
        for (DWORD i = 0; i < acl->AceCount; ++i) {
            void *raw = nullptr;

            if (GetAce(acl, i, &raw) &&
                static_cast<ACE_HEADER *>(raw)->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);
                std::cerr << "ACL fixture world=" << bool(EqualSid(&ace->SidStart, world))
                          << " observed_mask=" << ace->Mask << " requested=" << mask << "\n";

                if (EqualSid(&ace->SidStart, world) && (ace->Mask & mask) == mask) {
                    found = true;
                }
            }
        }
    }

    if (descriptor) {
        LocalFree(descriptor);
    }

    check(found, "requested Everyone ACE not observed");
}
} // namespace

int main(int argc, char **argv) {
    try {
        check(argc == 2, "fixture root required");
        detail::PrivateStateSecurity security;
        const auto current = reinterpret_cast<TOKEN_USER *>(security.user.data())->User.Sid;
        BYTE world[SECURITY_MAX_SID_SIZE]{}, system[SECURITY_MAX_SID_SIZE]{},
                administrators[SECURITY_MAX_SID_SIZE]{};
        DWORD size = sizeof world;
        check(CreateWellKnownSid(WinWorldSid, nullptr, world, &size), "world SID");
        size = sizeof system;
        check(CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &size), "system SID");
        size = sizeof administrators;
        check(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators, &size),
              "administrator SID");
        check(security.accepts(current, security.acl, true), "current user private ACL accepted");
        check(!security.accepts(world, security.acl, false), "foreign owner rejected");
        check(!security.accepts(current, nullptr, false), "null DACL rejected");
        Acl readable;
        grant(readable, security.acl, world, FILE_GENERIC_READ);
        check(security.accepts(current, readable.value, false), "artifact foreign read accepted");
        check(!security.accepts(current, readable.value, true), "private foreign read rejected");
        Acl writable;
        grant(writable, security.acl, world, FILE_GENERIC_WRITE);
        check(!security.accepts(current, writable.value, false) &&
                      !security.accepts(current, writable.value, true),
              "foreign write rejected");
        Acl privileged;
        grant(privileged, security.acl, system, FILE_ALL_ACCESS);
        check(security.accepts(current, privileged.value, true), "existing SYSTEM policy accepted");
        Acl administrator;
        grant(administrator, security.acl, administrators, FILE_ALL_ACCESS);
        check(security.accepts(current, administrator.value, true),
              "existing Administrators policy accepted");

        for (const auto mask :
             std::array<DWORD, 3>{GENERIC_READ, GENERIC_EXECUTE, GENERIC_READ | GENERIC_EXECUTE}) {
            Acl generic;
            grant(generic, security.acl, world, mask);
            check(security.accepts(current, generic.value, false) &&
                          !security.accepts(current, generic.value, true),
                  "generic read/execute artifact only");
        }

        for (const auto mask : std::array<DWORD, 6>{GENERIC_WRITE, GENERIC_ALL, WRITE_DAC,
                                                    WRITE_OWNER, DELETE, FILE_DELETE_CHILD}) {
            Acl mutation;
            grant(mutation, security.acl, world, mask);
            check(!security.accepts(current, mutation.value, false),
                  "generic or explicit foreign mutation rejected");
        }

        Acl inherit_only;
        grant(inherit_only, security.acl, world, FILE_GENERIC_WRITE);

        for (DWORD i = 0; i < inherit_only.value->AceCount; ++i) {
            void *raw = nullptr;
            check(GetAce(inherit_only.value, i, &raw), "inherit fixture ACE");
            auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);

            if (EqualSid(&ace->SidStart, world)) {
                ace->Header.AceFlags |= INHERIT_ONLY_ACE;
            }
        }

        check(security.accepts(current, inherit_only.value, true),
              "inherit-only permission does not grant object access");
        alignas(ACL) BYTE denied_storage[512]{};
        auto denied = reinterpret_cast<PACL>(denied_storage);
        check(InitializeAcl(denied, sizeof denied_storage, ACL_REVISION) &&
                      AddAccessDeniedAce(denied, ACL_REVISION, FILE_GENERIC_WRITE, world) &&
                      AddAccessAllowedAce(denied, ACL_REVISION, FILE_GENERIC_WRITE, world),
              "deny/allow fixture");
        check(!security.accepts(current, denied, false),
              "foreign allow is conservatively rejected even after deny");
        Acl unknown;
        grant(unknown, security.acl, world, FILE_GENERIC_READ);

        for (DWORD i = 0; i < unknown.value->AceCount; ++i) {
            void *raw = nullptr;
            check(GetAce(unknown.value, i, &raw), "unknown fixture ACE");
            auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);

            if (EqualSid(&ace->SidStart, world)) {
                ace->Header.AceType = SYSTEM_AUDIT_ACE_TYPE;
            }
        }

        check(IsValidAcl(unknown.value) && !security.accepts(current, unknown.value, false),
              "unknown effective ACE rejected");
        const auto root = std::filesystem::path(argv[1]) / ("private-security-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};

        const auto long_lock = root / (std::string(215, 'l') + ".lock");
        check(long_lock.native().size() >= MAX_PATH, "fixture must exceed legacy MAX_PATH");
        auto lock = [&] {
            return detail::open_private_state_lock(
                    long_lock, GENERIC_READ | GENERIC_WRITE | READ_CONTROL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT);
        };
        const HANDLE held = lock();
        check(held != INVALID_HANDLE_VALUE, "long private lock creation");
        const bool private_file = security.accepts(held, true);
        const HANDLE competing = lock();
        CloseHandle(held);

        if (competing != INVALID_HANDLE_VALUE) {
            CloseHandle(competing);
        }

        const HANDLE released = lock();

        if (released != INVALID_HANDLE_VALUE) {
            CloseHandle(released);
        }

        check(DeleteFileW(os::win32_file_path(long_lock).c_str()), "long lock cleanup");
        check(private_file && competing == INVALID_HANDLE_VALUE && released != INVALID_HANDLE_VALUE,
              "long lock remains private and exclusive until released");

        std::cout << "Long private lock remained exclusive until release\n";

        for (unsigned mode = 0; mode < 3; ++mode) {
            auto descriptor = security.descriptor;
            check(SetSecurityDescriptorDacl(&descriptor, TRUE,
                                            mode == 0   ? security.acl
                                            : mode == 1 ? readable.value
                                                        : writable.value,
                                            FALSE),
                  "fixture descriptor");
            SECURITY_ATTRIBUTES attributes{sizeof attributes, &descriptor, FALSE};
            const auto path = root / std::to_string(mode);
            HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE | READ_CONTROL, 0,
                                      &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            check(file != INVALID_HANDLE_VALUE, "fixture file creation");

            if (mode) {
                world_readback(file, world, mode == 1 ? FILE_GENERIC_READ : FILE_GENERIC_WRITE);
            }

            const bool artifact = security.accepts(file, false),
                       private_file = security.accepts(file, true);
            CloseHandle(file);
            check(artifact == (mode != 2) && private_file == (mode == 0),
                  "actual creator/readback predicate");
        }

        std::cout
                << "PASS synthetic owner/nullDACL/privileged SID and real creator/Everyone read-write DACL checks\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
