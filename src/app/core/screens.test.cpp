#include "screens.hpp"
#include <cassert>
#include <iostream>

using voyemu::Json;
namespace {
template <class Function> bool rejects(Function f) {
    try {
        f();
    } catch (const std::runtime_error &error) {
        return std::string_view(error.what()).starts_with("PROFILE_SCREENS: ");
    }

    return false;
}

Json android(const char *role, unsigned width = 1920, unsigned height = 720) {
    return {{"role", role},
            {"guest", "android"},
            {"width", width},
            {"height", height},
            {"density", 160}};
}
} // namespace

int main() try {
    const auto rear = voyemu::profile_screens(
            Json::array({android("primary"), android("secondary"), android("rear", 1902, 540)}));

    assert(rear.size() == 3 && rear[2].role == "rear" && rear[2].width == 1902 &&
           rear[2].height == 540 && rear[2].density == 160 && rear[2].android_display == 2u);
    assert(rear[0].android_display == 0u && rear[1].android_display == 1u);
    assert(voyemu::find_screen(rear, "rear").value().android_display == 2u &&
           !voyemu::find_screen(rear, "hud"));
    assert(voyemu::multimedia_screen("secondary") && !voyemu::multimedia_screen("rear"));

    const auto hud = voyemu::profile_screens(
            Json::array({android("primary"),
                         android("secondary"),
                         {{"role", "hud"}, {"guest", "qnx"}, {"width", 1280}, {"height", 720}}}));
    assert(hud[2].guest == "qnx" && !hud[2].android_display && hud[2].density == 0);

    // A screen on another guest, a repeated or unknown role and a misplaced primary are rejected.
    assert(rejects([] {
        voyemu::profile_screens(Json::array(
                {android("primary"),
                 {{"role", "rear"}, {"guest", "qnx"}, {"width", 1902}, {"height", 540}}}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(Json::array({android("primary"), android("primary")}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(Json::array({android("primary"), android("tertiary")}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(Json::array({android("secondary"), android("primary")}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(
                Json::array({android("primary"), android("rear"), android("secondary")}));
    }));
    // Missing role or guest, unbounded size, frames over the limit and a QNX density.
    assert(rejects([] {
        voyemu::profile_screens(Json::array({{{"width", 1920}, {"height", 720}}}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(Json::array({android("primary", 0)}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(Json::array({android("primary", 3840, 2160)}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(Json::array({android("primary"),
                                             {{"role", "hud"},
                                              {"guest", "qnx"},
                                              {"width", 1280},
                                              {"height", 720},
                                              {"density", 160}}}));
    }));
    assert(rejects([] {
        voyemu::profile_screens(Json::array());
    }));

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
