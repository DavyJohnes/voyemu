#include "android_console.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#ifndef _WIN32
#include <sys/resource.h>
#endif
using namespace voyemu;

static void require(bool value, const char *reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

int main() try {
    {
        AndroidSdkAdbSink sink;

        require(sink.port() > 0 && sink.port() < 65536, "SDK notification port out of range");
        // The sink binds the port without listening, so a notification connection never
        // completes: Linux and Windows refuse it, while the XNU TCP stack drops a SYN to a
        // bound, non-listening socket and the connection stays incomplete until its deadline.
        bool refused = false;

        try {
            android_console_detail::Socket client;

            client.connect(sink.port(),
                           android_console_detail::Clock::now() + std::chrono::seconds(5));
#ifdef __APPLE__
        } catch (const android_console_detail::Timeout &) {
#else
        } catch (const android_console_detail::Disconnected &) {
#endif
            refused = true;
        }

        require(refused, "SDK notification port accepted a connection");
    }

#ifndef _WIN32
    // Without a free descriptor the socket itself fails; the sink reports the owner error.
    rlimit previous{};

    require(getrlimit(RLIMIT_NOFILE, &previous) == 0, "getrlimit failed");
    rlimit exhausted = previous;
    exhausted.rlim_cur = 3;
    require(setrlimit(RLIMIT_NOFILE, &exhausted) == 0, "setrlimit failed");
    std::string message;

    try {
        AndroidSdkAdbSink sink;
    } catch (const std::runtime_error &error) {
        message = error.what();
    }

    require(setrlimit(RLIMIT_NOFILE, &previous) == 0, "setrlimit restore failed");
    require(message == "ANDROID_ADB_OWNER: SDK notification port unavailable",
            "socket failure did not map to ANDROID_ADB_OWNER");
#endif
    std::cout << "android console tests passed\n";

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
