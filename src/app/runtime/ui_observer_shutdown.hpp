#pragma once
#include <chrono>
#include <string_view>
namespace voyemu {
struct UiObserverShutdown {
    // Guest cleanup: workers (2s), callbacks (1s), plus transport/Binder margin.
    static constexpr auto grace = std::chrono::seconds(6);

    static bool replaced_boot(std::string_view previous, std::string_view current) {
        const auto valid = [](std::string_view value) {
            if (value.size() != 36) {
                return false;
            }

            for (std::size_t i = 0; i < value.size(); ++i) {
                if (i == 8 || i == 13 || i == 18 || i == 23) {
                    if (value[i] != '-') {
                        return false;
                    }
                } else if (!((value[i] >= '0' && value[i] <= '9') ||
                             (value[i] >= 'a' && value[i] <= 'f'))) {
                    return false;
                }
            }

            return true;
        };

        return valid(previous) && valid(current) && previous != current;
    }

    static bool verified(bool acknowledged, bool terminal, int exit_code) {
        return acknowledged && terminal && exit_code == 0;
    }

    static bool waiting(std::chrono::steady_clock::time_point now,
                        std::chrono::steady_clock::time_point deadline, bool alive) {
        return alive && now < deadline;
    }
};
} // namespace voyemu
