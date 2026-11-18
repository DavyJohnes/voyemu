#include "machine_host_ports.hpp"
#include <cstdlib>
#include <exception>
#include <iostream>
#include <set>
namespace {
void check(bool value) {
    if (!value) {
        std::abort();
    }
}
} // namespace

int main() try {
    using namespace voyemu;
    std::set<unsigned> occupied{5574, 5575, 35574, 45995};
    auto available = [&](unsigned port) {
        return !occupied.count(port);
    };
    std::set<unsigned> leased;
    auto unleased = [&](unsigned console) {
        return !leased.count(console);
    };
    const auto console = select_machine_console_port(5574, available, unleased);
    check(console == 5554 && machine_android_ports_available(console, available, unleased));
    check(select_machine_qmp_port(45995, console, available) == 45996);
    check(occupied == std::set<unsigned>({5574, 5575, 35574, 45995}));
    const auto former_adb_server_occupied = [](unsigned port) {
        return port != 25574;
    };
    check(select_machine_console_port(5574, former_adb_server_occupied, unleased) == 5574);
    check(select_machine_qmp_port(25554, 5554, available) == 25554);
    check(select_machine_console_port(5580, available, unleased) == 5580);
    occupied.insert(35580);
    check(select_machine_console_port(5580, available, unleased) != 5580);
    occupied.erase(35580);
    occupied.insert(5581);
    check(select_machine_console_port(5580, available, unleased) != 5580);
    check(!machine_android_ports_available(5562, available, unleased) &&
          !machine_android_ports_available(5555, available, unleased));
    check(select_machine_console_port(5562, available, unleased) == 5554);
    // A console another Voyemu process leases is skipped although its listeners are still free.
    leased.insert(5554);
    check(!machine_android_ports_available(5554, available, unleased) &&
          select_machine_console_port(5554, available, unleased) == 5556);
    leased.clear();
    check(select_machine_qmp_port(35554, 5554, available) == 45996);
    check(select_machine_qmp_port(47001, 5554, available) == 47001);
    auto none = [](unsigned) {
        return false;
    };
    bool denied = false;
    try {
        (void)select_machine_console_port(5574, none, unleased);
    } catch (const std::runtime_error &) {
        denied = true;
    }

    check(denied);
    denied = false;
    try {
        (void)select_machine_qmp_port(45995, 5554, none);
    } catch (const std::runtime_error &) {
        denied = true;
    }

    check(denied);
    std::cout << "machine host port selection PASS\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
