#include "error.hpp"
#include "https.hpp"
#include "interruption.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <array>
#include <cwchar>
#include <stdexcept>

namespace voyemu::os {
namespace {
// One asynchronous request: WinHTTP completes each operation on its own thread and signals done, so
// the waiting thread notices an interruption while no byte arrives.
struct Transfer {
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD error = 0;
    DWORD bytes = 0;

    Transfer() = default;
    Transfer(const Transfer &) = delete;
    Transfer &operator=(const Transfer &) = delete;

    ~Transfer() {
        if (done) {
            CloseHandle(done);
        }

        if (closed) {
            CloseHandle(closed);
        }
    }
};

void CALLBACK transfer_status(HINTERNET, DWORD_PTR context, DWORD status, LPVOID information,
                              DWORD length) {
    auto &transfer = *reinterpret_cast<Transfer *>(context);

    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) {
        SetEvent(transfer.closed);

        return;
    }

    if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        transfer.error = static_cast<WINHTTP_ASYNC_RESULT *>(information)->dwError;
    } else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        transfer.bytes = length;
    } else if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE &&
               status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
        return;
    }

    SetEvent(transfer.done);
}
} // namespace

// WinHTTP with the system certificate store and proxy settings.
void https_get(const std::string &url, const std::vector<std::string> &headers,
               const std::function<void(std::string_view)> &sink,
               const std::function<void(std::uint64_t)> &length) {
    auto wide = [](const std::string &text) {
        const auto n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);

        if (!n) {
            throw VoyemuError("DOWNLOAD_URL", "invalid UTF8");
        }

        std::wstring result(n, L'\0');

        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                 static_cast<int>(text.size()), result.data(), n)) {
            throw VoyemuError("DOWNLOAD_URL", "conversion failed");
        }

        return result;
    };
    auto wide_url = wide(url);
    URL_COMPONENTS parts{};

    parts.dwStructSize = sizeof parts;
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength =
            parts.dwUserNameLength = parts.dwPasswordLength = DWORD(-1);

    if (!WinHttpCrackUrl(wide_url.c_str(), static_cast<DWORD>(wide_url.size()), 0, &parts) ||
        parts.nScheme != INTERNET_SCHEME_HTTPS || parts.dwUserNameLength ||
        parts.dwPasswordLength) {
        throw VoyemuError("DOWNLOAD_URL", "HTTPS without credentials required");
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength),
            path(parts.lpszUrlPath, parts.dwUrlPathLength);

    if (parts.dwExtraInfoLength) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }

    if (path.empty()) {
        path = L"/";
    }

    struct Handle {
        HINTERNET value;

        ~Handle() {
            if (value) {
                WinHttpCloseHandle(value);
            }
        }
    };
    // The state of the request and its read buffer outlive the request handle and its callbacks.
    Transfer transfer;
    std::array<char, 65536> buffer{};

    if (!transfer.done || !transfer.closed) {
        throw VoyemuError("DOWNLOAD_HTTP", "transfer events unavailable");
    }

    Handle session{WinHttpOpen(L"Voyemu/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)};

    if (!session.value || !WinHttpSetTimeouts(session.value, 30000, 30000, 30000, 60000)) {
        throw VoyemuError("DOWNLOAD_HTTP", "session initialization failed");
    }

    Handle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};

    if (!connection.value) {
        throw VoyemuError("DOWNLOAD_HTTP", "connection failed");
    }

    auto *const opened = WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                                            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    auto context = reinterpret_cast<DWORD_PTR>(&transfer);

    if (!opened) {
        throw VoyemuError("DOWNLOAD_HTTP", "request initialization failed");
    }

    if (!WinHttpSetOption(opened, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof context) ||
        WinHttpSetStatusCallback(opened, transfer_status,
                                 WINHTTP_CALLBACK_FLAG_SENDREQUEST_COMPLETE |
                                         WINHTTP_CALLBACK_FLAG_HEADERS_AVAILABLE |
                                         WINHTTP_CALLBACK_FLAG_READ_COMPLETE |
                                         WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
                                         WINHTTP_CALLBACK_FLAG_HANDLES,
                                 0) == WINHTTP_INVALID_STATUS_CALLBACK) {
        WinHttpCloseHandle(opened);

        throw VoyemuError("DOWNLOAD_HTTP", "request initialization failed");
    }

    // Closing the request cancels its pending operation; its last callback announces the closing.
    struct Request {
        HINTERNET value;
        const Transfer &transfer;

        ~Request() {
            WinHttpCloseHandle(value);
            WaitForSingleObject(transfer.closed, INFINITE);
        }
    } request{opened, transfer};
    // Completes the operation that begin starts: its WinHTTP error, 0 on success. While it waits,
    // an interruption throws and the request closes on the way out.
    const auto complete = [&](const auto &begin) -> DWORD {
        transfer.error = 0;
        transfer.bytes = 0;

        if (!begin()) {
            const auto error = GetLastError();

            return error ? error : ERROR_WINHTTP_INTERNAL_ERROR;
        }

        while (WaitForSingleObject(transfer.done, 100) == WAIT_TIMEOUT) {
            interruption::check();
        }

        return transfer.error;
    };

    std::wstring extra;

    for (const auto &header : headers) {
        extra += wide(header) + L"\r\n";
    }

    DWORD redirects = 5, policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;

    auto error = complete([&] {
        return WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
                                &redirects, sizeof redirects) &&
               WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                                sizeof policy) &&
               WinHttpSendRequest(request.value,
                                  extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(),
                                  extra.empty() ? 0 : static_cast<DWORD>(extra.size()),
                                  WINHTTP_NO_REQUEST_DATA, 0, 0, context);
    });

    if (!error) {
        error = complete([&] {
            return WinHttpReceiveResponse(request.value, nullptr);
        });
    }

    if (error) {
        if (error == ERROR_WINHTTP_NAME_NOT_RESOLVED || error == ERROR_WINHTTP_CANNOT_CONNECT ||
            error == ERROR_WINHTTP_TIMEOUT) {
            throw HttpsFailure(HttpsFailure::Kind::unreachable, 0, "HTTPS transfer failed");
        }

        throw VoyemuError("DOWNLOAD_HTTP", "HTTPS transfer failed");
    }

    DWORD status = 0, bytes = sizeof status;

    if (WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes,
                            WINHTTP_NO_HEADER_INDEX) &&
        (status == 429 || status == 503)) {
        // A Retry-After HTTP date is not a number and counts as absent.
        DWORD retry_after = 0;
        bytes = sizeof retry_after;
        const bool delay = WinHttpQueryHeaders(
                request.value, WINHTTP_QUERY_RETRY_AFTER | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &retry_after, &bytes, WINHTTP_NO_HEADER_INDEX);

        throw HttpsBusy(status, delay && retry_after ? static_cast<long long>(retry_after) : -1);
    }

    // A status that cannot be read stays 0 and names no status.
    if (status != 200 && status) {
        throw HttpsFailure(HttpsFailure::Kind::status, static_cast<long>(status),
                           "successful complete response required");
    }

    if (status != 200) {
        throw VoyemuError("DOWNLOAD_HTTP", "successful complete response required");
    }

    std::array<wchar_t, 32> size{};
    bytes = static_cast<DWORD>(sizeof size - sizeof(wchar_t));

    if (length && WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH,
                                      WINHTTP_HEADER_NAME_BY_INDEX, size.data(), &bytes,
                                      WINHTTP_NO_HEADER_INDEX)) {
        length(std::wcstoull(size.data(), nullptr, 10));
    }

    // The 60-second receive timeout of the session bounds each read, not the whole transfer.
    for (;;) {
        // An asynchronous read reports its byte count only to the callback.
        error = complete([&] {
            return WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                                   nullptr);
        });

        if (error == ERROR_WINHTTP_TIMEOUT) {
            throw HttpsFailure(HttpsFailure::Kind::stalled, static_cast<long>(status),
                               "response read failed");
        }

        if (error) {
            throw VoyemuError("DOWNLOAD_HTTP", "response read failed");
        }

        if (!transfer.bytes) {
            break;
        }

        sink({buffer.data(), transfer.bytes});
    }
}
} // namespace voyemu::os
