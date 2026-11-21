#include "media_rpc.hpp"
#include "logging.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "os/private_region.hpp"
#include "private_state_security.hpp"
#include "process.hpp"
#include <charconv>
#include <chrono>
#include <curl/curl.h>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace voyemu::media {
namespace {
struct CurlGlobal {
    CurlGlobal() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
            throw VoyemuError("MEDIA_CURL", "initialization failed");
        }

        const auto *version = curl_version_info(CURLVERSION_NOW);

        if (!version || (version->features & CURL_VERSION_HTTP2) == 0) {
            throw VoyemuError("MEDIA_CURL", "HTTP/2 is required");
        }
    }
};

void initialize() {
    static CurlGlobal initialized;

    (void)initialized;
}

struct Transfer {
    std::size_t messages{};
    MessageStream frames;
    const std::atomic<bool> *stop{};
    std::string error;
    std::optional<unsigned> grpc_status;
    explicit Transfer(MessageStream::Receiver receiver)
        : frames([this, receiver = std::move(receiver)](Bytes value) {
              ++messages;
              receiver(std::move(value));
          }) {}
};

size_t receive(char *data, size_t size, size_t count, void *opaque) {
    auto &state = *static_cast<Transfer *>(opaque);

    if (size && count > SIZE_MAX / size) {
        return 0;
    }

    try {
        state.frames.feed({reinterpret_cast<const std::uint8_t *>(data), size * count});

        return size * count;
    } catch (const std::exception &error) {
        state.error = error.what();

        return 0;
    }
}

size_t headers(char *data, size_t size, size_t count, void *opaque) {
    auto &state = *static_cast<Transfer *>(opaque);

    if (size && count > SIZE_MAX / size) {
        return 0;
    }

    std::string_view line(data, size * count);
    constexpr std::string_view key = "grpc-status:";

    if (line.starts_with(key)) {
        line.remove_prefix(key.size());

        while (!line.empty() && line.front() == ' ') {
            line.remove_prefix(1);
        }

        unsigned status{};
        auto parsed = std::from_chars(line.data(), line.data() + line.size(), status);

        if (parsed.ec == std::errc{}) {
            state.grpc_status = status;
        }
    }

    return size * count;
}

int progress(void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto &state = *static_cast<Transfer *>(opaque);

    return state.stop && state.stop->load() ? 1 : 0;
}

void perform(const Connection &connection, std::string_view method,
             std::span<const std::uint8_t> payload, Transfer &state, bool streaming,
             CURL *reused = nullptr, int timeout_ms = 1500) {
    initialize();

    if (connection.port < 1024 || connection.port > 65535 || connection.bearer.empty() ||
        connection.bearer.size() > 4096 ||
        connection.bearer.find_first_of("\r\n") != std::string::npos) {
        throw VoyemuError("MEDIA_CONNECTION", "invalid local credential binding");
    }

    auto body = grpc_message(payload);
    auto url = "http://127.0.0.1:" + std::to_string(connection.port) +
               "/android.emulation.control.EmulatorController/" + std::string(method);
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> owned(reused ? nullptr : curl_easy_init(),
                                                              curl_easy_cleanup);
    CURL *curl = reused ? reused : owned.get();

    if (!curl) {
        throw VoyemuError("MEDIA_CURL", "request allocation failed");
    }

    curl_easy_reset(curl);
    curl_slist *list = nullptr;

    for (const auto &value : std::vector<std::string>{
                 "content-type: application/grpc", "te: trailers", "grpc-accept-encoding: identity",
                 "authorization: Bearer " + connection.bearer}) {
        list = curl_slist_append(list, value.c_str());
    }

    std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_list(list,
                                                                            curl_slist_free_all);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION,
                     static_cast<long>(CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 1500L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headers);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
    curl_easy_setopt(curl, CURLOPT_TCP_NODELAY, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &state);

    if (!streaming) {
        if (timeout_ms < 1 || timeout_ms > 10000) {
            throw VoyemuError("MEDIA_TIMEOUT", "expected 1..10000 ms");
        }

        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
    }

    const auto result = curl_easy_perform(curl);
    long response{};
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);

    if (state.stop && state.stop->load()) {
        return;
    }

    if (!state.error.empty()) {
        throw std::runtime_error(state.error);
    }

    if (result != CURLE_OK || response != 200 || !state.grpc_status || *state.grpc_status != 0 ||
        state.frames.incomplete()) {
        const auto name = method == "getScreenshot" || method == "streamScreenshot" ||
                                          method == "sendTouch" || method == "sendKey"
                                  ? method
                                  : std::string_view("unknown");
        throw VoyemuError(
                "MEDIA_RPC_UNKNOWN",
                "local SDK request did not complete method=" + std::string(name) +
                        " curl=" + std::to_string(static_cast<int>(result)) +
                        " http=" + std::to_string(response) +
                        " grpc_present=" + (state.grpc_status ? "1" : "0") + " grpc=" +
                        (state.grpc_status ? std::to_string(*state.grpc_status) : "none") +
                        " incomplete=" + (state.frames.incomplete() ? "1" : "0") +
                        " messages=" + std::to_string(state.messages));
    }
}

