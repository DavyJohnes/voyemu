#pragma once
#include "error.hpp"
#include <charconv>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
namespace voyemu {
struct UiObserverRecovery {
    static bool valid_boot(std::string_view value) {
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
    }

    struct Observation {
        std::string boot, inventory;
    };

    template <class Observe, class Reap>
    static void retire(std::string_view before, unsigned previous, Observe observe, Reap reap) {
        const auto first = observe();

        if (!previous_gone(before, first.boot, previous, first.inventory)) {
            throw VoyemuError("TREE_TRANSPORT_UNKNOWN",
                              "observer process remains but cannot authenticate");
        }

        if (!reap()) {
            throw VoyemuError("TREE_OWNER", "observer host helper did not terminate");
        }

        const auto second = observe();

        if (second.boot != first.boot) {
            throw VoyemuError("TREE_STALE", "guest boot changed during observer retirement");
        }

        if (!previous_gone(before, second.boot, previous, second.inventory)) {
            throw VoyemuError("TREE_OWNER", "observer PID appeared during retirement");
        }
    }

    static void launch(bool helper_alive, bool retained_metadata) {
        if (helper_alive || retained_metadata) {
            throw VoyemuError("TREE_OWNER", "retained observer lifetime prevents a new helper");
        }
    }

    static bool previous_gone(std::string_view before, std::string_view current, unsigned previous,
                              std::string_view inventory) {
        if (!valid_boot(before) || !valid_boot(current) || previous <= 1) {
            throw VoyemuError("TREE_OWNER", "valid observer boot and PID identities required");
        }

        if (inventory.empty() || inventory.size() > 128 * 1024) {
            throw VoyemuError("TREE_OWNER", "complete guest process inventory unavailable");
        }

        std::istringstream rows{std::string(inventory)};
        std::string value;

        if (!(rows >> value) || value != "PID") {
            throw VoyemuError("TREE_OWNER", "complete guest process inventory unavailable");
        }

        std::set<unsigned> pids;

        while (rows >> value) {
            unsigned pid = 0;
            auto parsed = std::from_chars(value.data(), value.data() + value.size(), pid);

            if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || pid == 0 ||
                !pids.insert(pid).second) {
                throw VoyemuError("TREE_OWNER", "complete guest process inventory unavailable");
            }
        }

        if (!pids.contains(1)) {
            throw VoyemuError("TREE_OWNER", "complete guest process inventory unavailable");
        }

        return !pids.contains(previous);
    }
};
} // namespace voyemu
