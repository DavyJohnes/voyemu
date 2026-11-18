#include "capture_png.hpp"
#include "model.hpp"
#include "process.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
using namespace voyemu;

static void decoded_rgba_identity(std::span<const std::uint8_t> png,
                                  std::span<const std::uint8_t> expected, unsigned width,
                                  unsigned height) {
    CFDataRef data = CFDataCreate(nullptr, png.data(), png.size());
    auto source = CGImageSourceCreateWithData(data, nullptr);

    assert(source);
    auto image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    assert(image && CGImageGetWidth(image) == width && CGImageGetHeight(image) == height &&
           CGImageGetBitsPerComponent(image) == 8 && CGImageGetBitsPerPixel(image) == 32);
    const auto decoded = CGDataProviderCopyData(CGImageGetDataProvider(image));
    assert(decoded && CFDataGetLength(decoded) == static_cast<CFIndex>(expected.size()));
    assert(std::equal(expected.begin(), expected.end(), CFDataGetBytePtr(decoded)));
    CFRelease(decoded);
    CGImageRelease(image);
    CFRelease(source);
    CFRelease(data);
}

static void smooth_panel_footprint() {
    constexpr unsigned width = 1920, height = 720;
    std::vector<std::uint8_t> pixels(std::size_t(width) * height * 4);

    for (unsigned y = 0; y < height; ++y) {
        for (unsigned x = 0; x < width; ++x) {
            const auto index = (std::size_t(y) * width + x) * 4;
            unsigned red = 18 + x / 128 + y / 96, green = red + 2, blue = red + 5;
            const unsigned panel = x / 640, local_x = x % 640;

            if (local_x >= 40 && local_x < 600 && y >= 90 && y < 650) {
                red = green = 40 + (y - 90) / 80;
                blue = red + 4;

                if (local_x >= 72 && local_x < 560 && y >= 150 && y < 600) {
                    const auto row = (y - 150) % 90;

                    if (row < 12 && local_x < 300) {
                        red = green = blue = 190 + local_x / 40;
                    } else if (row >= 34 && row < 50) {
                        red = 55 + panel * 12;
                        green = 110 + local_x / 32;
                        blue = 180 + local_x / 40;
                    }
                }
            }

            pixels[index] = static_cast<std::uint8_t>(red);
            pixels[index + 1] = static_cast<std::uint8_t>(green);
            pixels[index + 2] = static_cast<std::uint8_t>(blue);
            pixels[index + 3] = 255;
        }
    }

    const auto png = output::png_frame(pixels, width, height);
    decoded_rgba_identity(png, pixels, width, height);
    // A smooth OEM-sized panel must remain substantially smaller than stored RGBA scanlines.
    assert(png.size() < pixels.size() / 6);
    std::cout << "PNG smooth-panel1920x720: raw=" << pixels.size() << " encoded=" << png.size()
              << "; full decoded RGBA identity verified\n";
}

int main() try {
    const std::array<std::uint8_t, 16> rgba{255, 0, 0,   255, 255, 0, 0,   255,
                                            0,   0, 255, 255, 0,   0, 255, 255};
    const auto png = output::png_frame(rgba, 2, 2);

    assert(png.size() > 32 && png[0] == 137 && png[1] == 'P' && png[2] == 'N' && png[3] == 'G');
    decoded_rgba_identity(png, rgba, 2, 2);
    const std::array<std::uint8_t, 16> alpha{20,  80, 160, 17,  220, 140, 40,  93,
                                             110, 30, 200, 176, 250, 60,  120, 255};
    decoded_rgba_identity(output::png_frame(alpha, 2, 2), alpha, 2, 2);
    smooth_panel_footprint();
    bool rejected = false;
    try {
        output::png_frame(rgba, 3, 2);
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
    const auto root = std::filesystem::path(__FILE__)
                              .parent_path()
                              .parent_path()
                              .parent_path()
                              .parent_path() /
                      ".local/build/tmp" / ("test-png-" + random_token());
    ensure_private_directory(root);
    output::write_artifact(root / "frame.png", png);
    assert(std::filesystem::file_size(root / "frame.png") == png.size());
    const auto before = sha256_file(root / "frame.png");
    rejected = false;
    try {
        output::write_artifact(root / "frame.png", png);
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected && sha256_file(root / "frame.png") == before);
    std::filesystem::create_symlink(root / "frame.png", root / "linked.png");
    rejected = false;
    try {
        output::write_artifact(root / "linked.png", png);
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected && sha256_file(root / "frame.png") == before);
    std::filesystem::remove_all(root);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
} catch (...) {
    std::cerr << "unexpected non-standard exception\n";

    return 1;
}
