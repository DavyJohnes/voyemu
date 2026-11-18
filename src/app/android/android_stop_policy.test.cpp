#include "android_stop_policy.hpp"
#include <iostream>
#include <stdexcept>

int main() try {
    // Stop arrives before bind. A deterministic scheduler publishes the SDK
    // listeners after the first identity probe, without relying on wall time.
    bool bound = false;
    unsigned probes = 0, pauses = 0;
    const bool verified = voyemu::await_android_stop_identity(
            [] {
                return true;
            },
            [&] {
                return !bound;
            },
            [&] {
                return pauses < 3;
            },
            [&] {
                ++probes;

                return bound;
            },
            [&] {
                ++pauses;
                bound = true;
            });

    if (!verified || probes != 2 || pauses != 1) {
        return 2;
    }

    // A retained process that never binds is bounded and never gains identity.
    unsigned ticks = 0;

    if (voyemu::await_android_stop_identity(
                [] {
                    return true;
                },
                [] {
                    return true;
                },
                [&] {
                    return ticks < 3;
                },
                [] {
                    return false;
                },
                [&] {
                    ++ticks;
                }) ||
        ticks != 3) {
        return 3;
    }

    // An exited process with free ports needs neither identity nor a kill.
    if (voyemu::await_android_stop_identity(
                [] {
                    return false;
                },
                [] {
                    return true;
                },
                [] {
                    return true;
                },
                [] {
                    throw std::runtime_error("unexpected probe");

                    return false;
                },
                [] {})) {
        return 4;
    }

    // Identity mismatch remains fatal; it cannot fall through to host shutdown.
    bool mismatch = false;
    try {
        voyemu::await_android_stop_identity(
                [] {
                    return true;
                },
                [] {
                    return false;
                },
                [] {
                    return true;
                },
                [] {
                    throw std::runtime_error("ANDROID_IDENTITY_MISMATCH");

                    return false;
                },
                [] {});
    } catch (const std::runtime_error &) {
        mismatch = true;
    }

    if (!mismatch) {
        return 5;
    }

    for (unsigned flags = 0; flags < 4; ++flags) {
        const bool child = flags & 1, mismatch = flags & 2;

        if (voyemu::allow_unready_android_stop(child, mismatch) != (flags == 1)) {
            return 1;
        }
    }

    // A partial start: the live owned child never bound its listeners because another session
    // holds the tuple. Host-only termination stops it and leaves those listeners alone.
    if (!voyemu::allow_unready_android_stop(true, false) ||
        !voyemu::android_host_stop_complete(true, false, false)) {
        return 6;
    }

    // A live child, or a terminal child whose own listeners remain, is not stopped.
    if (voyemu::android_host_stop_complete(false, false, true) ||
        voyemu::android_host_stop_complete(false, true, true) ||
        voyemu::android_host_stop_complete(true, true, false) ||
        !voyemu::android_host_stop_complete(true, true, true)) {
        return 7;
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
