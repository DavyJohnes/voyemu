#include "window_shell.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

#import <Cocoa/Cocoa.h>

namespace voyemu::window_shell {
namespace {
NSWindow *native(SDL_Window *window) {
    return (__bridge NSWindow *)SDL_GetPointerProperty(
            SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
}
} // namespace

// The application menu keeps only the standard Quit item so Command-Q works.
void prepare_application() {
    NSProcessInfo.processInfo.processName = @"Voyemu";
    auto *menu = [NSMenu new];
    auto *application = [[NSMenuItem alloc] initWithTitle:@"Voyemu" action:nil keyEquivalent:@""];
    [menu addItem:application];
    auto *submenu = [[NSMenu alloc] initWithTitle:@"Voyemu"];
    auto *quit = [submenu addItemWithTitle:@"Quit Voyemu"
                                    action:@selector(terminate:)
                             keyEquivalent:@"q"];
    quit.tag = 101;
    application.submenu = submenu;
    NSApp.mainMenu = menu;
}

void set_quit_title(const char *title) {
    [[NSApp.mainMenu itemAtIndex:0].submenu itemWithTag:101].title = [NSString
            stringWithUTF8String:title];
}

void set_light_appearance(SDL_Window *window, bool light) {
    NSWindow *handle = native(window);
    NSAppearanceName appearance = light ? NSAppearanceNameAqua : NSAppearanceNameDarkAqua;

    if (handle && ![handle.appearance.name isEqualToString:appearance]) {
        handle.appearance = [NSAppearance appearanceNamed:appearance];
    }
}

void send_to_back(SDL_Window *window) {
    [native(window) orderBack:nil];
}

// Dynamic system colours resolve in the window's own appearance, as its title bar does.
std::optional<TitleColors> title_colors(SDL_Window *window) {
    NSWindow *handle = native(window);

    if (!handle) {
        return std::nullopt;
    }

    __block NSColor *background = nil, *text = nil;
    [handle.effectiveAppearance performAsCurrentDrawingAppearance:^{
        background = [NSColor.windowBackgroundColor
                colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
        text = [NSColor.windowFrameTextColor colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    }];

    if (!background || !text) {
        return std::nullopt;
    }

    const auto rgb = [](NSColor *color) {
        const auto channel = [](CGFloat value) {
            return static_cast<unsigned>(
                    std::lround(std::clamp(value, CGFloat(0), CGFloat(1)) * 255));
        };

        return channel(color.redComponent) << 16 | channel(color.greenComponent) << 8 |
               channel(color.blueComponent);
    };

    return TitleColors{rgb(background), rgb(text)};
}

std::optional<float> title_font_pixels(SDL_Window *window) {
    NSWindow *handle = native(window);

    if (!handle) {
        return std::nullopt;
    }

    return static_cast<float>([NSFont titleBarFontOfSize:0].pointSize * handle.backingScaleFactor);
}
} // namespace voyemu::window_shell
