#include "error.hpp"
#include "model.hpp"
#include "process.hpp"
#include "runtime_fixture_media.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <nghttp2/nghttp2.h>
#include <string>
#include <thread>
#include <vector>

namespace {
class WindowsSdkMedia final : public FixtureSdkMedia {
    struct Exchange {
        WindowsSdkMedia *owner;
        std::string path, authorization;
        std::vector<std::uint8_t> request, response;
        std::size_t offset = 0;
        bool submitted = false, failed = false, complete = false;
    };
    SOCKET listener_ = INVALID_SOCKET;
    std::atomic<bool> closed_{false};
    std::atomic<unsigned> touches_{0};
    std::thread worker_;
    std::string token_;
    std::filesystem::path root_;

    static void number(std::vector<std::uint8_t> &out, std::uint64_t value) {
        do {
            const auto byte = static_cast<std::uint8_t>(value & 127);
            value >>= 7;
            out.push_back(byte | (value ? 128 : 0));
        } while (value);
    }

    static void integer(std::vector<std::uint8_t> &out, unsigned field, std::uint64_t value) {
        number(out, static_cast<std::uint64_t>(field) * 8);
        number(out, value);
    }

    static void bytes(std::vector<std::uint8_t> &out, unsigned field,
                      const std::vector<std::uint8_t> &value) {
        number(out, field * 8 + 2);
        number(out, value.size());
        out.insert(out.end(), value.begin(), value.end());
    }

    static nghttp2_nv header(const char *name, const char *value) {
        return {reinterpret_cast<std::uint8_t *>(const_cast<char *>(name)),
                reinterpret_cast<std::uint8_t *>(const_cast<char *>(value)), std::strlen(name),
                std::strlen(value), NGHTTP2_NV_FLAG_NONE};
    }

    static int receive_header(nghttp2_session *, const nghttp2_frame *, const std::uint8_t *name,
                              std::size_t name_size, const std::uint8_t *value,
                              std::size_t value_size, std::uint8_t, void *user) {
        auto &state = *static_cast<Exchange *>(user);

        if (name_size > 128 || value_size > 4096) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }

        const std::string_view key(reinterpret_cast<const char *>(name), name_size);

        if (key == ":path") {
            state.path.assign(reinterpret_cast<const char *>(value), value_size);
        } else if (key == "authorization") {
            state.authorization.assign(reinterpret_cast<const char *>(value), value_size);
        }

        return 0;
    }

    static int receive_data(nghttp2_session *, std::uint8_t, int32_t, const std::uint8_t *bytes,
                            std::size_t size, void *user) {
        auto &state = *static_cast<Exchange *>(user);

        if (state.request.size() + size > 4096) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }

        state.request.insert(state.request.end(), bytes, bytes + size);

