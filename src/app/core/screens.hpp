#pragma once
#include "model.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace voyemu {
// One screen of a machine as the displays list of its profile describes it: the API role, the
// guest that draws it and its size. Android screens are emulator displays in list order; the main
// window shows the multimedia screens and the screens window shows the other roles.
struct Screen {
    std::string role, guest;
    unsigned width{}, height{}, density{};
    // Emulator display number of an Android screen.
    std::optional<unsigned> android_display;
    // AVD flag and window offsets of an Android screen after the first; absent offsets take the
    // caller's default.
    int flag = 0;
    std::optional<int> x_offset, y_offset;
};
// Each role has one guest: the multimedia and rear screens belong to Android, the HUD to QNX.
inline constexpr std::array<std::pair<std::string_view, std::string_view>, 4> screen_roles{
        {{"primary", "android"}, {"secondary", "android"}, {"rear", "android"}, {"hud", "qnx"}}};
// The largest frame of a screen the window, frames and screenshots carry.
inline constexpr unsigned screen_pixel_limit = 1920u * 1080u;
// The emulator describes up to three displays next to the primary one.
inline constexpr unsigned android_screen_limit = 4;

inline bool multimedia_screen(std::string_view role) {
    return role == "primary" || role == "secondary";
}

inline std::vector<Screen> profile_screens(const Json &displays) {
    const auto need = [](bool ok, const char *reason) {
        if (!ok) {
            throw VoyemuError("PROFILE_SCREENS", reason);
        }
    };
    const auto dimension = [&](const Json &value, unsigned limit) {
        need(value.is_number_integer() && value.get<std::int64_t>() > 0 &&
                     value.get<std::int64_t>() <= limit,
             "bounded screen size required");

        return value.get<unsigned>();
    };

    need(displays.is_array() && !displays.empty() && displays.size() <= screen_roles.size(),
         "bounded displays list required");
    std::vector<Screen> screens;
    unsigned android = 0;

    for (std::size_t i = 0; i < displays.size(); ++i) {
        const auto &display = displays[i];
        need(display.is_object() && display.contains("role") && display.at("role").is_string() &&
                     display.contains("guest") && display.at("guest").is_string(),
             "role and guest of each screen required");
        Screen screen;
        screen.role = display.at("role").get<std::string>();
        screen.guest = display.at("guest").get<std::string>();
        const auto known = std::find_if(screen_roles.begin(), screen_roles.end(),
                                        [&](const auto &entry) {
                                            return entry.first == screen.role;
                                        });
        need(known != screen_roles.end() && known->second == screen.guest,
             "known role on its own guest required");
        need(std::none_of(screens.begin(), screens.end(),
                          [&](const Screen &other) {
                              return other.role == screen.role;
                          }),
             "unique screen role required");
        need((i == 0) == (screen.role == "primary") && (screen.role != "secondary" || i == 1),
             "primary first and secondary second required");
        screen.width = dimension(display.at("width"), 4096);
        screen.height = dimension(display.at("height"), 4096);
        need(screen.width * screen.height <= screen_pixel_limit, "bounded screen frame required");

        if (screen.guest == "android") {
            need(android < android_screen_limit, "bounded Android screens required");
            screen.density = dimension(display.at("density"), 640);
            screen.android_display = android++;
            screen.flag = display.value("flag", 0);

            if (display.contains("xOffset")) {
                screen.x_offset = display.at("xOffset").get<int>();
            }

            if (display.contains("yOffset")) {
                screen.y_offset = display.at("yOffset").get<int>();
            }
        } else {
            need(!display.contains("density"), "density only for Android screens");
        }

        screens.push_back(std::move(screen));
    }

    return screens;
}

// The Android screens of a displays list, in emulator display order.
inline std::vector<Screen> android_screens(const Json &displays) {
    auto screens = profile_screens(displays);

    std::erase_if(screens, [](const Screen &screen) {
        return !screen.android_display;
    });

    return screens;
}

// AVD keys of an emulator display: hw.lcd for the primary one, hw.displayN for the others.
inline std::string avd_display_prefix(std::size_t display) {
    return display ? "hw.display" + std::to_string(display) : "hw.lcd";
}

// AVD keys of the Android screens: size and density of each, flag and offsets of the extra ones.
inline void add_avd_screens(std::map<std::string, std::string> &values,
                            const std::vector<Screen> &screens, int unset_offset) {
    for (std::size_t i = 0; i < screens.size(); ++i) {
        const auto &screen = screens[i];
        const auto prefix = avd_display_prefix(i);
        values[prefix + ".width"] = std::to_string(screen.width);
        values[prefix + ".height"] = std::to_string(screen.height);
        values[prefix + ".density"] = std::to_string(screen.density);

        if (i) {
            values[prefix + ".flag"] = std::to_string(screen.flag);
            values[prefix + ".xOffset"] = std::to_string(screen.x_offset.value_or(unset_offset));
            values[prefix + ".yOffset"] = std::to_string(screen.y_offset.value_or(unset_offset));
        }
    }
}

inline std::optional<Screen> find_screen(const std::vector<Screen> &screens,
                                         std::string_view role) {
    for (const auto &screen : screens) {
        if (screen.role == role) {
            return screen;
        }
    }

    return std::nullopt;
}
} // namespace voyemu
