#pragma once
#include "error.hpp"
#include <stdexcept>
namespace voyemu {
inline bool valid_machine_console_port(unsigned port) {
    return port >= 5554 && port <= 5682 && port % 2 == 0 && port != 5562;
}

// A tuple is free when no other Voyemu process leases its console and no listener holds a port.
template <class Available, class Leasable>
bool machine_android_ports_available(unsigned console, Available available, Leasable leasable) {
    return valid_machine_console_port(console) && leasable(console) && available(console) &&
           available(console + 1) && available(console + 30000);
}

template <class Available, class Leasable>
unsigned select_machine_console_port(unsigned preferred, Available available, Leasable leasable) {
    if (machine_android_ports_available(preferred, available, leasable)) {
        return preferred;
    }

    for (unsigned port = 5554; port <= 5682; port += 2) {
        if (port != preferred && machine_android_ports_available(port, available, leasable)) {
            return port;
        }
    }

    throw VoyemuError("HOST_PORTS_BUSY", "no free Android console, ADB and gRPC tuple");
}

template <class Available>
unsigned select_machine_qmp_port(unsigned preferred, unsigned console, Available available) {
    const auto free = [&](unsigned port) {
        return port >= 1024 && port <= 65535 && port != console && port != console + 1 &&
               port != console + 30000 && available(port);
    };

    if (free(preferred)) {
        return preferred;
    }

    for (unsigned port = 45995; port <= 46123; ++port) {
        if (port != preferred && free(port)) {
            return port;
        }
    }

    throw VoyemuError("HOST_PORTS_BUSY", "no free QMP port");
}
} // namespace voyemu