        return 0;
    }

    static ssize_t send_body(nghttp2_session *session, int32_t stream, std::uint8_t *output,
                             std::size_t size, std::uint32_t *flags, nghttp2_data_source *source,
                             void *) {
        auto &state = *static_cast<Exchange *>(source->ptr);
        const auto count = std::min(size, state.response.size() - state.offset);

        std::memcpy(output, state.response.data() + state.offset, count);
        state.offset += count;

        if (state.offset == state.response.size()) {
            *flags = NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
            auto trailer = header("grpc-status", "0");

            if (nghttp2_submit_trailer(session, stream, &trailer, 1)) {
                return NGHTTP2_ERR_CALLBACK_FAILURE;
            }
        }

        return static_cast<ssize_t>(count);
    }

    static int stream_closed(nghttp2_session *, int32_t, std::uint32_t error, void *user) {
        auto &state = *static_cast<Exchange *>(user);

        state.complete = error == NGHTTP2_NO_ERROR;
        state.failed = error != NGHTTP2_NO_ERROR;

        return 0;
    }

    static int receive_frame(nghttp2_session *session, const nghttp2_frame *frame, void *user) {
        auto &state = *static_cast<Exchange *>(user);

        if (!((frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA) &&
              (frame->hd.flags & NGHTTP2_FLAG_END_STREAM))) {
            return 0;
        }

        try {
            if (state.submitted || state.authorization != "Bearer " + state.owner->token_ ||
                state.request.size() < 5 || state.request[0] != 0) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK request binding failed");
            }

            const auto size = (std::uint32_t(state.request[1]) << 24) |
                              (std::uint32_t(state.request[2]) << 16) |
                              (std::uint32_t(state.request[3]) << 8) | state.request[4];

            if (size != state.request.size() - 5) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK request extent failed");
            }

            std::vector<std::uint8_t> payload;
            unsigned display = 0;

            if (state.path == "/android.emulation.control.EmulatorController/getScreenshot") {
                bool matched = false;

                for (unsigned candidate = 0; candidate < 2; ++candidate) {
                    std::vector<std::uint8_t> descriptor;
                    integer(descriptor, 1, 1);
                    integer(descriptor, 3, 1920);
                    integer(descriptor, 4, 720);
                    integer(descriptor, 5, candidate);

                    if (descriptor.size() == size &&
                        std::equal(descriptor.begin(), descriptor.end(),
                                   state.request.begin() + 5)) {
                        display = candidate;
                        matched = true;
                        break;
                    }
                }

                if (!matched) {
                    throw voyemu::VoyemuError("FIXTURE",
                                              "fixture SDK original display request required");
                }

                std::vector<std::uint8_t> descriptor;
                integer(descriptor, 1, 1);
                integer(descriptor, 3, 1920);
                integer(descriptor, 4, 720);
                integer(descriptor, 5, display);
                bytes(payload, 1, descriptor);
                std::vector<std::uint8_t> pixels(static_cast<std::size_t>(1920u * 720u) * 4u);
                const auto effect = state.owner->touches_.load();

                for (std::size_t at = 0; at < pixels.size(); at += 4) {
                    pixels[at + display] = static_cast<std::uint8_t>(80 + (effect % 8) * 16);
                    pixels[at + 3] = 255;
                }

                bytes(payload, 4, pixels);
                integer(payload, 5, effect + 1);
                integer(payload, 6,
                        static_cast<std::uint64_t>(
                                std::chrono::duration_cast<std::chrono::microseconds>(
                                        std::chrono::system_clock::now().time_since_epoch())
                                        .count()));
            } else if (state.path == "/android.emulation.control.EmulatorController/sendTouch") {
                if (size < 8 || size > 128) {
                    throw voyemu::VoyemuError("FIXTURE", "fixture SDK bounded touch required");
                }

                const auto count = ++state.owner->touches_;
                voyemu::write_private_json(
                        state.owner->root_ / "fixture-sdk-touch.json",
                        {{"schema", 1}, {"touch_calls", count}, {"synthetic_fixture", true}});
            } else if (state.path != "/android.emulation.control.EmulatorController/sendKey") {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK method not allowed");
            }

            state.response.resize(5);

            for (unsigned i = 0; i < 4; ++i) {
                state.response[4 - i] = static_cast<std::uint8_t>(payload.size() >> (i * 8));
            }

            state.response.insert(state.response.end(), payload.begin(), payload.end());
            std::array<nghttp2_nv, 2> headers{header(":status", "200"),
                                              header("content-type", "application/grpc")};
            nghttp2_data_provider provider{};
            provider.source.ptr = &state;
            provider.read_callback = send_body;

            if (nghttp2_submit_response(session, frame->hd.stream_id, headers.data(),
                                        headers.size(), &provider)) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK response unavailable");
            }

            state.submitted = true;

            return 0;
        } catch (...) {
            state.failed = true;

            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    }

    void connection(SOCKET client) {
        DWORD timeout = 1000;

        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout),
                   sizeof timeout);
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout),
                   sizeof timeout);
        Exchange state{};
        state.owner = this;
        nghttp2_session_callbacks *callbacks = nullptr;

        if (nghttp2_session_callbacks_new(&callbacks)) {
            throw voyemu::VoyemuError("FIXTURE", "fixture SDK callbacks unavailable");
        }

        nghttp2_session_callbacks_set_on_header_callback(callbacks, receive_header);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, receive_data);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, receive_frame);
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, stream_closed);
        nghttp2_session *session = nullptr;
        const auto created = nghttp2_session_server_new(&session, callbacks, &state);
        nghttp2_session_callbacks_del(callbacks);

        if (created) {
            throw voyemu::VoyemuError("FIXTURE", "fixture SDK session unavailable");
        }

        struct Close {
            nghttp2_session *value;

            ~Close() {
                nghttp2_session_del(value);
            }
        } close{session};
        nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::array<std::uint8_t, 65536> input{};

        while (!closed_ && std::chrono::steady_clock::now() < deadline) {
            const std::uint8_t *bytes = nullptr;
            ssize_t length = 0;

            while ((length = nghttp2_session_mem_send(session, &bytes)) > 0) {
                ssize_t offset = 0;

                while (offset < length) {
                    const auto count = send(client, reinterpret_cast<const char *>(bytes + offset),
                                            static_cast<int>(length - offset), 0);

                    if (count <= 0) {
                        throw voyemu::VoyemuError("FIXTURE", "fixture SDK send failed");
                    }

                    offset += count;
                }
            }

            if (length < 0 || state.failed) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK protocol failed");
            }

            if (state.complete) {
                return;
            }

            const auto count = recv(client, reinterpret_cast<char *>(input.data()),
                                    static_cast<int>(input.size()), 0);

            if (count <= 0) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK read failed");
            }

            if (nghttp2_session_mem_recv(session, input.data(), count) != count) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK frame failed");
            }
        }

        if (!closed_) {
            throw voyemu::VoyemuError("FIXTURE", "fixture SDK finite response deadline");
        }
    }

    void serve() {
        while (!closed_) {
            fd_set selected;
            FD_ZERO(&selected);
            FD_SET(listener_, &selected);
            timeval wait{0, 100000};
            const auto ready = select(0, &selected, nullptr, nullptr, &wait);

            if (ready <= 0) {
                continue;
            }

            const auto client = accept(listener_, nullptr, nullptr);

            if (client == INVALID_SOCKET) {
                continue;
            }

            try {
                connection(client);
                u_long unread = 0;
                ioctlsocket(client, FIONREAD, &unread);
                const auto sent = shutdown(client, SD_SEND);
                DWORD timeout = 200;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char *>(&timeout), sizeof timeout);
                std::size_t drained = 0;
                std::array<char, 4096> buffer{};
                const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);

                while (std::chrono::steady_clock::now() < end) {
                    const auto count = recv(client, buffer.data(), static_cast<int>(buffer.size()),
                                            0);

                    if (count <= 0) {
                        break;
                    }

                    drained += static_cast<std::size_t>(count);
                }

                voyemu::write_private_json(root_ / "fixture-sdk-media-close.json",
                                           {{"schema", 1},
                                            {"queued_read_bytes", unread},
                                            {"drained_bytes", drained},
                                            {"send_shutdown", sent == 0}});
            } catch (...) {
                try {
                    voyemu::write_private_json(
                            root_ / "fixture-sdk-media-error.json",
                            {{"schema", 1}, {"error", "bounded_fixture_sdk_request_failed"}});
                } catch (...) { /* Ignored: the fixture error record is best-effort. */
                }
            }

            closesocket(client);
        }
    }

