#include "capture_png.hpp"
#include "error.hpp"
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <stdexcept>
namespace voyemu::output {
std::vector<std::uint8_t> png_frame(std::span<const std::uint8_t> pixels, unsigned width,
                                    unsigned height, bool bgra) {
    if (!width || !height || width > 3840 || height > 2160 ||
        pixels.size() != std::size_t(width) * height * 4) {
        throw VoyemuError("TEST_FRAME", "complete bounded pixels required");
    }

    CGColorSpaceRef color = CGColorSpaceCreateDeviceRGB();
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, pixels.data(), pixels.size(),
                                                              nullptr);
    const auto flags = bgra ? CGBitmapInfo(kCGBitmapByteOrder32Little) |
                                       CGBitmapInfo(kCGImageAlphaNoneSkipFirst)
                            : CGBitmapInfo(kCGBitmapByteOrder32Big) |
                                       CGBitmapInfo(kCGImageAlphaLast);
    CGImageRef image = CGImageCreate(width, height, 8, 32, std::size_t(width) * 4, color, flags,
                                     provider, nullptr, false, kCGRenderingIntentDefault);
    CFMutableDataRef data = CFDataCreateMutable(kCFAllocatorDefault, 0);
    CGImageDestinationRef destination =
            data ? CGImageDestinationCreateWithData(data, CFSTR("public.png"), 1, nullptr)
                 : nullptr;
    bool complete = false;

    if (image && destination) {
        CGImageDestinationAddImage(destination, image, nullptr);
        complete = CGImageDestinationFinalize(destination);
    }

    std::vector<std::uint8_t> result;

    if (complete) {
        result.assign(CFDataGetBytePtr(data), CFDataGetBytePtr(data) + CFDataGetLength(data));
    }

    if (destination) {
        CFRelease(destination);
    }

    if (data) {
        CFRelease(data);
    }

    if (image) {
        CGImageRelease(image);
    }

    if (provider) {
        CGDataProviderRelease(provider);
    }

    if (color) {
        CGColorSpaceRelease(color);
    }

    if (!complete) {
        throw VoyemuError("TEST_PNG", "original frame encoding failed");
    }

    return result;
}
} // namespace voyemu::output