std::string owned_ini(const std::filesystem::path &path) {
    const auto file = os::read_owned_file(path, 32768);

    switch (file.status) {
    case os::OwnedFile::Status::read:
        return file.bytes;
    case os::OwnedFile::Status::absent:
        throw VoyemuError("MEDIA_DISCOVERY", "SDK descriptor unavailable");
    case os::OwnedFile::Status::unverifiable:
        throw VoyemuError("MEDIA_DISCOVERY", "SDK descriptor ownership unavailable");
    case os::OwnedFile::Status::truncated:
        throw VoyemuError("MEDIA_DISCOVERY", "truncated SDK descriptor");
    case os::OwnedFile::Status::foreign:
        break;
    }

    throw VoyemuError("MEDIA_DISCOVERY", "invalid SDK descriptor ownership");
}
} // namespace

Connection read_connection(const std::filesystem::path &path, std::string_view session_id) {
    const auto record = read_private_json(path, 4096);

    if (record.at("schema") != 1 || record.at("session_id").get<std::string>() != session_id) {
        throw VoyemuError("MEDIA_CONNECTION", "stale host session");
    }

    const auto pid = record.at("vm_pid").get<unsigned>();
    auto discovery = std::filesystem::path(record.at("discovery").get<std::string>());

    if (!discovery.is_absolute() || discovery.filename() != "pid_" + std::to_string(pid) + ".ini") {
        throw VoyemuError("MEDIA_DISCOVERY", "descriptor identity differs");
    }

    std::map<std::string, std::string> values;
    std::istringstream input(owned_ini(discovery));
    std::string line;

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto at = line.find('=');

        if (at != std::string::npos) {
            auto key = line.substr(0, at);

            if (!values.emplace(key, line.substr(at + 1)).second) {
                throw VoyemuError("MEDIA_DISCOVERY", "duplicate SDK field");
            }
        }
    }

    const auto port = record.at("grpc_port").get<unsigned>(),
               serial = record.at("serial_port").get<unsigned>();

    if (values["grpc.port"] != std::to_string(port) ||
        values["port.serial"] != std::to_string(serial) || values["grpc.token"].empty()) {
        throw VoyemuError("MEDIA_DISCOVERY", "SDK port binding differs");
    }

    return {port, values["grpc.token"], path.parent_path() / "media-frames"};
}

struct MappedFrame::Impl {
    std::filesystem::path path;
    std::size_t bytes{};
    os::PrivateRegion region;

    Impl(const std::filesystem::path &directory, std::size_t extent) : bytes(extent) {
        if (!directory.is_absolute()) {
            throw VoyemuError("MEDIA_MEMORY", "absolute owned directory required");
        }

        if (bytes == 0 || bytes > maximum_pixel_bytes) {
            throw VoyemuError("MEDIA_MEMORY", "bounded screen frame required");
        }

        ensure_private_directory(directory);
        path = directory / ("frame-" + random_token() + ".rgba");
        using Creation = os::PrivateRegion::Creation;

        switch (region.create(path, bytes)) {
        case Creation::created:
            return;
        case Creation::create_failed:
            throw VoyemuError("MEDIA_MEMORY", "private region creation failed");
        case Creation::extent_failed:
            throw VoyemuError("MEDIA_MEMORY", "region extent failed");
        case Creation::ownership_failed:
            throw VoyemuError("MEDIA_MEMORY", "region ownership failed");
        case Creation::mapping_failed:
            break;
        }

        throw VoyemuError("MEDIA_MEMORY", "mapping failed");
    }

    void validate() const {
        if (!region.current()) {
            throw VoyemuError("MEDIA_MEMORY", "mapped region identity changed");
        }
    }
};

MappedFrame::MappedFrame(const std::filesystem::path &directory, std::size_t bytes)
    : impl_(std::make_unique<Impl>(directory, bytes)) {}

MappedFrame::~MappedFrame() = default;

std::string MappedFrame::handle() const {
    impl_->validate();
    const auto name = impl_->path.generic_string();

    // A Windows drive path gains the slash that a POSIX absolute path already has.
    return "file://" + (name.starts_with('/') ? name : "/" + name);
}

