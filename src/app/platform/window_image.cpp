#include "window_image.hpp"
#include "error.hpp"
#include <SDL3/SDL.h>
#include <memory>
#include <stdexcept>
#include <string>

namespace voyemu::window {
namespace {
struct SurfaceDeleter {
    void operator()(SDL_Surface *surface) const {
        SDL_DestroySurface(surface);
    }
};
using Surface = std::unique_ptr<SDL_Surface, SurfaceDeleter>;

std::string utf8(const std::filesystem::path &path) {
    const auto text = path.u8string();

    return {text.begin(), text.end()};
}

Image decode(SDL_Surface *decoded, const std::string &name) {
    Surface loaded(decoded);

    if (!loaded) {
        throw VoyemuError("WINDOW_IMAGE", "cannot decode " + name + ": " + SDL_GetError());
    }

    // SDL_ConvertPixels takes no palette, so an indexed image is expanded to a surface first;
    // any other image converts straight into the result without an intermediate copy.
    Surface expanded;
    const SDL_Surface *source = loaded.get();

    if (SDL_ISPIXELFORMAT_INDEXED(loaded->format)) {
        expanded.reset(SDL_ConvertSurface(loaded.get(), SDL_PIXELFORMAT_ABGR8888));
        source = expanded.get();
    }

    Image image{loaded->w, loaded->h, {}};
    image.rgba.resize(std::size_t(image.width) * image.height * 4);

    // ABGR8888 stores bytes R, G, B, A in memory on every supported little-endian host.
    if (!source ||
        !SDL_ConvertPixels(image.width, image.height, source->format, source->pixels, source->pitch,
                           SDL_PIXELFORMAT_ABGR8888, image.rgba.data(), image.width * 4)) {
        throw VoyemuError("WINDOW_IMAGE", "cannot convert " + name + ": " + SDL_GetError());
    }

    return image;
}
} // namespace

Image decode_png(const std::filesystem::path &path) {
    return decode(SDL_LoadPNG(utf8(path).c_str()), utf8(path));
}

Image decode_png_bytes(std::string_view bytes) {
    return decode(SDL_LoadPNG_IO(SDL_IOFromConstMem(bytes.data(), bytes.size()), true),
                  "in-memory PNG");
}

void save_png(const std::filesystem::path &path, int width, int height,
              const std::vector<std::uint8_t> &bgra) {
    if (width <= 0 || height <= 0 || bgra.size() != std::size_t(width) * height * 4) {
        throw VoyemuError("WINDOW_IMAGE", "incomplete capture");
    }

    // ARGB8888 stores bytes B, G, R, A in memory on little-endian hosts.
    Surface surface(SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_ARGB8888,
                                          const_cast<std::uint8_t *>(bgra.data()), width * 4));

    if (!surface || !SDL_SavePNG(surface.get(), utf8(path).c_str())) {
        throw VoyemuError("WINDOW_IMAGE", "cannot write " + utf8(path) + ": " + SDL_GetError());
    }
}
} // namespace voyemu::window
