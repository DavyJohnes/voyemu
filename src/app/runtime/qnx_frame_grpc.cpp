#include "qnx_frame_grpc.hpp"
#include "check.hpp"
#include "error.hpp"
#include "frame_transport.hpp"
#include "os/net.hpp"
#include "qnx_frame_grpc_wire.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <nghttp2/nghttp2.h>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace voyemu::frames {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr unsigned max_clients = 4, max_streams = 2;
constexpr auto frame_interval = 33ms, io_interval = 100ms;
constexpr auto io_timeout = 5s;
constexpr std::string_view get_method =
        "/android.emulation.control.EmulatorController/getScreenshot";
constexpr std::string_view stream_method =
        "/android.emulation.control.EmulatorController/streamScreenshot";

#ifdef _WIN32
using Descriptor = SOCKET;
using AddressSize = int;
constexpr Descriptor invalid_descriptor = INVALID_SOCKET;
struct Network {
    Network() {
        WSADATA data{};

        if (::WSAStartup(MAKEWORD(2, 2), &data)) {
            throw VoyemuError("QNX_FRAME_GRPC", "Winsock unavailable");
        }
    }

    ~Network() {
        ::WSACleanup();
    }
};

void close_socket(Descriptor socket) {
    ::closesocket(socket);
}

bool retry_io() {
    const auto error = ::WSAGetLastError();

    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}

bool set_socket_flags(Descriptor socket) {
    u_long nonblocking = 1;

    return ::ioctlsocket(socket, FIONBIO, &nonblocking) == 0 &&
           ::SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);
}

bool exclusive_listener(Descriptor socket) {
    const int one = 1;

    return ::setsockopt(socket, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                        reinterpret_cast<const char *>(&one), sizeof(one)) == 0;
}

int wait_socket(Descriptor socket, bool writing, std::chrono::milliseconds timeout) {
    fd_set read{}, write{};

    FD_SET(socket, &read);

    if (writing) {
        FD_SET(socket, &write);
    }

    timeval budget{0, static_cast<long>(timeout.count() * 1000)};

    return ::select(0, &read, writing ? &write : nullptr, nullptr, &budget);
}
#else
using Descriptor = int;
using AddressSize = socklen_t;
constexpr Descriptor invalid_descriptor = -1;
struct Network {};

void close_socket(Descriptor socket) {
    ::close(socket);
}

bool retry_io() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
}

bool set_socket_flags(Descriptor socket) {
    return ::fcntl(socket, F_SETFL, O_NONBLOCK) == 0 && ::fcntl(socket, F_SETFD, FD_CLOEXEC) == 0 &&
           os::suppress_sigpipe(socket);
}

bool exclusive_listener(Descriptor) {
    return true;
}

int wait_socket(Descriptor socket, bool writing, std::chrono::milliseconds timeout) {
    pollfd ready{socket, static_cast<short>(POLLIN | (writing ? POLLOUT : 0)), 0};

    return ::poll(&ready, 1, static_cast<int>(timeout.count()));
}
#endif

struct Socket {
    explicit Socket(Descriptor value) : value(value) {}

    ~Socket() {
        if (value != invalid_descriptor) {
            close_socket(value);
        }
    }

    Socket(Socket &&other) noexcept : value(std::exchange(other.value, invalid_descriptor)) {}

    Socket(const Socket &) = delete;
    Descriptor value;
};

nghttp2_nv header(std::string_view name, std::string_view value) {
    return {reinterpret_cast<std::uint8_t *>(const_cast<char *>(name.data())),
            reinterpret_cast<std::uint8_t *>(const_cast<char *>(value.data())), name.size(),
            value.size(), NGHTTP2_NV_FLAG_NONE};
}

bool credential_matches(std::string_view value, std::string_view expected) {
    if (value.size() != expected.size()) {
        return false;
    }

    unsigned difference = 0;

    for (std::size_t i = 0; i < value.size(); ++i) {
        difference |= static_cast<unsigned char>(value[i]) ^
                      static_cast<unsigned char>(expected[i]);
    }

    return difference == 0;
}

enum class Method {
    unknown,
    get,
    stream
};

struct Exchange {
    Method method = Method::unknown;
    bool post = false, grpc = false, auth_seen = false, authorized = false;
    bool malformed = false, submitted = false, deferred = false;
    std::size_t header_size = 0, sent = 0;
    std::uint32_t sequence = 0;
    std::optional<std::uint64_t> last_sample;
    Clock::time_point progress = Clock::now();
    media::Bytes request, reply;
    std::optional<grpc_wire::Request> subscription;
};

class Connection {
public:
    Connection(Descriptor socket, const std::filesystem::path &frame,
               const std::string &authorization)
        : socket_(socket), frame_path_(frame), authorization_(authorization) {}

