#include "android_console.hpp"
#include "android_console_test_host.hpp"
#include <stdexcept>
#include <string>
#include <sys/resource.h>

namespace {
void require(bool value, const char *reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}
} // namespace

void exhausted_descriptor_case() {
    // Without a free descriptor the socket itself fails; the sink reports the owner error.
    rlimit previous{};

    require(getrlimit(RLIMIT_NOFILE, &previous) == 0, "getrlimit failed");
    rlimit exhausted = previous;
    exhausted.rlim_cur = 3;
    require(setrlimit(RLIMIT_NOFILE, &exhausted) == 0, "setrlimit failed");
    std::string message;

    try {
        voyemu::AndroidSdkAdbSink sink;
    } catch (const std::runtime_error &error) {
        message = error.what();
    }

    require(setrlimit(RLIMIT_NOFILE, &previous) == 0, "setrlimit restore failed");
    require(message == "ANDROID_ADB_OWNER: SDK notification port unavailable",
            "socket failure did not map to ANDROID_ADB_OWNER");
}