public:
    WindowsSdkMedia(const std::filesystem::path &root, unsigned port, unsigned serial,
                    std::int64_t pid)
        : token_(voyemu::random_token()), root_(root) {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<unsigned short>(port));

        if (listener_ == INVALID_SOCKET ||
            bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address) ||
            listen(listener_, 4)) {
            throw voyemu::VoyemuError("FIXTURE", "fixture SDK gRPC listener failed");
        }

        const char *local = std::getenv("LOCALAPPDATA");

        if (!local) {
            throw voyemu::VoyemuError("FIXTURE", "fixture local application directory missing");
        }

        const auto discovery = std::filesystem::path(local) / "Temp/avd/running";
        voyemu::ensure_private_directory(discovery);
        const auto descriptor = discovery / ("pid_" + std::to_string(pid) + ".ini");
        {
            std::ofstream file(descriptor);
            file << "port.serial=" << serial << "\ngrpc.port=" << port << "\ngrpc.token=" << token_
                 << '\n';

            if (!file) {
                throw voyemu::VoyemuError("FIXTURE", "fixture SDK descriptor failed");
            }
        }
        std::cout << "Advertising in: " << std::filesystem::canonical(descriptor).string()
                  << std::endl;
        worker_ = std::thread([this] {
            serve();
        });
    }

    ~WindowsSdkMedia() override {
        closed_ = true;

        if (worker_.joinable()) {
            worker_.join();
        }

        if (listener_ != INVALID_SOCKET) {
            closesocket(listener_);
        }
    }
};
} // namespace

std::unique_ptr<FixtureSdkMedia> start_fixture_sdk_media(const std::filesystem::path &root,
                                                         unsigned grpc, unsigned serial,
                                                         std::int64_t pid) {
    if (grpc < 1024 || grpc > 65535) {
        throw voyemu::VoyemuError("FIXTURE", "fixture SDK control port missing");
    }

    return std::make_unique<WindowsSdkMedia>(root, grpc, serial, pid);
}
