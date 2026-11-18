#include "curl.hpp"
#include "error.hpp"
#include "library.hpp"
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace voyemu::os {
namespace {
struct Loaded {
    explicit Loaded(const std::string &name) : library(name) {}

    Library library;
    Curl curl{};
};
} // namespace

// Linux distributions ship libcurl with the system TLS library and certificate store, so Voyemu
// links no TLS of its own; a loaded library stays loaded for the life of the process.
const Curl &load_curl(const std::string &library) {
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Loaded>> loaded;
    const std::lock_guard lock(mutex);

    if (const auto found = loaded.find(library); found != loaded.end()) {
        return found->second->curl;
    }

    auto candidate = std::make_unique<Loaded>(library);

    if (!candidate->library.loaded()) {
        throw VoyemuError("DOWNLOAD_LIBRARY",
                          library +
                                  " is required for HTTPS downloads; install the system libcurl: " +
                                  candidate->library.failure());
    }

    auto resolve = [&](auto &function, const char *name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                candidate->library.symbol(name));

        if (!function) {
            throw VoyemuError("DOWNLOAD_LIBRARY", library + " lacks " + name);
        }
    };
    auto &curl = candidate->curl;
    resolve(curl.global_init, "curl_global_init");
    resolve(curl.easy_init, "curl_easy_init");
    resolve(curl.easy_setopt, "curl_easy_setopt");
    resolve(curl.easy_perform, "curl_easy_perform");
    resolve(curl.easy_getinfo, "curl_easy_getinfo");
    resolve(curl.easy_cleanup, "curl_easy_cleanup");
    resolve(curl.easy_strerror, "curl_easy_strerror");
    resolve(curl.slist_append, "curl_slist_append");
    resolve(curl.slist_free_all, "curl_slist_free_all");

    return loaded.emplace(library, std::move(candidate)).first->second->curl;
}

const Curl &curl() {
    return load_curl("libcurl.so.4");
}
} // namespace voyemu::os
