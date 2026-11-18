#include "error.hpp"
#include "https.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <array>
#include <chrono>
#include <stdexcept>

namespace voyemu::os {
// WinHTTP with the system certificate store and proxy settings.
void https_get(const std::string &url, const std::vector<std::string> &headers,
               const std::function<void(std::string_view)> &sink) {
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
    Handle session{WinHttpOpen(L"Voyemu/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};

    if (!session.value || !WinHttpSetTimeouts(session.value, 30000, 30000, 30000, 60000)) {
        throw VoyemuError("DOWNLOAD_HTTP", "session initialization failed");
    }

    Handle connection{WinHttpConnect(session.value, host.c_str(), parts.nPort, 0)};

    if (!connection.value) {
        throw VoyemuError("DOWNLOAD_HTTP", "connection failed");
    }

    Handle request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      WINHTTP_FLAG_SECURE)};

    if (!request.value) {
        throw VoyemuError("DOWNLOAD_HTTP", "request initialization failed");
    }

    std::wstring extra;

    for (const auto &header : headers) {
        extra += wide(header) + L"\r\n";
    }

    DWORD redirects = 5, policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;

    if (!WinHttpSetOption(request.value, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &redirects,
                          sizeof redirects) ||
        !WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof policy) ||
        !WinHttpSendRequest(request.value,
                            extra.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : extra.c_str(),
                            extra.empty() ? 0 : static_cast<DWORD>(extra.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
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

    if (status != 200) {
        throw VoyemuError("DOWNLOAD_HTTP", "successful complete response required");
    }

    const auto started = std::chrono::steady_clock::now();
    std::array<char, 65536> buffer{};

    for (;;) {
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(600)) {
            throw VoyemuError("DOWNLOAD_TIMEOUT", "response deadline exceeded");
        }

        DWORD count = 0;

        if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()),
                             &count)) {
            throw VoyemuError("DOWNLOAD_HTTP", "response read failed");
        }

        if (!count) {
            break;
        }

        sink({buffer.data(), count});
    }
}
} // namespace voyemu::os
