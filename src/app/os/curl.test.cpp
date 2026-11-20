#include "os/curl.hpp"
#include "core/download.hpp"
#include "core/process.hpp"
#include "os/library.hpp"
#include <arpa/inet.h>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unistd.h>

using namespace voyemu;

namespace {
// The libcurl failure of load_curl, or empty when it loads.
std::string load_failure(const std::string &library) {
    try {
        os::load_curl(library);
    } catch (const std::runtime_error &error) {
        return error.what();
    }

    return {};
}

// A local HTTPS server on the system libssl with the fixture certificate for 127.0.0.1.
class Server {
public:
    Server(const os::Library &ssl, const std::filesystem::path &fixtures) {
        auto resolve = [&](auto &function, const char *name) {
            function =
                    reinterpret_cast<std::remove_reference_t<decltype(function)>>(ssl.symbol(name));

            if (!function) {
                throw std::runtime_error(std::string("libssl lacks ") + name);
            }
        };

        resolve(server_method_, "TLS_server_method");
        resolve(context_new_, "SSL_CTX_new");
        resolve(use_certificate_, "SSL_CTX_use_certificate_file");
        resolve(use_key_, "SSL_CTX_use_PrivateKey_file");
        resolve(context_free_, "SSL_CTX_free");
        resolve(new_, "SSL_new");
        resolve(set_fd_, "SSL_set_fd");
        resolve(accept_, "SSL_accept");
        resolve(read_, "SSL_read");
        resolve(write_, "SSL_write");
        resolve(shutdown_, "SSL_shutdown");
        resolve(free_, "SSL_free");
        context_ = context_new_(server_method_());

        if (!context_ || use_certificate_(context_, (fixtures / "server.pem").c_str(), pem) != 1 ||
            use_key_(context_, (fixtures / "server-key.der").c_str(), der) != 1) {
            throw std::runtime_error("TLS server identity rejected");
        }

        listener_ = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        socklen_t size = sizeof address;

        if (listener_ < 0 ||
            ::bind(listener_, reinterpret_cast<sockaddr *>(&address), sizeof address) != 0 ||
            ::listen(listener_, 8) != 0 ||
            ::getsockname(listener_, reinterpret_cast<sockaddr *>(&address), &size) != 0) {
            throw std::runtime_error("TLS server socket failed");
        }

        port_ = ntohs(address.sin_port);
        thread_ = std::thread([this] {
            serve();
        });
    }

    ~Server() {
        stop_ = true;
        thread_.join();
        ::close(listener_);
        context_free_(context_);
    }

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    std::string url(const std::string &path) const {
        return "https://127.0.0.1:" + std::to_string(port_) + path;
    }

    static std::string body() {
        std::string text;

        for (unsigned i = 0; text.size() < 300000; ++i) {
            text += "pinned body " + std::to_string(i) + "\n";
        }

        return text;
    }

private:
    static constexpr int pem = 1, der = 2;

    void serve() {
        while (!stop_) {
            pollfd ready{listener_, POLLIN, 0};

            if (::poll(&ready, 1, 100) != 1) {
                continue;
            }

            const int client = ::accept4(listener_, nullptr, nullptr, SOCK_CLOEXEC);

            if (client >= 0) {
                answer(client);
                ::close(client);
            }
        }
    }

    void answer(int client) {
        void *session = new_(context_);

        if (session && set_fd_(session, client) == 1 && accept_(session) == 1) {
            std::string request;
            char buffer[4096];

            while (request.find("\r\n\r\n") == std::string::npos) {
                const int count = read_(session, buffer, sizeof buffer);

                if (count <= 0) {
                    break;
                }

                request.append(buffer, static_cast<std::size_t>(count));
            }

            const auto path = request.substr(4, request.find(' ', 4) - 4);
            std::string status = "200 OK", headers, content = body();

            if (path == "/missing") {
                status = "404 Not Found";
                content = "missing";
            } else if (path == "/redirect-http") {
                status = "302 Found";
                headers = "Location: http://127.0.0.1:" + std::to_string(port_) + "/ok\r\n";
                content.clear();
            } else if (path == "/redirect-https") {
                status = "302 Found";
                headers = "Location: /ok\r\n";
                content.clear();
            } else if (path == "/busy") {
                status = "429 Too Many Requests";
                headers = "Retry-After: 1\r\n";
                content = "slow down";
            } else if (path == "/unavailable" || (path == "/busy-once" && !busy_once_++)) {
                status = "503 Service Unavailable";
                headers = path == "/busy-once" ? "Retry-After: 1\r\n" : "";
                content = "later";
            } else if (path == "/authorized" &&
                       request.find("\r\nAuthorization: Bearer QQ==\r\n") == std::string::npos) {
                status = "403 Forbidden";
                content = "header missing";
            }

            const auto response = "HTTP/1.1 " + status + "\r\n" + headers +
                                  "Content-Length: " + std::to_string(content.size()) +
                                  "\r\nConnection: close\r\n\r\n" + content;

            for (std::size_t sent = 0; sent < response.size();) {
                const int count = write_(session, response.data() + sent,
                                         static_cast<int>(response.size() - sent));

                if (count <= 0) {
                    break;
                }

                sent += static_cast<std::size_t>(count);
            }

            shutdown_(session);
        }

        if (session) {
            free_(session);
        }
    }

