#include "deps_test_host.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>
#include <stdexcept>

namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

void with_foreign_write(const std::filesystem::path &file, const std::function<void()> &granted) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL acl = nullptr;

    check(GetNamedSecurityInfoW(const_cast<LPWSTR>(file.c_str()), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr,
                                &descriptor) == ERROR_SUCCESS,
          "metadata DACL read");
    EXPLICIT_ACCESSW access{};
    access.grfAccessPermissions = FILE_WRITE_DATA | FILE_APPEND_DATA;
    access.grfAccessMode = GRANT_ACCESS;
    access.grfInheritance = NO_INHERITANCE;
    access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD world_size = sizeof(world);
    check(CreateWellKnownSid(WinWorldSid, nullptr, world, &world_size), "metadata world SID");
    access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(world);
    PACL updated = nullptr;
    check(SetEntriesInAclW(1, &access, acl, &updated) == ERROR_SUCCESS, "metadata DACL merge");
    const auto added = SetNamedSecurityInfoW(const_cast<LPWSTR>(file.c_str()), SE_FILE_OBJECT,
                                             DACL_SECURITY_INFORMATION, nullptr, nullptr, updated,
                                             nullptr);
    LocalFree(updated);
    check(added == ERROR_SUCCESS, "metadata DACL write");
    granted();
    check(SetNamedSecurityInfoW(const_cast<LPWSTR>(file.c_str()), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION, nullptr, nullptr, acl,
                                nullptr) == ERROR_SUCCESS,
          "metadata DACL restore");

    if (descriptor) {
        LocalFree(descriptor);
    }
}