    void run(std::stop_token stop) {
        nghttp2_session_callbacks *callbacks = nullptr;

        if (nghttp2_session_callbacks_new(&callbacks)) {
            return;
        }

        const std::unique_ptr<nghttp2_session_callbacks, decltype(&nghttp2_session_callbacks_del)>
                callback_owner(callbacks, nghttp2_session_callbacks_del);
        nghttp2_session_callbacks_set_send_callback(callbacks, send);
        nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks, begin_headers);
        nghttp2_session_callbacks_set_on_header_callback(callbacks, receive_header);
        nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, receive_body);
        nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, receive_frame);
        nghttp2_session_callbacks_set_on_stream_close_callback(callbacks, close_stream);
        nghttp2_session *session = nullptr;

        if (nghttp2_session_server_new(&session, callbacks, this)) {
            return;
        }

        session_.reset(session);
        const std::array<nghttp2_settings_entry, 2> settings{{
                {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, max_streams},
                {NGHTTP2_SETTINGS_MAX_HEADER_LIST_SIZE, 8192},
        }};

        if (nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, settings.data(), settings.size())) {
            return;
        }

        std::array<std::uint8_t, 16384> input{};
        const auto accepted = Clock::now();
        auto last_read = accepted;
        auto next_sample = last_read;

        while (!stop.stop_requested()) {
            const auto now = Clock::now();
            const bool streaming = std::any_of(streams_.begin(), streams_.end(),
                                               [](const auto &entry) {
                                                   return entry.second.subscription.has_value();
                                               });

            if ((!authenticated_ && now - accepted > io_timeout) ||
                (!streaming && now - last_read > io_timeout) ||
                std::any_of(streams_.begin(), streams_.end(), [now](const auto &entry) {
                    const auto &exchange = entry.second;

                    return (!exchange.submitted || !exchange.reply.empty()) &&
                           now - exchange.progress > io_timeout;
                })) {
                return;
            }

            if (streaming && now >= next_sample) {
                update_subscriptions();
                next_sample = now + frame_interval;
            }

            write_blocked_ = false;

            if (nghttp2_session_send(session) ||
                (!nghttp2_session_want_read(session) && !nghttp2_session_want_write(session))) {
                return;
            }

            const auto ready = wait_socket(socket_, write_blocked_,
                                           streaming ? frame_interval : io_interval);

            if (ready < 0 && !retry_io()) {
                return;
            }

            if (ready <= 0) {
                continue;
            }

            const auto count = ::recv(socket_, reinterpret_cast<char *>(input.data()),
                                      static_cast<int>(input.size()), 0);

            if (count < 0 && retry_io()) {
                continue;
            }

            if (count <= 0 || nghttp2_session_mem_recv(session, input.data(), count) != count) {
                return;
            }

            last_read = Clock::now();
        }
    }

