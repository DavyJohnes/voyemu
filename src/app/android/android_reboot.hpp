#pragma once
#include "error.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace voyemu {
inline bool android_boot_id_valid(std::string_view id) {
    if (id.size() != 36) {
        return false;
    }

    for (std::size_t i = 0; i < id.size(); ++i) {
        if (i != 8 && i != 13 && i != 18 && i != 23 && (id[i] < '0' || id[i] > '9') &&
            (id[i] < 'a' || id[i] > 'f')) {
            return false;
        }
    }

    return id[8] == '-' && id[13] == '-' && id[18] == '-' && id[23] == '-' &&
           id.find_first_not_of("0123456789abcdef-") == std::string_view::npos;
}

inline void validate_android_reboot_mode(std::string_view mode) {
    if (mode != "warm" && mode != "cold") {
        throw VoyemuError("TEST_ARGUMENT", "reboot mode must be warm or cold");
    }
}

inline bool android_reboot_changed(std::string_view before, std::string_view after) {
    if (!android_boot_id_valid(before) || !android_boot_id_valid(after)) {
        throw VoyemuError("ANDROID_REBOOT_BOOT", "complete Linux boot identity required");
    }

    return before != after;
}
} // namespace voyemu
