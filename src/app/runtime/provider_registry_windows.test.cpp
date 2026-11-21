#include "os/private_state_security.hpp"
#include "provider_process.hpp"
#include "provider_registry_test_host.hpp"
#include <aclapi.h>
#include <cassert>
#include <fstream>
using namespace voyemu;
using namespace voyemu::runtime;

void public_result(const std::filesystem::path &path, const Json &reply) {
    voyemu::detail::PrivateStateSecurity security;
    BYTE world[SECURITY_MAX_SID_SIZE]{};
    DWORD size = sizeof world;

    assert(CreateWellKnownSid(WinWorldSid, nullptr, world, &size));
    EXPLICIT_ACCESSW entry{};
    entry.grfAccessPermissions = FILE_GENERIC_READ;
    entry.grfAccessMode = GRANT_ACCESS;
    entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    entry.Trustee.ptstrName = reinterpret_cast<LPWSTR>(world);
    PACL acl = nullptr;
    assert(SetEntriesInAclW(1, &entry, security.acl, &acl) == ERROR_SUCCESS);
    assert(SetSecurityDescriptorDacl(&security.descriptor, TRUE, acl, FALSE));
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE | READ_CONTROL, 0, &security.attributes,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    PACL observed = nullptr;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    assert(GetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                           &observed, nullptr, &descriptor) == ERROR_SUCCESS);
    bool found = false;

    for (DWORD i = 0; observed && i < observed->AceCount; ++i) {
        void *raw = nullptr;

        if (GetAce(observed, i, &raw) &&
            static_cast<ACE_HEADER *>(raw)->AceType == ACCESS_ALLOWED_ACE_TYPE) {
            auto ace = static_cast<ACCESS_ALLOWED_ACE *>(raw);

            if (EqualSid(&ace->SidStart, world) &&
                (ace->Mask & FILE_GENERIC_READ) == FILE_GENERIC_READ) {
                found = true;
            }
        }
    }

    assert(found);

    if (descriptor) {
        LocalFree(descriptor);
    }

    const auto bytes = reply.dump();
    DWORD count = 0;
    assert(WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) &&
           count == bytes.size());
    CloseHandle(file);
    LocalFree(acl);
}

void pinned_self_worker_case(const Json &manifest, const std::filesystem::path &self,
                             const std::filesystem::path &root, const std::filesystem::path &jobs,
                             const ProviderContext &context,
                             const std::function<void()> &check_clean) {
    auto actual = manifest;
    const auto executable = root / "runtime.exe", replacement = root / "replacement.exe";
    {
        voyemu::detail::PrivateStateSecurity security;
        HANDLE owned = CreateFileW(executable.c_str(), GENERIC_WRITE, 0, &security.attributes,
                                   CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        assert(owned != INVALID_HANDLE_VALUE && CloseHandle(owned));
        std::ifstream source(self, std::ios::binary);
        std::ofstream target(executable, std::ios::binary);
        target << source.rdbuf();
        assert(source && target);
    }

    std::filesystem::copy_file(executable, replacement);
    actual["runtime"] = {{"path", executable.string()}, {"sha256", sha256_file(executable)}};
    ProviderRegistry live(jobs);
    live.load(actual);
    const auto pinned_runner = [&](const ProcessSpec &spec, int milliseconds) {
        HANDLE write = CreateFileW(executable.c_str(), GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (write != INVALID_HANDLE_VALUE) {
            CloseHandle(write);
        }

        assert(write == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
        assert(!MoveFileExW(replacement.c_str(), executable.c_str(), MOVEFILE_REPLACE_EXISTING));
        assert(GetLastError() == ERROR_SHARING_VIOLATION || GetLastError() == ERROR_ACCESS_DENIED);

        return run_provider(spec, milliseconds);
    };
    const auto observed = live.collect("example", context, 5000, pinned_runner, [] {
        return true;
    });
    assert(observed["status"] == "ready" && observed["data"]["value"] == 0 &&
           observed["boot_id"] == "boot" && observed["request_id"] == "request");
    check_clean();
    assert(sha256_file(executable) == actual["runtime"]["sha256"].get<std::string>());
    HANDLE released = CreateFileW(executable.c_str(), GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(released != INVALID_HANDLE_VALUE);
    CloseHandle(released);
}
