#include "ui_observer_shutdown.hpp"
#include <initializer_list>
#include <iostream>
#include <stdexcept>

int main() try {
    using namespace voyemu;
    using namespace std::chrono;
    const auto start = steady_clock::time_point{};
    const auto deadline = start + UiObserverShutdown::grace;
    const auto require = [](bool value) {
        if (!value) {
            throw std::runtime_error("observer shutdown policy");
        }
    };
    const std::string_view old_boot = "1cfcc625-9d42-40e8-ac0c-f2f8dc789b85",
                           new_boot = "76045be3-57f5-4b04-bccd-40ff21a6d6ec";
    require(UiObserverShutdown::replaced_boot(old_boot, new_boot));
    require(!UiObserverShutdown::replaced_boot(old_boot, old_boot));
    require(!UiObserverShutdown::replaced_boot("", new_boot));
    require(!UiObserverShutdown::replaced_boot(old_boot, "76045be3-57f5-4b04-bccd-40ff21a6d6eg"));
    require(!UiObserverShutdown::replaced_boot(old_boot, "76045be3_57f5-4b04-bccd-40ff21a6d6ec"));
    // Reproduces a legitimate cleanup exceeding the former two-second grace.
    require(UiObserverShutdown::waiting(start + milliseconds(2500), deadline, true));
    require(UiObserverShutdown::waiting(start + milliseconds(5900), deadline, true));
    require(!UiObserverShutdown::waiting(deadline, deadline, true));
    require(!UiObserverShutdown::waiting(start, deadline, false));

    for (bool ack : {false, true}) {
        for (bool terminal : {false, true}) {
            for (int code : {-1, 0, 1, 137}) {
                require(UiObserverShutdown::verified(ack, terminal, code) ==
                        (ack && terminal && code == 0));
            }
        }
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
