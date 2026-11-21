#include "android_console.hpp"
#include "android_console_test_host.hpp"
#include "os/platform.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
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
        } catch (const android_console_detail::Timeout &) {
            // macOS lets the refused connect time out; the other hosts report the disconnect.
            refused = voyemu::os::platform_name == "macOS";
        } catch (const android_console_detail::Disconnected &) {
            refused = voyemu::os::platform_name != "macOS";
        }

        require(refused, "SDK notification port accepted a connection");
    }

    exhausted_descriptor_case();
    std::cout << "android console tests passed\n";

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
