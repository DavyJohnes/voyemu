#pragma once
#include "https.hpp"
#include <filesystem>

namespace voyemu::os {
// The libcurl functions that the HTTPS backend calls, declared here instead of by curl.h so that a
// host without curl headers loads the system library at run time; the option numbers are the
// stable libcurl ABI.
struct Curl {
    int (*global_init)(long flags);
    void *(*easy_init)();
    int (*easy_setopt)(void *handle, int option, ...);
    int (*easy_perform)(void *handle);
    int (*easy_getinfo)(void *handle, int info, ...);
    void (*easy_cleanup)(void *handle);
    const char *(*easy_strerror)(int code);
    void *(*slist_append)(void *list, const char *text);
    void (*slist_free_all)(void *list);
};
// This host's libcurl; throws DOWNLOAD_LIBRARY when the host has none.
const Curl &curl();
// Loads libcurl by file name from the system library directories and resolves every function of
// Curl; throws DOWNLOAD_LIBRARY naming the library when it is absent or lacks one. Linux backend.
const Curl &load_curl(const std::string &library);
// https_get through curl; a non-empty trust file replaces the system certificate authorities, for
// tests against a local server.
void curl_https_get(const Curl &curl, const std::string &url,
                    const std::vector<std::string> &headers,
                    const std::function<void(std::string_view)> &sink,
                    const std::filesystem::path &trust = {});
} // namespace voyemu::os
