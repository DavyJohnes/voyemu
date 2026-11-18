#include "qnx_frame_grpc_wire.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

using voyemu::media::Bytes;
namespace wire = voyemu::frames::grpc_wire;
using voyemu::frames::Frame;

namespace {
std::span<const std::uint8_t> body(const Bytes &message) {
    assert(message.size() >= 5 && message[0] == 0);
    const auto size = (std::uint32_t(message[1]) << 24) | (std::uint32_t(message[2]) << 16) |
                      (std::uint32_t(message[3]) << 8) | message[4];
    assert(size == message.size() - 5);

    return std::span(message).subspan(5);
}

std::span<const std::uint8_t> small_image(const Bytes &message, std::uint8_t format,
                                          std::uint8_t width, std::uint8_t height) {
    auto bytes = body(message);
    const std::array<std::uint8_t, 9> prefix{0x0a,  6,    0x08,   format, 0x18,
                                             width, 0x20, height, 0x22};

    assert(bytes.size() >= prefix.size() &&
           std::equal(prefix.begin(), prefix.end(), bytes.begin()));
    bytes = bytes.subspan(prefix.size());
    std::size_t length = 0;

    for (unsigned shift = 0;; shift += 7) {
        assert(shift <= 21 && !bytes.empty());
        const auto byte = bytes.front();
        bytes = bytes.subspan(1);
        length |= std::size_t(byte & 127) << shift;

        if (!(byte & 128)) {
            break;
        }
    }

    assert(bytes.size() >= length);

    return bytes.first(length);
}

void rejects(std::initializer_list<std::uint8_t> data) {
    bool rejected = false;

    try {
        wire::parse_request({data.begin(), data.size()});
    } catch (const std::runtime_error &) {
        rejected = true;
    }

    assert(rejected);
}
} // namespace

int main() try {
    using namespace std::chrono;
    using wire::PixelFormat;
    assert(wire::parse_request({}).format == PixelFormat::png);
    const Bytes request{0x08, 2, 0x18, 12, 0x20, 3, 0x28, 0, 0x32, 0};
    const auto rgb = wire::parse_request(request);
    assert(rgb.format == PixelFormat::rgb888 && rgb.width == 12 && rgb.height == 3);
    rejects({0x08, 3});                            // Unknown pixel format.
    rejects({0x28, 1});                            // QNX has one display.
    rejects({0x32, 2, 0x08, 1});                   // MMAP transport.
    rejects({0x32, 3, 0x12, 1, 'x'});              // Client-provided handle.
    rejects({0x32, 2, 0x08});                      // Truncated submessage.
    rejects({0x18, 0x80});                         // Truncated varint.
    rejects({0x18, 0x80, 0x80, 0x80, 0x80, 0x10}); // uint32 overflow.
    rejects({0x1a, 0});                            // Wrong wire type for width.

    Frame frame;
    frame.bgra.resize(voyemu::frames::pixel_bytes);

    for (std::size_t offset = 0; offset < frame.bgra.size(); offset += 4) {
        const bool top = offset < frame.bgra.size() / 2;
        frame.bgra[offset] = top ? std::byte{7} : std::byte{91};
        frame.bgra[offset + 1] = top ? std::byte{31} : std::byte{13};
        frame.bgra[offset + 2] = top ? std::byte{211} : std::byte{2};
        frame.bgra[offset + 3] = std::byte{255};
    }

    const auto expected_timestamp =
            duration_cast<microseconds>((system_clock::now() - seconds(3)).time_since_epoch())
                    .count();
    frame.timestamp_ns =
            duration_cast<nanoseconds>((steady_clock::now() - seconds(3)).time_since_epoch())
                    .count();

    const auto rgba = wire::encode_frame({PixelFormat::rgba8888}, frame, 9);
    const auto latest_timestamp =
            duration_cast<microseconds>((system_clock::now() - seconds(3)).time_since_epoch())
                    .count();
    const auto payload = body(rgba);
    const auto decoded = voyemu::media::decode_image(Bytes(payload.begin(), payload.end()),
                                                     {0, 1920, 720});
    assert(decoded && decoded->sequence == 9 && decoded->width == frame.width &&
           decoded->height == frame.height);
    const auto pixels = decoded->pixels();
    const std::array<std::uint8_t, 4> top{211, 31, 7, 255}, bottom{2, 13, 91, 255};
    assert(std::equal(top.begin(), top.end(), pixels.begin()));
    assert(std::equal(bottom.begin(), bottom.end(), pixels.end() - 4));
    assert(decoded->timestamp_us >= static_cast<std::uint64_t>(expected_timestamp - 100000) &&
           decoded->timestamp_us <= static_cast<std::uint64_t>(latest_timestamp + 100000));

    const auto scaled = wire::encode_frame(rgb, frame, 0);
    const auto rgb_pixels = small_image(scaled, 2, 8, 3);
    assert(rgb_pixels.size() == 8 * 3 * 3);
    assert(std::equal(top.begin(), top.begin() + 3, rgb_pixels.begin()));
    assert(std::equal(bottom.begin(), bottom.begin() + 3, rgb_pixels.end() - 3));

    const auto png = wire::encode_frame({PixelFormat::png, 8}, frame, 0);
    const auto png_pixels = small_image(png, 0, 8, 3);
    const std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    assert(png_pixels.size() >= 24 &&
           std::equal(signature.begin(), signature.end(), png_pixels.begin()));
    assert(png_pixels[19] == 8 && png_pixels[23] == 3); // IHDR width and height.

    // A full opaque frame with incompressible colors must fit the same gRPC bound.
    std::uint32_t noise = 0x13579bdf;

    for (std::size_t offset = 0; offset < frame.bgra.size(); offset += 4) {
        noise ^= noise << 13;
        noise ^= noise >> 17;
        noise ^= noise << 5;

        for (unsigned channel = 0; channel < 3; ++channel) {
            frame.bgra[offset + channel] =
                    std::byte(static_cast<std::uint8_t>(noise >> (channel * 8)));
        }
    }

    const auto full_png = wire::encode_frame({}, frame, 0);
    const auto png_body = body(full_png);
    assert(png_body.size() > 3 * 1024 * 1024 && png_body.size() <= voyemu::media::maximum_message);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
