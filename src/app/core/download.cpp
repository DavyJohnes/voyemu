#include "download.hpp"
#include "check.hpp"
#include "disk_space.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "os/https.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <fcntl.h>

#include <io.h>

#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace voyemu {
namespace {
void validate(const DownloadPin &pin) {
    if (!pin.url.starts_with("https://") ||
        (pin.encoding != DownloadEncoding::raw && pin.encoding != DownloadEncoding::base64) ||
        (pin.encoding == DownloadEncoding::base64 && pin.maximum_bytes > 1024 * 1024)) {
        throw VoyemuError("DOWNLOAD_PIN", "supported bounded HTTPS representation required");
    }

    const auto authority = pin.url.find_first_of("/?", 8);
    const auto host = pin.url.substr(8, authority == std::string::npos ? std::string::npos
                                                                       : authority - 8);

    if (!pin.url.starts_with("https://") || host.empty() || pin.url.size() > 4096 ||
        host.find('@') != std::string::npos || pin.url.find('#') != std::string::npos ||
        pin.url.find_first_of("\r\n\t\\ ") != std::string::npos || !sha256_text(pin.sha256) ||
        !pin.maximum_bytes || pin.maximum_bytes > (16ULL << 30)) {
        throw VoyemuError("DOWNLOAD_PIN", "bounded HTTPS URL and lowercase SHA256 required");
    }
}

std::uint64_t verify(const std::filesystem::path &file, const DownloadPin &pin) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) ||
        std::filesystem::hard_link_count(file) != 1 ||
        std::filesystem::file_size(file) > pin.maximum_bytes) {
        throw VoyemuError("DOWNLOAD_CACHE", "ordinary bounded file required");
    }

    const auto identity = cache_file_identity(file);

    if (sha256_file(file) != pin.sha256) {
        throw VoyemuError("DOWNLOAD_SHA", "cached or received bytes differ from pin");
    }

    if (cache_file_identity(file) != identity) {
        throw VoyemuError("DOWNLOAD_CHANGED", "bytes changed during verification");
    }

    return std::filesystem::file_size(file);
}

std::string decode_base64(std::string_view source, std::size_t maximum) {
    std::string text;

    for (unsigned char c : source) {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
            continue;
        }

        text += char(c);
    }

    if (text.size() % 4) {
        throw VoyemuError("DOWNLOAD_ENCODING", "incomplete base64 group");
    }

    auto value = [](unsigned char c) -> unsigned {
        if (c >= 'A' && c <= 'Z') {
            return c - 'A';
        }

        if (c >= 'a' && c <= 'z') {
            return c - 'a' + 26;
        }

        if (c >= '0' && c <= '9') {
            return c - '0' + 52;
        }

        if (c == '+') {
            return 62;
        }

        if (c == '/') {
            return 63;
        }

        throw VoyemuError("DOWNLOAD_ENCODING", "invalid base64 byte");
    };
    std::string output;

    for (std::size_t i = 0; i < text.size(); i += 4) {
        const bool pad2 = text[i + 2] == '=', pad3 = text[i + 3] == '=';
        const auto a = value(text[i]), b = value(text[i + 1]), c = pad2 ? 0 : value(text[i + 2]),
                   e = pad3 ? 0 : value(text[i + 3]);

        if ((pad2 && !pad3) || ((pad2 || pad3) && i + 4 != text.size()) || (pad2 && (b & 15)) ||
            (!pad2 && pad3 && (c & 3))) {
            throw VoyemuError("DOWNLOAD_ENCODING", "invalid base64 padding");
        }

        const std::uint32_t bits = (a << 18) | (b << 12) | (c << 6) | e;
        output += char(bits >> 16);

        if (!pad2) {
            output += char(bits >> 8);
        }

        if (!pad3) {
            output += char(bits);
        }

        if (output.size() > maximum) {
            throw VoyemuError("DOWNLOAD_BOUND", "decoded resource exceeds maximum");
        }
    }

    return output;
}

