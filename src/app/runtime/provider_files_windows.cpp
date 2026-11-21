#include "../core/private_state_security.hpp"
#include "error.hpp"
#include "provider_files.hpp"
#include <memory>
#include <vector>
namespace voyemu::runtime::detail {
void provider_require(bool condition) {
    if (!condition) {
        throw VoyemuError("PROVIDER_INVALID", "invalid provider");
    }
}

struct ProviderHandle {
    HANDLE value = INVALID_HANDLE_VALUE;

    explicit ProviderHandle(HANDLE file) : value(file) {
        provider_require(file != INVALID_HANDLE_VALUE);
    }

    ~ProviderHandle() {
        CloseHandle(value);
    }

    ProviderHandle(const ProviderHandle &) = delete;
};

void provider_path(const std::filesystem::path &path) {
    const auto drive = path.root_name().wstring();

    provider_require(
            path.is_absolute() && path.lexically_normal() == path && drive.size() == 2 &&
            drive[1] == L':' &&
            ((drive[0] >= L'A' && drive[0] <= L'Z') || (drive[0] >= L'a' && drive[0] <= L'z')));

    for (const auto &part : path.relative_path()) {
        const auto name = part.wstring();
        provider_require(!name.empty() && name != L"." && name != L".." &&
                         name.find(L':') == std::wstring::npos && name.back() != L'.' &&
                         name.back() != L' ');
    }
}

void provider_object(HANDLE file, bool directory, bool private_access) {
    BY_HANDLE_FILE_INFORMATION info{};

    provider_require(GetFileType(file) == FILE_TYPE_DISK &&
                     GetFileInformationByHandle(file, &info) &&
                     !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                     bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == directory &&
                     (directory || info.nNumberOfLinks == 1));
    voyemu::detail::PrivateStateSecurity security;
    provider_require(security.accepts(file, private_access));
}

// Retaining each ancestor without delete sharing fences component replacement.
// Only the DOS volume mapping is resolved; no non-root reparse is followed.
struct ProviderDirectory::State {
    std::vector<std::unique_ptr<ProviderHandle>> held_;
};

ProviderDirectory::ProviderDirectory(const std::filesystem::path &path, bool private_leaf,
                                     bool create_leaf, bool fresh)
    : state_(std::make_unique<State>()) {
    provider_path(path);
    provider_require((!private_leaf && !create_leaf) || path != path.root_path());
    auto current = path.root_path();
    const auto open = [&](bool root, bool leaf) {
        auto handle = std::make_unique<ProviderHandle>(CreateFileW(
                current.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | (root ? 0 : FILE_FLAG_OPEN_REPARSE_POINT), nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        provider_require(GetFileType(handle->value) == FILE_TYPE_DISK &&
                         GetFileInformationByHandle(handle->value, &info) &&
                         (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                         !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT));

        if (leaf && private_leaf) {
            provider_object(handle->value, true, true);
        }

        state_->held_.push_back(std::move(handle));
    };
    open(true, false);

    for (const auto &part : path.relative_path()) {
        current /= part;
        const bool leaf = current == path;

        if (leaf && create_leaf) {
            voyemu::detail::PrivateStateSecurity security;

            if (!CreateDirectoryW(current.c_str(), &security.attributes)) {
                provider_require(!fresh && GetLastError() == ERROR_ALREADY_EXISTS);
            }
        }

        open(false, leaf);
    }
}

ProviderDirectory::~ProviderDirectory() = default;

namespace {
class PinnedFile {
    std::filesystem::path path_;
    ProviderDirectory parents_;
    ProviderHandle file_;
    std::size_t limit_;
    bool private_access_;

    static std::filesystem::path parent(const std::filesystem::path &path) {
        provider_path(path);
        provider_require(!path.filename().empty());

        return path.parent_path();
    }

public:
    PinnedFile(const std::filesystem::path &path, std::size_t limit, bool private_access)
        : path_(path), parents_(parent(path)),
          file_(CreateFileW(path.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr)),
          limit_(limit), private_access_(private_access) {
        provider_object(file_.value, false, private_access_);
    }

    std::string text() {
        BY_HANDLE_FILE_INFORMATION before{}, after{};
        FILE_BASIC_INFO first{}, last{};

        provider_require(
                GetFileInformationByHandle(file_.value, &before) &&
                GetFileInformationByHandleEx(file_.value, FileBasicInfo, &first, sizeof first));
        const auto size = (std::uint64_t(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
        provider_require(size <= limit_);
        std::string out;
        char buffer[8192];

        for (;;) {
            DWORD count = 0;
            provider_require(ReadFile(file_.value, buffer, sizeof buffer, &count, nullptr));

            if (!count) {
                break;
            }

            provider_require(count <= limit_ - out.size());
            out.append(buffer, count);
        }

        provider_object(file_.value, false, private_access_);
        provider_require(
                GetFileInformationByHandle(file_.value, &after) &&
                GetFileInformationByHandleEx(file_.value, FileBasicInfo, &last, sizeof last));
        provider_require(before.dwVolumeSerialNumber == after.dwVolumeSerialNumber &&
                         before.nFileIndexHigh == after.nFileIndexHigh &&
                         before.nFileIndexLow == after.nFileIndexLow && size == out.size() &&
                         before.nFileSizeHigh == after.nFileSizeHigh &&
                         before.nFileSizeLow == after.nFileSizeLow &&
                         CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) == 0 &&
                         first.ChangeTime.QuadPart == last.ChangeTime.QuadPart);
        ProviderHandle named(CreateFileW(path_.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                         FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION current{};
        provider_object(named.value, false, private_access_);
        provider_require(GetFileInformationByHandle(named.value, &current) &&
                         current.dwVolumeSerialNumber == before.dwVolumeSerialNumber &&
                         current.nFileIndexHigh == before.nFileIndexHigh &&
                         current.nFileIndexLow == before.nFileIndexLow);

        return out;
    }
};
} // namespace

struct ProviderPinnedFile::State {
    PinnedFile file;

    State(const std::filesystem::path &path, std::size_t limit, bool private_access)
        : file(path, limit, private_access) {}
};

ProviderPinnedFile::ProviderPinnedFile(const std::filesystem::path &path, std::size_t limit,
                                       bool private_access)
    : state_(std::make_unique<State>(path, limit, private_access)) {}

ProviderPinnedFile::~ProviderPinnedFile() = default;

std::string ProviderPinnedFile::text() {
    return state_->file.text();
}

std::string provider_read(const std::filesystem::path &path, std::size_t limit,
                          bool private_access) {
    PinnedFile file(path, limit, private_access);

    return file.text();
}

void provider_request(const std::filesystem::path &pending, const std::filesystem::path &request,
                      const std::string &bytes) {
    provider_require(pending.parent_path() == request.parent_path() && bytes.size() <= 16384);
    {
        voyemu::detail::PrivateStateSecurity security;
        ProviderHandle file(CreateFileW(pending.c_str(), GENERIC_WRITE | READ_CONTROL, 0,
                                        &security.attributes, CREATE_NEW,
                                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        provider_object(file.value, false, true);
        DWORD count = 0;
        provider_require(WriteFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()),
                                   &count, nullptr) &&
                         count == bytes.size() && FlushFileBuffers(file.value));
    }
    provider_require(MoveFileExW(pending.c_str(), request.c_str(), MOVEFILE_WRITE_THROUGH));
}
} // namespace voyemu::runtime::detail
