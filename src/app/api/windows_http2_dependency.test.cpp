#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <curl/curl.h>
#include <exception>
#include <functional>
#include <iostream>
#include <nghttp2/nghttp2.h>
#include <string>
#include <string_view>
#include <thread>

namespace {
constexpr std::array<unsigned char, 5> message{}; // Empty uncompressed gRPC message.

nghttp2_nv header(const char *name, const char *value) {
    return {reinterpret_cast<std::uint8_t *>(const_cast<char *>(name)),
            reinterpret_cast<std::uint8_t *>(const_cast<char *>(value)), std::strlen(name),
            std::strlen(value), NGHTTP2_NV_FLAG_NONE};
}

struct Server {
    std::string request;
    std::size_t offset{};
    bool replied{};
    bool ok{};
};

ssize_t body(nghttp2_session *session, int32_t stream, std::uint8_t *output, std::size_t size,
             std::uint32_t *flags, nghttp2_data_source *source, void *) {
    auto &state = *static_cast<Server *>(source->ptr);
    const auto count = std::min(size, message.size() - state.offset);

    std::memcpy(output, message.data() + state.offset, count);
    state.offset += count;

    if (state.offset == message.size()) {
        *flags = NGHTTP2_DATA_FLAG_EOF | NGHTTP2_DATA_FLAG_NO_END_STREAM;
        auto trailer = header("grpc-status", "0");
        assert(nghttp2_submit_trailer(session, stream, &trailer, 1) == 0);
    }

    return static_cast<ssize_t>(count);
}

int data(nghttp2_session *, std::uint8_t, int32_t, const std::uint8_t *bytes, std::size_t size,
         void *user) {
    static_cast<Server *>(user)->request.append(reinterpret_cast<const char *>(bytes), size);

    return 0;
}

int frame(nghttp2_session *session, const nghttp2_frame *frame, void *user) {
    auto &state = *static_cast<Server *>(user);

    if ((frame->hd.type == NGHTTP2_HEADERS || frame->hd.type == NGHTTP2_DATA) &&
        (frame->hd.flags & NGHTTP2_FLAG_END_STREAM)) {
        assert(!state.replied &&
               state.request ==
                       std::string(reinterpret_cast<const char *>(message.data()), message.size()));
        state.replied = true;
        std::array<nghttp2_nv, 2> headers{header(":status", "200"),
                                          header("content-type", "application/grpc")};
        nghttp2_data_provider provider{};
        provider.source.ptr = &state;
        provider.read_callback = body;
        assert(nghttp2_submit_response(session, frame->hd.stream_id, headers.data(), headers.size(),
                                       &provider) == 0);
    }

    return 0;
}

void serve(SOCKET listener, Server &state) {
    SOCKET client = accept(listener, nullptr, nullptr);

    assert(client != INVALID_SOCKET);
    DWORD timeout = 5000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout),
               sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout),
               sizeof(timeout));
    nghttp2_session_callbacks *callbacks = nullptr;
    assert(nghttp2_session_callbacks_new(&callbacks) == 0);
    nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks, frame);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks, data);
    nghttp2_session *session = nullptr;
    assert(nghttp2_session_server_new(&session, callbacks, &state) == 0);
    nghttp2_session_callbacks_del(callbacks);
    assert(nghttp2_submit_settings(session, NGHTTP2_FLAG_NONE, nullptr, 0) == 0);
    auto flush = [&] {
        const std::uint8_t *bytes = nullptr;
        ssize_t size = 0;

        while ((size = nghttp2_session_mem_send(session, &bytes)) > 0) {
            ssize_t offset = 0;

            while (offset < size) {
                const auto count = send(client, reinterpret_cast<const char *>(bytes + offset),
                                        static_cast<int>(size - offset), 0);
                assert(count > 0);
                offset += count;
            }
        }

        assert(size == 0);
    };
    flush();
    std::array<std::uint8_t, 65536> input{};

    while (!state.replied) {
        const auto count = recv(client, reinterpret_cast<char *>(input.data()),
                                static_cast<int>(input.size()), 0);
        assert(count > 0);
        assert(nghttp2_session_mem_recv(session, input.data(), count) == count);
        flush();
    }

    state.ok = state.offset == message.size();
    nghttp2_session_del(session);
    // Keep the receive side open for pending client control frames. Closing
    // with unread data can reset TCP before the client reads the trailers.
    assert(shutdown(client, SD_SEND) == 0);

    while (recv(client, reinterpret_cast<char *>(input.data()), static_cast<int>(input.size()), 0) >
           0) {}

    closesocket(client);
}

size_t receive(char *bytes, size_t size, size_t count, void *user) {
    static_cast<std::string *>(user)->append(bytes, size * count);

    return size * count;
}

size_t headers(char *bytes, size_t size, size_t count, void *user) {
    if (std::string_view(bytes, size * count).starts_with("grpc-status: 0")) {
        *static_cast<bool *>(user) = true;
    }

    return size * count;
}
} // namespace

int main() try {
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    const auto *version = curl_version_info(CURLVERSION_NOW);
    assert(version && std::string_view(version->version) == "8.23.0" &&
           (version->features & CURL_VERSION_HTTP2) && !(version->features & CURL_VERSION_SSL));
    assert(version->nghttp2_version && std::string_view(version->nghttp2_version) == "1.70.0");
    assert(version->protocols && version->protocols[0] &&
           std::string_view(version->protocols[0]) == "http" && !version->protocols[1]);
    WSADATA winsock{};
    assert(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0);
    int length = sizeof(address);
    assert(getsockname(listener, reinterpret_cast<sockaddr *>(&address), &length) == 0 &&
           listen(listener, 1) == 0);
    Server state;
    std::thread server(serve, listener, std::ref(state));
    CURL *client = curl_easy_init();
    assert(client);
    std::string response;
    bool receipt = false;
    const auto url = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port)) +
                     "/android.emulation.control.EmulatorController/getScreenshot";
    curl_easy_setopt(client, CURLOPT_URL, url.c_str());
    curl_easy_setopt(client, CURLOPT_HTTP_VERSION,
                     static_cast<long>(CURL_HTTP_VERSION_2_PRIOR_KNOWLEDGE));
    curl_easy_setopt(client, CURLOPT_PROXY, "");
    curl_easy_setopt(client, CURLOPT_NOPROXY, "*");
    curl_easy_setopt(client, CURLOPT_POSTFIELDS, message.data());
    curl_easy_setopt(client, CURLOPT_POSTFIELDSIZE, static_cast<long>(message.size()));
    curl_easy_setopt(client, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(client, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(client, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(client, CURLOPT_HEADERFUNCTION, headers);
    curl_easy_setopt(client, CURLOPT_HEADERDATA, &receipt);
    const auto result = curl_easy_perform(client);
    long status = 0, http = 0;
    curl_easy_getinfo(client, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_getinfo(client, CURLINFO_HTTP_VERSION, &http);
    curl_easy_cleanup(client);
    server.join();
    closesocket(listener);
    WSACleanup();
    std::cout << "curl=" << version->version << " nghttp2=" << version->nghttp2_version
              << " result=" << result << " status=" << status << " http_version=" << http
              << " grpc_trailer=" << receipt << " bytes=" << response.size() << std::endl;
    assert(result == CURLE_OK && status == 200 && http == CURL_HTTP_VERSION_2_0 && receipt &&
           state.ok &&
           response == std::string(reinterpret_cast<const char *>(message.data()), message.size()));
    curl_global_cleanup();
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
