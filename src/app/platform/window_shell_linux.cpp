#include "window_shell.hpp"

namespace voyemu::window_shell {
void prepare_application() {}

void set_quit_title(const char *) {}

void set_light_appearance(SDL_Window *, bool) {}

void send_to_back(SDL_Window *) {}

std::optional<TitleColors> title_colors(SDL_Window *) {
    return std::nullopt;
}

std::optional<float> title_font_pixels(SDL_Window *) {
    return std::nullopt;
}
} // namespace voyemu::window_shell
