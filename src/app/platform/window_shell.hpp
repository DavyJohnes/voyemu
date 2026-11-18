#pragma once
#include <optional>
struct SDL_Window;

// Native window details SDL3 does not expose; each host compiles only its own backend.
namespace voyemu::window_shell {
// Called after SDL video initialization, before the window is created.
void prepare_application();
// Title of the only application menu item (Quit) where the host has one.
void set_quit_title(const char *title);
// Title bar and system controls follow the in-window light or dark theme where the host allows.
void set_light_appearance(SDL_Window *window, bool light);
// Background launches stay behind the windows the user is working in.
void send_to_back(SDL_Window *window);
// Background and text of the host's window title bar as 0xRRGGBB where the host exposes them.
struct TitleColors {
    unsigned background, text;
};
std::optional<TitleColors> title_colors(SDL_Window *window);
// Em size of the host's window title bar font in window pixels where the host exposes it.
std::optional<float> title_font_pixels(SDL_Window *window);
} // namespace voyemu::window_shell
