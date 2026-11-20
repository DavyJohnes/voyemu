#include "curl.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include <exception>
#include <limits>
#include <mutex>
#include <stdexcept>

namespace voyemu::os {
namespace {
// libcurl option, information and result numbers of the stable ABI (curl/curl.h).
constexpr int curl_ok = 0, curl_resolve_proxy = 5, curl_resolve_host = 6, curl_connect = 7,
              curl_timeout = 28;
constexpr long curl_global_default = 3, curl_protocol_https = 2;
constexpr int option_write_data = 10001, option_url = 10002, option_error_buffer = 10010,
              option_write_function = 20011, option_low_speed_limit = 19,
              option_low_speed_time = 20, option_user_agent = 10018, option_http_header = 10023,
              option_fail_on_error = 45, option_follow_location = 52, option_ca_info = 10065,
              option_max_redirects = 68, option_connect_timeout = 78, option_no_signal = 99,
              option_no_progress = 43, option_xferinfo_data = 10057,
              option_xferinfo_function = 20219, option_protocols = 181,
              option_redirect_protocols = 182, option_protocols_text = 10318,
              option_redirect_protocols_text = 10319, info_response_code = 0x200002,
              info_content_length = 0x600000 + 15, info_retry_after = 0x600000 + 57;
} // namespace

void curl_https_get(const Curl &curl, const std::string &url,
                    const std::vector<std::string> &headers,
                    const std::function<void(std::string_view)> &sink,
                    const std::filesystem::path &trust,
                    const std::function<void(std::uint64_t)> &length) {
    static std::once_flag initialized;
    static bool ready = false;

    std::call_once(initialized, [&] {
        ready = curl.global_init(curl_global_default) == curl_ok;
    });

    if (!ready) {
        throw VoyemuError("DOWNLOAD_HTTP", "curl initialization failed");
    }

    struct Client {
        const Curl &curl;
        void *value;

        ~Client() {
            if (value) {
                curl.easy_cleanup(value);
            }
        }
    } client{curl, curl.easy_init()};

    if (!client.value) {
        throw VoyemuError("DOWNLOAD_HTTP", "allocation failed");
    }

    struct Output {
        const Curl *curl;
        void *client;
        const std::function<void(std::string_view)> *sink;
        const std::function<void(std::uint64_t)> *length;
        bool started;
        std::exception_ptr error;
    };
    Output output{&curl, client.value, &sink, &length, false, {}};
    using Write = std::size_t (*)(char *, std::size_t, std::size_t, void *);
    Write callback = [](char *data, std::size_t size, std::size_t count,
                        void *opaque) noexcept -> std::size_t {
        auto &result = *static_cast<Output *>(opaque);

        if (size && count > std::numeric_limits<std::size_t>::max() / size) {
            return 0;
        }

        try {
            // The write function receives only the body of the final response.
            if (!result.started) {
                result.started = true;
                long long total = -1;

                if (*result.length &&
                    result.curl->easy_getinfo(result.client, info_content_length, &total) ==
                            curl_ok &&
                    total >= 0) {
                    (*result.length)(static_cast<std::uint64_t>(total));
                }
            }

            const auto bytes = size * count;
            (*result.sink)({data, bytes});

            return bytes;
        } catch (...) {
            result.error = std::current_exception();

            return 0;
        }
    };
    // libcurl calls the transfer information function about once a second even while no bytes
    // arrive, so an interrupted command does not wait for a stalled transfer.
    using Progress = int (*)(void *, long long, long long, long long, long long);
    Progress progress = [](void *opaque, long long, long long, long long,
                           long long) noexcept -> int {
        auto &result = *static_cast<Output *>(opaque);

        try {
            interruption::check();

            return 0;
        } catch (...) {
            result.error = std::current_exception();

            return 1;
        }
    };
    struct Headers {
        const Curl &curl;
        void *value = nullptr;

        ~Headers() {
            curl.slist_free_all(value);
        }
    } list{curl};

    for (const auto &header : headers) {
        auto *appended = curl.slist_append(list.value, header.c_str());

        if (!appended) {
            throw VoyemuError("DOWNLOAD_HTTP", "header allocation failed");
        }

        list.value = appended;
    }

    char detail[256]{};
    auto set = [&](int option, auto value) {
        return curl.easy_setopt(client.value, option, value);
    };
    // libcurl before 7.85 knows only the protocol bit masks, which later releases still accept.
    const bool protocols = (set(option_protocols_text, "https") == curl_ok &&
                            set(option_redirect_protocols_text, "https") == curl_ok) ||
                           (set(option_protocols, curl_protocol_https) == curl_ok &&
                            set(option_redirect_protocols, curl_protocol_https) == curl_ok);

    if (!protocols || set(option_url, url.c_str()) != curl_ok ||
        set(option_follow_location, 1L) != curl_ok || set(option_max_redirects, 5L) != curl_ok ||
        set(option_fail_on_error, 1L) != curl_ok || set(option_connect_timeout, 30L) != curl_ok ||
        set(option_low_speed_limit, 1L) != curl_ok || set(option_low_speed_time, 60L) != curl_ok ||
        set(option_no_signal, 1L) != curl_ok || set(option_write_function, callback) != curl_ok ||
        set(option_xferinfo_function, progress) != curl_ok ||
        set(option_xferinfo_data, static_cast<void *>(&output)) != curl_ok ||
        set(option_no_progress, 0L) != curl_ok ||
        set(option_write_data, static_cast<void *>(&output)) != curl_ok ||
        set(option_user_agent, "Voyemu/1.0") != curl_ok ||
        set(option_error_buffer, static_cast<char *>(detail)) != curl_ok ||
        (list.value && set(option_http_header, list.value) != curl_ok) ||
        (!trust.empty() && set(option_ca_info, trust.c_str()) != curl_ok)) {
        throw VoyemuError("DOWNLOAD_HTTP", "curl rejected the HTTPS transfer options");
    }

    const auto result = curl.easy_perform(client.value);

    if (output.error) {
        std::rethrow_exception(output.error);
    }

    long status = 0;
    curl.easy_getinfo(client.value, info_response_code, &status);

    if (status == 429 || status == 503) {
        // curl_off_t seconds of Retry-After, as delay or HTTP date; 0 when the header is absent.
        long long retry_after = 0;

        if (curl.easy_getinfo(client.value, info_retry_after, &retry_after) != curl_ok ||
            retry_after <= 0) {
            retry_after = -1;
        }

        throw HttpsBusy(status, retry_after);
    }

    if (result != curl_ok || status != 200) {
        const char *reason = detail[0] ? detail : curl.easy_strerror(result);
        const auto message = "successful complete HTTPS response required: " +
                             std::string(reason ? reason : "unknown curl failure");

        if (status >= 400) {
            throw HttpsFailure(HttpsFailure::Kind::status, status, message);
        }

        // Results 5, 6 and 7 resolve no proxy or host or connect to none; 28 times out, before a
        // response while connecting or after one while the body stalls.
        if (result == curl_resolve_proxy || result == curl_resolve_host || result == curl_connect ||
            (result == curl_timeout && status == 0)) {
            throw HttpsFailure(HttpsFailure::Kind::unreachable, 0, message);
        }

        if (result == curl_timeout) {
            throw HttpsFailure(HttpsFailure::Kind::stalled, status, message);
        }

        throw VoyemuError("DOWNLOAD_HTTP", message);
    }
}

void https_get(const std::string &url, const std::vector<std::string> &headers,
               const std::function<void(std::string_view)> &sink,
               const std::function<void(std::uint64_t)> &length) {
    curl_https_get(curl(), url, headers, sink, {}, length);
}
} // namespace voyemu::os
