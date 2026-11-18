#include "android_clock.hpp"
#include "process.hpp"
#include <iostream>
#include <stdexcept>
using namespace voyemu;

static void require(bool value) {
    if (!value) {
        throw std::runtime_error("Android clock epoch/readback guard failed");
    }
}

int main() try {
    require(android_epoch_seconds("1791579105\r\n") == 1791579105);
    require(android_epoch_seconds("\t1791579105 \n") == 1791579105);
    require(android_epoch_seconds("0") == 0);

    for (const auto *bad :
         {"", "+1", "-1", "1x", "1\n2", "SecurityException", "Starting service: fixture",
          "9223372036854776", "999999999999999999999999"}) {
        require(!android_epoch_seconds(bad));
    }

    require(android_clock_current(1791579105, 1791579105325));
    require(android_clock_current(1791579105, 1791579107000));
    require(!android_clock_current(1791579105, 1791579107001));
    require(!android_clock_current(1791579105, 1791575505000));
    require(!android_clock_current(-1, 1791579105000));
    require(!android_clock_current(std::numeric_limits<std::int64_t>::max(), 1));
    require(android_clock_arrival_valid(1700000000000, 1700000055000, 55000));
    require(!android_clock_arrival_valid(1700000000000, 1700000063001, 63001));
    require(!android_clock_arrival_valid(1700000000000, 1700000057001, 55000));
    require(!android_clock_arrival_valid(1700000000000, 1700000052999, 55000));
    require(!android_clock_arrival_valid(1700000000000, 1700000000000, -1));
    require(!android_clock_current(1700000000, 1700000055000));
    require(android_clock_observation_current(0));
    require(android_clock_observation_current(63000));
    require(!android_clock_observation_current(-1));
    require(!android_clock_observation_current(63001));
    // A valid current observation remains usable at the measured OEM 28 second offset.
    require(android_clock_observation_current(1000) &&
            !android_clock_synced(1700000000, 1700000028000, 1700000029000, 1000));
    require(android_clock_synced(1700000000, 1700000000000, 1700000001000, 1000));
    // Host wall-clock steps invalidate synchronization quality, not monotonic freshness.
    require(android_clock_observation_current(1000) &&
            !android_clock_synced(1700000000, 1700000000000, 1700000029000, 1000));
    const auto framed = android_clock_caller_command(1791579105325);
    require(framed.find("/system/bin/cmd alarm set-time 1791579105325 & c=$!") !=
            std::string::npos);
    require(framed.find("post-spawn-not-atomic") != std::string::npos);
    require(framed.find("/proc/$c/stat /proc/$c/attr/current /proc/$c/comm") != std::string::npos);
    require(framed.find("wait \"$c\"; r=$?") != std::string::npos);
    bool invalid = false;
    try {
        android_clock_caller_command(-1);
    } catch (const std::invalid_argument &) {
        invalid = true;
    }

    require(invalid);
#ifndef _WIN32
    // Exercise the exact shell frame with a local fixed child; /proc may be absent on macOS.
    // Missing diagnostics must never overwrite the child's original exit status or output.
    for (const auto &[child, status] :
         {std::pair{"sh -c 'printf command-stdout; printf command-stderr >&2; exit 255'", 255},
          std::pair{"sh -c 'kill -TERM $$'", 143}}) {
        auto script = framed;
        const std::string invocation = "/system/bin/cmd alarm set-time 1791579105325";
        script.replace(script.find(invocation), invocation.size(), child);
        ProcessSpec spec;
        spec.executable = "/bin/sh";
        spec.arguments = {"-c", script};
        const auto result = execute(spec, 1500, 8192);
        require(!result.timed_out && result.exit_code == status);
        require(result.output.find("child_pid=") != std::string::npos);
        require(result.output.find("wait_status=" + std::to_string(status)) != std::string::npos);
        require(result.output.find("VOYEMU_CLOCK_V1 end") != std::string::npos);
        require(result.output.find("after /proc/uptime=") != std::string::npos);

        if (status == 255) {
            require(result.output.find("command-stdout") != std::string::npos);
            require(result.output.find("command-stderr") != std::string::npos);
        } else {
            require(result.output.find("possible_signal=15") != std::string::npos);
        }
    }
#endif
    std::cout
            << "bounded epoch parsing, timezone-independent readback, drift and overflow guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
