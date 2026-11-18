#include "ui_observer_recovery.hpp"
#include <exception>
#include <iostream>
using namespace voyemu;

static void require(bool value) {
    if (!value) {
        throw std::runtime_error("observer recovery regression");
    }
}

int main() try {
    const std::string boot = "aaaaaaaa-1111-2222-3333-444444444444",
                      next = "bbbbbbbb-1111-2222-3333-444444444444";

    // These current-source expressions wrongly authorize a duplicate helper when
    // a malformed boot response or a changed boot accompanies a live former PID.
    require(std::string("warning\n") + boot != boot);

    for (const auto &current : {boot, next}) {
        require(!UiObserverRecovery::previous_gone(boot, current, 777, "PID\n1\n777\n888\n"));
    }

    for (const auto &current : {boot, next}) {
        require(UiObserverRecovery::previous_gone(boot, current, 777, "PID\n1\n888\n"));
    }

    auto reject = [&](std::string_view before, std::string_view current, unsigned pid,
                      std::string_view inventory) {
        bool denied = false;
        try {
            (void)UiObserverRecovery::previous_gone(before, current, pid, inventory);
        } catch (const std::runtime_error &error) {
            denied = std::string_view(error.what()).starts_with("TREE_OWNER:");
        }

        require(denied);
    };

    for (const auto &invalid :
         {std::string{}, "warning\n" + boot, boot + "\nwarning", std::string(36, 'a')}) {
        reject(boot, invalid, 777, "PID\n1\n777\n");
    }

    reject("foreign", boot, 777, "PID\n1\n777\n");

    for (const auto &inventory :
         {"", "PID\n777\n", "PID\n1\n777garbage\n", "PID\n1\n777\n777\n", "PID\n1\n0\n",
          "PID\n1\n-777\n", "PID\n1\n4294967296\n", "NAME\n1\n777\n"}) {
        reject(boot, boot, 777, inventory);
    }

    reject(boot, boot, 1, "PID\n1\n");
    // Across screen requests the same owned live PID cannot authorize replacement.
    unsigned launches = 1;

    for (const auto screen : {"primary", "secondary", "primary"}) {
        (void)screen;

        if (UiObserverRecovery::previous_gone(boot, boot, 777, "PID\n1\n777\n")) {
            ++launches;
        }
    }

    require(launches == 1);
    using Observation = UiObserverRecovery::Observation;
    unsigned probes = 0, reaps = 0;
    bool stopped = false;
    UiObserverRecovery::retire(
            boot, 777,
            [&] {
                ++probes;

                return Observation{next, "PID\n1\n888\n"};
            },
            [&] {
                ++reaps;
                stopped = true;

                return true;
            });
    require(probes == 2 && reaps == 1 && stopped);
    auto denied_retirement = [&](Observation first, Observation second, bool reaped) {
        unsigned reads = 0, stops = 0;
        bool denied = false;
        try {
            UiObserverRecovery::retire(
                    boot, 777,
                    [&] {
                        return reads++ == 0 ? first : second;
                    },
                    [&] {
                        ++stops;

                        return reaped;
                    });
        } catch (const std::runtime_error &) {
            denied = true;
        }

        require(denied);

        return stops;
    };
    require(denied_retirement({next, "PID\n1\n777\n"}, {next, "PID\n1\n"}, true) == 0);
    require(denied_retirement({"warning\n" + boot, "PID\n1\n777\n"}, {boot, "PID\n1\n"}, true) ==
            0);
    require(denied_retirement({boot, "PID\n1\n888\n"}, {boot, "PID\n1\n888\n"}, false) == 1);
    require(denied_retirement({boot, "PID\n1\n888\n"}, {next, "PID\n1\n888\n"}, true) == 1);
    require(denied_retirement({boot, "PID\n1\n888\n"}, {boot, "PID\n1\n777\n"}, true) == 1);

    for (bool alive : {false, true}) {
        for (bool metadata : {false, true}) {
            bool denied = false;
            try {
                UiObserverRecovery::launch(alive, metadata);
            } catch (const std::runtime_error &) {
                denied = true;
            }

            require(denied == (alive || metadata));
        }
    }

    // Actual production launch fence receives this retained marker after launch
    // succeeds but its localabstract endpoint fails, even after the retained host stream ends.
    bool pending = false;
    UiObserverRecovery::launch(false, pending);
    pending = true;
    bool retry_denied = false;
    try {
        UiObserverRecovery::launch(false, pending);
    } catch (const std::runtime_error &) {
        retry_denied = true;
    }

    require(retry_denied);
    std::cout
            << "Boot identity and complete PID inventory prevent duplicate observer recovery passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
