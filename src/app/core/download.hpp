#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
namespace voyemu {
enum class DownloadEncoding {
    raw,
    base64
};
struct DownloadPin {
    std::string url, sha256;
    std::uint64_t maximum_bytes = 8ULL << 30;
    DownloadEncoding encoding = DownloadEncoding::raw;
};
struct DownloadedBlob {
    std::filesystem::path file;
    std::uint64_t bytes{};
    bool cached = false;
};
using DownloadSink = std::function<void(std::string_view)>;
using DownloadTransport = std::function<void(const DownloadPin &, const DownloadSink &)>;
// Native HTTPS by default; the transport argument permits offline contract tests without sockets.
// A transport failure os::HttpsBusy (HTTP 429 or 503) is retried up to seven attempts in all.
DownloadedBlob download_pinned(const DownloadPin &, const std::filesystem::path &cache,
                               bool offline = false, std::uint64_t reserve_bytes = 20ULL << 30,
                               const DownloadTransport &transport = {});
} // namespace voyemu