private:
    static ssize_t send(nghttp2_session *, const std::uint8_t *data, std::size_t size, int,
                        void *context) noexcept {
        auto &connection = *static_cast<Connection *>(context);
        const auto count = ::send(connection.socket_, reinterpret_cast<const char *>(data),
                                  static_cast<int>(size), 0);

        if (count > 0) {
            return count;
        }

        if (count < 0 && retry_io()) {
            connection.write_blocked_ = true;

            return NGHTTP2_ERR_WOULDBLOCK;
        }

        return NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    static int begin_headers(nghttp2_session *, const nghttp2_frame *frame,
                             void *context) noexcept {
        auto &connection = *static_cast<Connection *>(context);

        if (frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }

        if (connection.streams_.size() >= max_streams) {
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }

        try {
            connection.streams_.try_emplace(frame->hd.stream_id);
        } catch (...) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }

        return 0;
    }

    static int receive_header(nghttp2_session *, const nghttp2_frame *frame,
                              const std::uint8_t *name, std::size_t name_size,
                              const std::uint8_t *value, std::size_t value_size, std::uint8_t,
                              void *context) noexcept {
        auto &streams = static_cast<Connection *>(context)->streams_;
        const auto stream = streams.find(frame->hd.stream_id);

        if (stream == streams.end() || frame->headers.cat != NGHTTP2_HCAT_REQUEST) {
            return 0;
        }

        auto &exchange = stream->second;

        if (name_size > 128 || value_size > 4096 ||
            exchange.header_size + name_size + value_size > 8192) {
            return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
        }

        exchange.header_size += name_size + value_size;
        const std::string_view field(reinterpret_cast<const char *>(name), name_size);
        const std::string_view text(reinterpret_cast<const char *>(value), value_size);

        if (field == ":method") {
            exchange.post = text == "POST";
        }

        if (field == "content-type") {
            exchange.grpc = text == "application/grpc" || text == "application/grpc+proto";
        }

        if (field == ":path") {
            exchange.method = text == get_method      ? Method::get
                              : text == stream_method ? Method::stream
                                                      : Method::unknown;
        }

        if (field == "authorization") {
            const auto &expected = static_cast<Connection *>(context)->authorization_;
            exchange.authorized = !exchange.auth_seen && credential_matches(text, expected);
            exchange.auth_seen = true;
        }

        return 0;
    }

    static int receive_body(nghttp2_session *, std::uint8_t, int32_t id, const std::uint8_t *data,
                            std::size_t size, void *context) noexcept {
        auto &streams = static_cast<Connection *>(context)->streams_;
        const auto stream = streams.find(id);

        if (stream == streams.end()) {
            return 0;
        }

        auto &exchange = stream->second;

        if (exchange.request.size() + size > grpc_wire::maximum_request_size + 5) {
            exchange.malformed = true;

            return 0;
        }

        try {
            exchange.request.insert(exchange.request.end(), data, data + size);
        } catch (...) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }

        return 0;
    }

    static int receive_frame(nghttp2_session *, const nghttp2_frame *frame,
                             void *context) noexcept {
        if (!(frame->hd.flags & NGHTTP2_FLAG_END_STREAM) ||
            (frame->hd.type != NGHTTP2_HEADERS && frame->hd.type != NGHTTP2_DATA)) {
            return 0;
        }

        try {
            return static_cast<Connection *>(context)->respond(frame->hd.stream_id);
        } catch (...) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    }

    static int close_stream(nghttp2_session *, int32_t id, std::uint32_t, void *context) noexcept {
        static_cast<Connection *>(context)->streams_.erase(id);

        return 0;
    }

    static ssize_t send_body(nghttp2_session *session, int32_t id, std::uint8_t *output,
                             std::size_t size, std::uint32_t *flags, nghttp2_data_source *source,
                             void *) noexcept {
        auto &exchange = *static_cast<Exchange *>(source->ptr);

        if (exchange.reply.empty()) {
            exchange.deferred = true;

            return NGHTTP2_ERR_DEFERRED;
        }

        const auto count = std::min(size, exchange.reply.size() - exchange.sent);
        std::memcpy(output, exchange.reply.data() + exchange.sent, count);
        exchange.sent += count;
        exchange.progress = Clock::now();

        if (exchange.sent == exchange.reply.size()) {
            exchange.reply.clear();
            exchange.sent = 0;

            if (exchange.method == Method::get) {
                *flags = NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
                const auto trailer = header("grpc-status", "0");

                if (nghttp2_submit_trailer(session, id, &trailer, 1)) {
                    return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
                }
            }
        }

        return static_cast<ssize_t>(count);
    }

    int respond(int32_t id) {
        const auto stream = streams_.find(id);

        if (stream == streams_.end()) {
            return 0;
        }

        auto &exchange = stream->second;

        if (exchange.submitted) {
            return 0;
        }

        exchange.submitted = true;
        const auto fail = [this, id](std::string_view status) {
            const std::array headers{header(":status", "200"),
                                     header("content-type", "application/grpc"),
                                     header("grpc-status", status)};

            return nghttp2_submit_headers(session_.get(), NGHTTP2_FLAG_END_STREAM, id, nullptr,
                                          headers.data(), headers.size(), nullptr) == 0
                           ? 0
                           : NGHTTP2_ERR_CALLBACK_FAILURE;
        };

        if (!exchange.authorized) {
            return fail("16");
        }

        authenticated_ = true;

        if (!exchange.post || !exchange.grpc || exchange.malformed || exchange.request.size() < 5 ||
            exchange.request[0] != 0) {
            return fail("3");
        }

        if (exchange.method == Method::unknown) {
            return fail("12");
        }

        std::uint32_t length = 0;

        for (unsigned i = 1; i <= 4; ++i) {
            length = (length << 8) | exchange.request[i];
        }

        if (length != exchange.request.size() - 5) {
            return fail("3");
        }

        grpc_wire::Request request;
        try {
            request = grpc_wire::parse_request(std::span(exchange.request).subspan(5));
        } catch (const std::runtime_error &) {
            return fail("3");
        }

        if (exchange.method == Method::stream) {
            exchange.subscription = request;
        } else {
            refresh();
            exchange.reply = available_ ? grpc_wire::encode_frame(request, frame_, 0)
                                        : media::grpc_message({});
            exchange.progress = Clock::now();
        }

        const std::array headers{header(":status", "200"),
                                 header("content-type", "application/grpc")};
        nghttp2_data_provider provider{};
        provider.source.ptr = &exchange;
        provider.read_callback = send_body;

        return nghttp2_submit_response(session_.get(), id, headers.data(), headers.size(),
                                       &provider) == 0
                       ? 0
                       : NGHTTP2_ERR_CALLBACK_FAILURE;
    }

    void refresh() {
        if (!reader_) {
            reader_ = std::make_unique<Reader>(frame_path_);
        }

        const bool was_available = available_;
        try {
            if (reader_->read_latest(frame_, available_ ? frame_.sequence : 0)) {
                available_ = true;
                ++sample_;
            } else if (reader_->invalidated()) {
                available_ = false;
            }
        } catch (const std::runtime_error &) {
            // The worker can still be creating the backing, or replacing a failed producer.
            reader_.reset();
            available_ = false;
        }

        if (was_available && !available_) {
            ++sample_;
        }
    }

    void update_subscriptions() {
        const auto ready = [](const auto &entry) {
            return entry.second.subscription && entry.second.reply.empty();
        };

        if (!std::any_of(streams_.begin(), streams_.end(), ready)) {
            return;
        }

        refresh();

        for (auto &entry : streams_) {
            auto &exchange = entry.second;

            if (!ready(entry) || exchange.last_sample == sample_) {
                continue;
            }

            exchange.reply = available_ ? grpc_wire::encode_frame(exchange.subscription.value(),
                                                                  frame_, exchange.sequence++)
                                        : media::grpc_message({});
            exchange.last_sample = sample_;
            exchange.progress = Clock::now();

            if (exchange.deferred) {
                if (nghttp2_session_resume_data(session_.get(), entry.first)) {
                    throw VoyemuError("QNX_FRAME_GRPC", "stream resume failed");
                }

                exchange.deferred = false;
            }
        }
    }

    Descriptor socket_;
    const std::filesystem::path &frame_path_;
    const std::string &authorization_;
    bool write_blocked_ = false, available_ = false, authenticated_ = false;
    std::uint64_t sample_ = 0;
    Frame frame_;
    std::unique_ptr<Reader> reader_;
    std::map<int32_t, Exchange> streams_;
    std::unique_ptr<nghttp2_session, decltype(&nghttp2_session_del)> session_{nullptr,
                                                                              nghttp2_session_del};
};
} // namespace

