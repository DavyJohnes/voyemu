#include "curl.hpp"
#include <curl/curl.h>

namespace voyemu::os {
// macOS links its system libcurl.
const Curl &curl() {
    static const Curl linked{reinterpret_cast<int (*)(long)>(&curl_global_init),
                             reinterpret_cast<void *(*)()>(&curl_easy_init),
                             reinterpret_cast<int (*)(void *, int, ...)>(&curl_easy_setopt),
                             reinterpret_cast<int (*)(void *)>(&curl_easy_perform),
                             reinterpret_cast<int (*)(void *, int, ...)>(&curl_easy_getinfo),
                             reinterpret_cast<void (*)(void *)>(&curl_easy_cleanup),
                             reinterpret_cast<const char *(*)(int)>(&curl_easy_strerror),
                             reinterpret_cast<void *(*)(void *, const char *)>(&curl_slist_append),
                             reinterpret_cast<void (*)(void *)>(&curl_slist_free_all)};

    return linked;
}
} // namespace voyemu::os
