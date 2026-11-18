#include "window_shell.hpp"
#include <SDL3/SDL.h>
#include <cstdlib>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace voyemu::window_shell {
void prepare_application() {}

void set_quit_title(const char *) {}

void set_light_appearance(SDL_Window *, bool) {}

namespace {
std::optional<DWORD> user_dword(const wchar_t *key, const wchar_t *name) {
    DWORD value = 0, size = sizeof(value);

    if (RegGetValueW(HKEY_CURRENT_USER, key, name, RRF_RT_REG_DWORD, nullptr, &value, &size) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }

    return value;
}
} // namespace

// Title bars show the accent (stored as 0xAABBGGRR) when the user enables it; otherwise the app
// theme's plain bar.
std::optional<TitleColors> title_colors(SDL_Window *) {
    const auto prevalence = user_dword(L"Software\\Microsoft\\Windows\\DWM", L"ColorPrevalence");
    const auto accent = user_dword(L"Software\\Microsoft\\Windows\\DWM", L"AccentColor");

    if (prevalence && *prevalence == 1 && accent) {
        const unsigned r = *accent & 0xffU, g = *accent >> 8 & 0xffU, b = *accent >> 16 & 0xffU;
        const bool dark_text = r * 299 + g * 587 + b * 114 > 150000;

        return TitleColors{r << 16 | g << 8 | b, dark_text ? 0x000000U : 0xffffffU};
    }

    const auto light =
            user_dword(L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       L"AppsUseLightTheme");

    if (!light) {
        return std::nullopt;
    }

    return *light ? TitleColors{0xffffffU, 0x000000U} : TitleColors{0x202020U, 0xffffffU};
}

// The caption font of the non-client metrics at the DPI of the window.
std::optional<float> title_font_pixels(SDL_Window *window) {
    const auto handle = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    NONCLIENTMETRICSW metrics{};

    metrics.cbSize = sizeof(metrics);

    if (!handle || !SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics,
                                               0, GetDpiForWindow(handle))) {
        return std::nullopt;
    }

    return static_cast<float>(std::abs(metrics.lfCaptionFont.lfHeight));
}

void send_to_back(SDL_Window *window) {
    const auto handle = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));

    if (handle) {
        SetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
} // namespace voyemu::window_shell