    const void *(*server_method_)();
    void *(*context_new_)(const void *);
    int (*use_certificate_)(void *, const char *, int);
    int (*use_key_)(void *, const char *, int);
    void (*context_free_)(void *);
    void *(*new_)(void *);
    int (*set_fd_)(void *, int);
    int (*accept_)(void *);
    int (*read_)(void *, void *, int);
    int (*write_)(void *, const void *, int);
    int (*shutdown_)(void *);
    void (*free_)(void *);
    void *context_{};
    unsigned busy_once_ = 0;
    int listener_ = -1;
    unsigned short port_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// The body of a GET through the system libcurl, or the failure prefixed by "!".
std::string get(const Server &server, const std::string &path,
                const std::vector<std::string> &headers, const std::filesystem::path &trust) {
    std::string body;

    try {
        os::curl_https_get(
                os::curl(), path.starts_with("http") ? path : server.url(path), headers,
                [&](std::string_view bytes) {
                    body.append(bytes);
                },
                trust);
    } catch (const std::runtime_error &error) {
        return "!" + std::string(error.what());
    }

    return body;
}

void check(bool value, const std::string &message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: curl_test FIXTURES");
        }

        const std::filesystem::path fixtures = argv[1];
        const auto absent = load_failure("libvoyemu-absent-curl.so.0");
        check(absent.starts_with("DOWNLOAD_LIBRARY: libvoyemu-absent-curl.so.0 is required"),
              "absent libcurl not reported: " + absent);
        // The C library loads but has none of the libcurl functions.
        check(load_failure("libc.so.6") == "DOWNLOAD_LIBRARY: libc.so.6 lacks curl_global_init",
              "incomplete libcurl not reported");
        const os::Library ssl("libssl.so.3");

        if (!load_failure("libcurl.so.4").empty() || !ssl.loaded()) {
            std::cout << "HTTPS skipped: system libcurl.so.4 or libssl.so.3 missing\n";

            return 77;
        }

        const Server server(ssl, fixtures);
        const auto trust = fixtures / "server.pem";
        check(get(server, "/ok", {}, trust) == Server::body(), "trusted HTTPS body differs");
        check(get(server, "/redirect-https", {}, trust) == Server::body(),
              "HTTPS redirect not followed");
        // The size of the final response arrives before its body.
        std::uint64_t announced = 0;
        std::string order;
        os::curl_https_get(
                os::curl(), server.url("/redirect-https"), {},
                [&](std::string_view) {
                    order += order.ends_with('B') ? "" : "B";
                },
                trust,
                [&](std::uint64_t total) {
                    announced = total;
                    order += 'L';
                });
        check(announced == Server::body().size() && order == "LB",
              "response size not announced before the body");
        check(get(server, "/authorized", {"Authorization: Bearer QQ=="}, trust) == Server::body(),
              "request header not sent");
        check(get(server, "/missing", {}, trust).starts_with("!DOWNLOAD_HTTP:"),
              "error status accepted");
        check(get(server, "/redirect-http", {}, trust).starts_with("!DOWNLOAD_HTTP:"),
              "redirect to plain HTTP followed");
        check(get(server, "http://127.0.0.1:1/ok", {}, trust).starts_with("!DOWNLOAD_HTTP:"),
              "plain HTTP accepted");
        // The system certificate authorities do not trust the fixture certificate.
        check(get(server, "/ok", {}, {}).starts_with("!DOWNLOAD_HTTP:"),
              "untrusted certificate accepted");

        // HTTP 429 and 503 report the Retry-After delay, or -1 without one.
        for (const auto &[path, code, delay] :
             {std::tuple<const char *, long, long long>{"/busy", 429, 1},
              std::tuple<const char *, long, long long>{"/unavailable", 503, -1}}) {
            bool busy = false;
            try {
                os::curl_https_get(
                        os::curl(), server.url(path), {}, [](std::string_view) {}, trust);
            } catch (const os::HttpsBusy &error) {
                busy = error.status == code && error.retry_after_seconds == delay &&
                       std::string_view(error.what()).starts_with("DOWNLOAD_HTTP:");
            }

            check(busy, std::string("busy server response not reported: ") + path);
        }

        bool stopped = false;
        try {
            os::curl_https_get(
                    os::curl(), server.url("/ok"), {},
                    [](std::string_view) {
                        throw std::logic_error("sink stop");
                    },
                    trust);
        } catch (const std::logic_error &) {
            stopped = true;
        }

        check(stopped, "sink exception lost");
        // The pinned download verifies and publishes the bytes that libcurl received.
        const auto cache = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                           ("voyemu-curl-" + random_token());
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{cache};
        const DownloadPin pin{server.url("/ok"), sha256_bytes(Server::body()), 1 << 20};
        const auto blob = download_pinned(
                pin, cache, false, 0, [&](const DownloadPin &requested, const DownloadSink &sink) {
                    os::curl_https_get(os::curl(), requested.url, {}, sink, trust);
                });
        check(!blob.cached && blob.bytes == Server::body().size() &&
                      sha256_file(blob.file) == pin.sha256,
              "pinned download through libcurl differs");
        // The pinned download waits out a busy server and then receives the body.
        const DownloadPin busy{server.url("/busy-once"), pin.sha256, 1 << 20};
        const auto retried =
                download_pinned(busy, cache / "busy", false, 0,
                                [&](const DownloadPin &requested, const DownloadSink &sink) {
                                    os::curl_https_get(os::curl(), requested.url, {}, sink, trust);
                                });
        check(!retried.cached && sha256_file(retried.file) == pin.sha256,
              "pinned download after HTTP 503 differs");
        std::cout << "libcurl loading failures and local HTTPS passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
