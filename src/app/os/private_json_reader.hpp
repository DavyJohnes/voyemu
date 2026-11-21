#pragma once
#include "core/error.hpp"
#ifdef _WIN32
#include <windows.h>

#include "os/current_user_windows.hpp"

#include "core/logging.hpp"
#include "core/process.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
namespace voyemu::detail {
// Cooperating processes serialize state publication and handle-bound reads.
// Wine 11 replacement APIs cannot atomically replace an open destination.
class PrivateJsonLock {
    struct Handle {
        HANDLE value = nullptr;

        ~Handle() {
            if (value) {
                CloseHandle(value);
            }
        }
    } mutex_;
    bool acquired_ = false;

public:
    explicit PrivateJsonLock(const std::filesystem::path &path) {
        std::error_code error;
        const auto parent =
                std::filesystem::canonical(std::filesystem::absolute(path).parent_path(), error);

        if (error == std::errc::no_such_file_or_directory) {
            throw VoyemuError("STATE_EXTENT", "invalid file");
        }

        if (error) {
            throw VoyemuError("STATE_LOCK", "canonical path identity failed: " + error.message());
        }

        auto canonical = (parent / path.filename()).make_preferred().wstring();
        const auto folded_size = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE,
                                               canonical.data(), static_cast<int>(canonical.size()),
                                               nullptr, 0, nullptr, nullptr, 0);

        if (folded_size <= 0) {
            throw VoyemuError("STATE_LOCK", "canonical path identity failed");
        }

        std::wstring folded(static_cast<std::size_t>(folded_size), L'\0');

        if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, canonical.data(),
                           static_cast<int>(canonical.size()), folded.data(), folded_size, nullptr,
                           nullptr, 0)) {
            throw VoyemuError("STATE_LOCK", "canonical path identity failed");
        }

        canonical = std::move(folded);
        os::CurrentUser user;

        switch (user.read()) {
        case os::CurrentUser::Reading::read:
            break;
        case os::CurrentUser::Reading::extent_differs:
            throw VoyemuError("STATE_LOCK", "user identity extent differs");
        case os::CurrentUser::Reading::token_unavailable:
        case os::CurrentUser::Reading::identity_unavailable:
            throw VoyemuError("STATE_LOCK", "user identity unavailable");
        }

        const auto sid = user.sid();

        if (!IsValidSid(sid)) {
            throw VoyemuError("STATE_LOCK", "invalid user identity");
        }

        std::string identity(reinterpret_cast<const char *>(canonical.data()),
                             canonical.size() * sizeof(wchar_t));
        identity.append(reinterpret_cast<const char *>(sid), GetLengthSid(sid));
        const auto digest = sha256_bytes(identity);
        const auto name = L"Local\\Voyemu.PrivateJson." +
                          std::wstring(digest.begin(), digest.end());
        mutex_.value = CreateMutexW(nullptr, FALSE, name.c_str());

        if (!mutex_.value) {
            throw VoyemuError("STATE_LOCK", "named coordination unavailable");
        }

        const auto result = WaitForSingleObject(mutex_.value, 5000);

        // A holder that exited without releasing the lock left at most its staging file, which
        // the next writer replaces; the published file is still whole.
        if (result == WAIT_ABANDONED) {
            logging::try_write(logging::Level::debug, "state", "state.lock_abandoned",
                               {{"path", path.string()}});
        } else if (result != WAIT_OBJECT_0) {
            if (result == WAIT_TIMEOUT) {
                throw VoyemuError("STATE_LOCK_TIMEOUT", "state operation exceeded five seconds");
            }

            throw VoyemuError("STATE_LOCK", "coordination wait failed");
        }

        acquired_ = true;
    }

    ~PrivateJsonLock() {
        if (acquired_) {
            ReleaseMutex(mutex_.value);
        }
    }

    PrivateJsonLock(const PrivateJsonLock &) = delete;
    PrivateJsonLock &operator=(const PrivateJsonLock &) = delete;
};

// A reader retains one ordinary file incarnation across atomic publication.
class PrivateJsonReader {
    PrivateJsonLock lock_;
    struct File {
        HANDLE value = INVALID_HANDLE_VALUE;

        ~File() {
            if (value != INVALID_HANDLE_VALUE) {
                CloseHandle(value);
            }
        }
    } file_;
    std::size_t maximum_;

public:
    PrivateJsonReader(const std::filesystem::path &path, std::size_t maximum)
        : lock_(path), maximum_(maximum) {
        file_.value = CreateFileW(path.c_str(), GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (file_.value == INVALID_HANDLE_VALUE) {
            throw VoyemuError("STATE_READ",
                              "cannot open file; win32=" + std::to_string(GetLastError()) +
                                      "; path=" + path.string());
        }

        BY_HANDLE_FILE_INFORMATION info{};

        if (GetFileType(file_.value) != FILE_TYPE_DISK ||
            !GetFileInformationByHandle(file_.value, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) ||
            info.nNumberOfLinks != 1) {
            throw VoyemuError("STATE_PATH", "ordinary unaliased file required");
        }

        const auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;

        if (size > maximum_) {
            throw VoyemuError("STATE_EXTENT", "invalid file");
        }
    }

    PrivateJsonReader(const PrivateJsonReader &) = delete;
    PrivateJsonReader &operator=(const PrivateJsonReader &) = delete;

    std::string text() {
        std::string text;
        char buffer[8192];

        for (;;) {
            DWORD count = 0;

            if (!ReadFile(file_.value, buffer, sizeof buffer, &count, nullptr)) {
                throw VoyemuError("STATE_READ", "cannot read file");
            }

            if (!count) {
                break;
            }

            if (count > maximum_ - text.size()) {
                throw VoyemuError("STATE_EXTENT", "file grew");
            }

            text.append(buffer, count);
        }

        return text;
    }
};
} // namespace voyemu::detail
#endif