void https(const DownloadPin &pin, const DownloadSink &sink,
           const std::function<void(std::uint64_t)> &length) {
    std::vector<std::string> headers;

    // Public Homebrew OCI blobs accept an anonymous marker, never user credentials.
    if (pin.url.starts_with("https://ghcr.io/v2/homebrew/core/")) {
        headers.emplace_back("Authorization: Bearer QQ==");
    }

    os::https_get(pin.url, headers, sink, length);
}

// The cache files that this process has already reported as downloaded or verified; a file is
// still verified on each use, but reported once.
std::mutex reported_mutex;
std::set<std::filesystem::path> reported_files;

bool first_report(const std::filesystem::path &file) {
    std::lock_guard lock(reported_mutex);

    return reported_files.insert(file).second;
}
} // namespace

std::string download_name(const DownloadPin &pin) {
    return pin.name.empty() ? progress::url_file(pin.url) : pin.name;
}

progress::Arguments download_names(const DownloadPin &pin) {
    progress::Arguments names{{"name", download_name(pin)}};

    if (!pin.label.empty()) {
        names["label"] = pin.label;
        names["version"] = pin.version;
    }

    return names;
}

DownloadedBlob download_pinned(const DownloadPin &pin, const std::filesystem::path &cache,
                               bool offline, std::uint64_t reserve,
                               const DownloadTransport &transport) {
    validate(pin);

    if (!cache.is_absolute() || cache.lexically_normal() != cache) {
        throw VoyemuError("DOWNLOAD_PATH", "absolute normalized cache required");
    }

    ensure_private_directory(cache);
    const auto destination = cache / (pin.sha256 + ".blob");

    if (std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        if (first_report(destination)) {
            progress::report("download_cached", download_names(pin));
        } else {
            logging::write(logging::Level::debug, "download", "cached file verified again",
                           {{"file", destination.string()}});
        }

        return {destination, verify(destination, pin), true};
    }

    if (offline) {
        throw VoyemuError("DOWNLOAD_OFFLINE", "pinned cache file missing");
    }

    require_disk_space("DOWNLOAD_SPACE", cache, 0, reserve);

    const auto temporary = cache / (".part-" + random_token());
#ifdef _WIN32
    const auto fd = _wopen(temporary.c_str(), _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                           _S_IREAD | _S_IWRITE);
    auto *stream = fd >= 0 ? _fdopen(fd, "w+b") : nullptr;
#else
    const auto fd = ::open(temporary.c_str(), O_CREAT | O_EXCL | O_NOFOLLOW | O_RDWR | O_CLOEXEC,
                           0600);
    auto *stream = fd >= 0 ? ::fdopen(fd, "w+b") : nullptr;
#endif

    if (!stream) {
#ifdef _WIN32
        if (fd >= 0) {
            _close(fd);
        }
#else
        if (fd >= 0) {
            ::close(fd);
        }
#endif
        throw VoyemuError("DOWNLOAD_FILE", "exclusive temporary file failed");
    }

    const auto initial = cache_file_identity(temporary);
    struct Cleanup {
        std::filesystem::path path;
        std::FILE *stream;
        Json initial;

        ~Cleanup() {
            if (stream) {
                std::fclose(stream);
            }

            try {
                if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path))) {
                    return;
                }

                const auto now = cache_file_identity(path);
#ifdef _WIN32
                if (now.at("volume") == initial.at("volume") &&
                    now.at("index_high") == initial.at("index_high") &&
                    now.at("index_low") == initial.at("index_low")) {
                    std::filesystem::remove(path);
                }
#else
                if (now.at("volume_uuid") == initial.at("volume_uuid") &&
                    now.at("inode") == initial.at("inode")) {
                    std::filesystem::remove(path);
                }
#endif
            } catch (...) { /* Ignored: an unknown identity is retained rather than removed. */
            }
        }
    } cleanup{temporary, stream, initial};
    const auto maximum_received = pin.encoding == DownloadEncoding::base64
                                          ? pin.maximum_bytes * 2 + 16
                                          : pin.maximum_bytes;
    std::uint64_t received = 0;
    const auto host = progress::url_host(pin.url);
    auto context = download_names(pin);
    context["host"] = host;
    progress::Meter meter("download", context);
    const DownloadSink sink = [&](std::string_view bytes) {
        if (bytes.size() > maximum_received - received) {
            throw VoyemuError("DOWNLOAD_BOUND", "response exceeds declared maximum");
        }

        if (std::filesystem::space(cache).available < reserve) {
            require_disk_space("DOWNLOAD_SPACE", cache, 0, reserve);
        }

        if (std::fwrite(bytes.data(), 1, bytes.size(), stream) != bytes.size()) {
            throw VoyemuError("DOWNLOAD_WRITE", "incomplete response write");
        }

        received += bytes.size();
        meter.advance(bytes.size());
    };

    // A server that refuses the request for now gets six more attempts after its Retry-After delay,
    // at most five minutes, or else after 2, 4, 8, 16, 32 and 64 seconds.
    for (unsigned attempt = 1;; ++attempt) {
        try {
            if (transport) {
                transport(pin, sink);
            } else {
                // A response that announces its size must fit beside the reserve before it arrives.
                https(pin, sink, [&](std::uint64_t total) {
                    require_disk_space("DOWNLOAD_SPACE", cache, total, reserve);
                    meter.length(total);
                });
            }

            break;
        } catch (const os::HttpsBusy &busy) {
            if (attempt == 7 || received) {
                throw;
            }

            const auto delay = busy.retry_after_seconds >= 0
                                       ? std::min<long long>(busy.retry_after_seconds, 300)
                                       : 1LL << attempt;
            progress::report("download_retry",
                             {{"host", host},
                              {"status", std::to_string(busy.status)},
                              {"attempt", std::to_string(attempt)},
                              {"delay", progress::duration_text(static_cast<std::uint64_t>(delay),
                                                                progress::language())}});
            std::this_thread::sleep_for(std::chrono::seconds(delay));
        }
    }

    if (std::fflush(stream)) {
        throw VoyemuError("DOWNLOAD_WRITE", "flush failed");
    }

    if (pin.encoding == DownloadEncoding::base64) {
        std::string encoded(static_cast<std::size_t>(received), '\0');

        if (std::fseek(stream, 0, SEEK_SET) ||
            std::fread(encoded.data(), 1, encoded.size(), stream) != encoded.size()) {
            throw VoyemuError("DOWNLOAD_READ", "encoded resource incomplete");
        }

        const auto decoded = decode_base64(encoded, static_cast<std::size_t>(pin.maximum_bytes));

        if (std::fseek(stream, 0, SEEK_SET) ||
            std::fwrite(decoded.data(), 1, decoded.size(), stream) != decoded.size() ||
            std::fflush(stream)) {
            throw VoyemuError("DOWNLOAD_WRITE", "decoded resource incomplete");
        }

#ifdef _WIN32
        if (_chsize_s(_fileno(stream), decoded.size())) {
            throw VoyemuError("DOWNLOAD_WRITE", "decoded extent failed");
        }
#else
        if (::ftruncate(::fileno(stream), static_cast<off_t>(decoded.size()))) {
            throw VoyemuError("DOWNLOAD_WRITE", "decoded extent failed");
        }
#endif
    }

#ifdef _WIN32
    if (_commit(_fileno(stream))) {
        throw VoyemuError("DOWNLOAD_WRITE", "synchronization failed");
    }
#else
    if (::fsync(::fileno(stream))) {
        throw VoyemuError("DOWNLOAD_WRITE", "synchronization failed");
    }
#endif

    if (std::fclose(stream)) {
        cleanup.stream = nullptr;
        throw VoyemuError("DOWNLOAD_WRITE", "close failed");
    }

    cleanup.stream = nullptr;
    const auto bytes = verify(temporary, pin);
    meter.finish();
    first_report(destination);
#ifdef _WIN32
    if (!MoveFileW(temporary.c_str(), destination.c_str())) {
        throw VoyemuError("DOWNLOAD_PUBLISH", "fresh cache destination required");
    }
#else
    if (::link(temporary.c_str(), destination.c_str())) {
        throw VoyemuError("DOWNLOAD_PUBLISH", "fresh cache destination required");
    }

    if (::unlink(temporary.c_str())) {
        throw VoyemuError("DOWNLOAD_PUBLISH", "temporary link removal failed");
    }
#endif

    return {destination, bytes, false};
}
} // namespace voyemu
