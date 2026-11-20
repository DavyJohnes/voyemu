#pragma once
#include "error.hpp"
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace voyemu::os {
// The DOWNLOAD_HTTP failure of a server that refuses the request for now with HTTP 429 or 503
// before any body byte; retry_after_seconds is its Retry-After delay, or -1 when the response has
// none.
struct HttpsBusy : VoyemuError {
    HttpsBusy(long status_code, long long retry_after)
        : VoyemuError("DOWNLOAD_HTTP", "server busy with HTTP " + std::to_string(status_code)),
          status(status_code), retry_after_seconds(retry_after) {}

    long status;
    long long retry_after_seconds;
};
// One HTTPS GET with the extra request headers that follows at most five HTTPS redirects and passes
// the body of the final 200 response to sink in order. HTTP 429 and 503 throw HttpsBusy, other
// failures DOWNLOAD_HTTP, or DOWNLOAD_LIBRARY when the host lacks the system library that does
// HTTPS;
// an exception from sink ends the transfer and propagates. length receives the Content-Length of
// the final response before its first body byte when the response has one. A transfer fails with
// DOWNLOAD_HTTP after 60 seconds without a received byte, however long it runs in all.
void https_get(const std::string &url, const std::vector<std::string> &headers,
               const std::function<void(std::string_view)> &sink,
               const std::function<void(std::uint64_t)> &length = {});
} // namespace voyemu::os