struct GrpcServer::Impl {
    Impl(const std::filesystem::path &frame, std::string token)
        : frame(frame), authorization("Bearer " + token),
          listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) {
        if (!hex_text(token, 32)) {
            throw VoyemuError("QNX_FRAME_GRPC", "invalid session credential");
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        AddressSize address_size = sizeof(address);

        if (listener.value == invalid_descriptor || !set_socket_flags(listener.value) ||
            !exclusive_listener(listener.value) ||
            ::bind(listener.value, reinterpret_cast<const sockaddr *>(&address), address_size) ||
            ::getsockname(listener.value, reinterpret_cast<sockaddr *>(&address), &address_size) ||
            ::listen(listener.value, max_clients)) {
            throw VoyemuError("QNX_FRAME_GRPC", "loopback listener unavailable");
        }

        port = ntohs(address.sin_port);
        worker = std::jthread([this](std::stop_token stop) {
            serve(stop);
        });
    }

    void serve(std::stop_token stop) noexcept {
        struct Client {
            std::atomic<bool> done = false;
            std::jthread thread;
        };
        std::vector<std::unique_ptr<Client>> clients;
        try {
            while (!stop.stop_requested()) {
                std::erase_if(clients, [](const auto &client) {
                    return client->done.load();
                });
                const auto ready = wait_socket(listener.value, false, io_interval);

                if (ready < 0 && !retry_io()) {
                    break;
                }

                if (ready <= 0 || stop.stop_requested()) {
                    continue;
                }

                Socket peer(::accept(listener.value, nullptr, nullptr));

                if (peer.value == invalid_descriptor || clients.size() >= max_clients ||
                    !set_socket_flags(peer.value)) {
                    continue;
                }

                auto client = std::make_unique<Client>();
                client->thread = std::jthread([this, done = &client->done,
                                               socket = std::move(peer)](std::stop_token token) {
                    try {
                        Connection(socket.value, frame, authorization).run(token);
                    } catch (...) { /* Ignored: a failed connection cannot interrupt the QNX
                                       session. */
                    }

                    done->store(true);
                });
                clients.push_back(std::move(client));
            }
        } catch (...) { /* Ignored: existing clients are joined before the listener is released. */
        }

        for (const auto &client : clients) {
            client->thread.request_stop();
        }
    }

    Network network;
    const std::filesystem::path frame;
    const std::string authorization;
    Socket listener;
    unsigned port = 0;
    std::jthread worker;
};

GrpcServer::GrpcServer(const std::filesystem::path &frame, std::string token)
    : impl_(std::make_unique<Impl>(frame, std::move(token))) {}

GrpcServer::~GrpcServer() = default;

unsigned GrpcServer::port() const {
    return impl_->port;
}
} // namespace voyemu::frames