std::span<const std::uint8_t> MappedFrame::pixels() const {
    impl_->validate();

    return {static_cast<const std::uint8_t *>(impl_->region.data()), impl_->bytes};
}

ScreenshotStream::ScreenshotStream(Connection connection, GuestDisplay display, bool mapped)
    : connection_(std::move(connection)), display_(display),
      mapped_(mapped && !connection_.frame_directory.empty()) {
    (void)image_request(display);
}

ScreenshotStream::~ScreenshotStream() = default;

void ScreenshotStream::run(const std::atomic<bool> &stop, std::function<void(Image)> receiver) {
    if (stop.load()) {
        return;
    }

    if (mapped_) {
        try {
            if (!frame_) {
                frame_ = std::make_unique<MappedFrame>(connection_.frame_directory,
                                                       display_.pixel_bytes());
            }

            if (!notification_) {
                notification_ = std::make_unique<MappedFrame>(connection_.frame_directory,
                                                              display_.pixel_bytes());
            }

            const auto handle = frame_->handle(), notification_handle = notification_->handle();
            const auto request = image_request(display_, handle),
                       notifications = frame_change_request(display_, notification_handle);
            initialize();
            std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> reused(curl_easy_init(),
                                                                       curl_easy_cleanup);

            if (!reused) {
                throw VoyemuError("MEDIA_CURL", "request allocation failed");
            }

            bool observed = false;
            // The streaming region only signals changes and is never read. Its writer may race.
            Transfer changed([&](Bytes metadata) {
                if (stop.load() ||
                    !decode_frame_change(std::move(metadata), display_, notification_handle)) {
                    return;
                }

                Transfer capture([&](Bytes value) {
                    auto image = decode_mapped_image(std::move(value), display_, frame_->pixels(),
                                                     handle);

                    if (image) {
                        if (!observed) {
                            logging::write(logging::Level::info, "media", "transport.mmap_ready",
                                           {{"display", display_.id}, {"change_driven", true}});
                            observed = true;
                        }

                        receiver(std::move(*image));
                    }
                });
                capture.stop = &stop;
                frame_->handle();
                try {
                    perform(connection_, "getScreenshot", request, capture, false, reused.get());
                } catch (...) {
                    if (stop.load()) {
                        return;
                    }

                    if (!capture.error.empty() || capture.grpc_status != 9) {
                        throw;
                    }
                }
            });
            changed.stop = &stop;
            perform(connection_, "streamScreenshot", notifications, changed, true);

            return;
        } catch (const std::exception &) {
            if (stop.load()) {
                return;
            }

            mapped_ = false;
            logging::write(logging::Level::info, "media", "transport.raw_fallback");
        }
    }

    stream_images(connection_, display_, stop, std::move(receiver));
}

void stream_images(const Connection &connection, GuestDisplay display,
                   const std::atomic<bool> &stop, std::function<void(Image)> receiver) {
    Transfer transfer([&](Bytes value) {
        auto frame = decode_image(std::move(value), display);

        if (frame) {
            receiver(std::move(*frame));
        }
    });

    transfer.stop = &stop;
    perform(connection, "streamScreenshot", image_request(display), transfer, true);
}

Image capture_image(const Connection &connection, GuestDisplay display, int timeout_ms) {
    if (timeout_ms < 1 || timeout_ms > 10000) {
        throw VoyemuError("MEDIA_TIMEOUT", "expected 1..10000 ms");
    }

    std::optional<Image> image;
    unsigned received = 0;
    Transfer transfer([&](Bytes value) {
        ++received;
        image = decode_image(std::move(value), display);
    });
    perform(connection, "getScreenshot", image_request(display), transfer, false, nullptr,
            timeout_ms);

    if (received != 1 || !image) {
        throw VoyemuError("TEST_FRAME_UNAVAILABLE",
                          "SDK has not exported a complete original frame");
    }

    return std::move(*image);
}

void send_input(const Connection &connection, std::string_view method,
                std::span<const std::uint8_t> payload, int timeout_ms) {
    if (method != "sendTouch" && method != "sendKey") {
        throw VoyemuError("MEDIA_INPUT", "closed SDK method required");
    }

    unsigned received = 0;
    Transfer transfer([&](Bytes value) {
        if (!value.empty()) {
            throw VoyemuError("MEDIA_INPUT", "unexpected SDK reply");
        }

        ++received;
    });
    perform(connection, method, payload, transfer, false, nullptr, timeout_ms);

    if (received != 1) {
        throw VoyemuError("MEDIA_INPUT", "SDK receipt missing");
    }
}
} // namespace voyemu::media
