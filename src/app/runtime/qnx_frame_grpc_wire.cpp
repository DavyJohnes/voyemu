#include "qnx_frame_grpc_wire.hpp"
#include "error.hpp"

#include "capture_png.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <utility>

namespace voyemu::frames::grpc_wire {
namespace {
[[noreturn]] void invalid_request() {
    throw VoyemuError("QNX_FRAME_GRPC", "invalid ImageFormat request");
}

class ProtoReader {
public:
    explicit ProtoReader(std::span<const std::uint8_t> bytes) : remaining_(bytes) {}

    bool empty() const {
        return remaining_.empty();
    }

    std::pair<unsigned, unsigned> tag() {
        const auto value = varint();
        const auto field = value >> 3;

        if (field == 0 || field >= (std::uint64_t{1} << 29)) {
            invalid_request();
        }

        return {static_cast<unsigned>(field), static_cast<unsigned>(value & 7)};
    }

    std::uint32_t integer(unsigned wire) {
        if (wire != 0) {
            invalid_request();
        }

        const auto value = varint();

        if (value > std::numeric_limits<std::uint32_t>::max()) {
            invalid_request();
        }

        return static_cast<std::uint32_t>(value);
    }

    std::span<const std::uint8_t> bytes(unsigned wire) {
        if (wire != 2) {
            invalid_request();
        }

        const auto size = varint();

        if (size > remaining_.size()) {
            invalid_request();
        }

        return consume(static_cast<std::size_t>(size));
    }

    void skip(unsigned wire) {
        switch (wire) {
        case 0:
            varint();
            break;
        case 1:
            consume(8);
            break;
        case 2:
            bytes(wire);
            break;
        case 5:
            consume(4);
            break;
        default:
            invalid_request();
        }
    }

private:
    std::span<const std::uint8_t> consume(std::size_t size) {
        if (size > remaining_.size()) {
            invalid_request();
        }

        const auto value = remaining_.first(size);
        remaining_ = remaining_.subspan(size);

        return value;
    }

    std::uint64_t varint() {
        std::uint64_t value = 0;

        for (unsigned shift = 0; shift < 64; shift += 7) {
            const auto byte = consume(1).front();

            if (shift == 63 && byte > 1) {
                invalid_request();
            }

            value |= std::uint64_t(byte & 127) << shift;

            if (!(byte & 128)) {
                return value;
            }
        }

        invalid_request();
    }

    std::span<const std::uint8_t> remaining_;
};

void check_inline_transport(std::span<const std::uint8_t> body) {
    ProtoReader transport(body);

    while (!transport.empty()) {
        const auto [field, wire] = transport.tag();

        if (field == 1) {
            if (transport.integer(wire) != 0) {
                invalid_request();
            }
        } else if (field == 2) {
            if (!transport.bytes(wire).empty()) {
                invalid_request();
            }
        } else {
            transport.skip(wire);
        }
    }
}

void append_varint(media::Bytes &bytes, std::uint64_t value) {
    while (value >= 128) {
        bytes.push_back(static_cast<std::uint8_t>(value & 127) | 128);
        value >>= 7;
    }

    bytes.push_back(static_cast<std::uint8_t>(value));
}

void append_integer(media::Bytes &bytes, unsigned field, std::uint64_t value) {
    append_varint(bytes, field << 3);
    append_varint(bytes, value);
}

void append_bytes(media::Bytes &bytes, unsigned field, std::span<const std::uint8_t> value) {
    append_varint(bytes, (field << 3) | 2);
    append_varint(bytes, value.size());
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::pair<unsigned, unsigned> output_size(const Request &request) {
    const auto max_width = request.width == 0 ? width : std::min(request.width, width);
    const auto max_height = request.height == 0 ? height : std::min(request.height, height);

    if (max_width * height > max_height * width) {
        return {std::max(1u, max_height * width / height), max_height};
    }

    return {max_width, std::max(1u, max_width * height / width)};
}

std::uint64_t unix_timestamp_us(std::uint64_t source_ns) {
    // QNX's host producer timestamps the published Screen frame with steady_clock.
    using namespace std::chrono;
    const auto steady_ns =
            duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
    const auto wall_us =
            duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();

    if (!source_ns || steady_ns < 0 || source_ns > static_cast<std::uint64_t>(steady_ns) ||
        wall_us <= 0) {
        return 0;
    }

    const auto age_us = (static_cast<std::uint64_t>(steady_ns) - source_ns) / 1000;

    return age_us < static_cast<std::uint64_t>(wall_us)
                   ? static_cast<std::uint64_t>(wall_us) - age_us
                   : 0;
}

media::Bytes image_bytes(PixelFormat format, const Frame &frame, unsigned out_width,
                         unsigned out_height) {
    const auto *source = reinterpret_cast<const std::uint8_t *>(frame.bgra.data());

    if (format == PixelFormat::png && out_width == width && out_height == height) {
        return output::png_frame({source, pixel_bytes}, width, height, true);
    }

    const unsigned channels = format == PixelFormat::rgb888 ? 3 : 4;
    media::Bytes pixels(std::size_t(out_width) * out_height * channels);

    // The native SDK consumer displays its inline pixels directly in top-to-bottom order.
    for (unsigned y = 0; y < out_height; ++y) {
        const auto source_y = ((2 * y + 1) * height) / (2 * out_height);

        for (unsigned x = 0; x < out_width; ++x) {
            const auto source_x = ((2 * x + 1) * width) / (2 * out_width);
            const auto *bgra = source + std::size_t(source_y) * stride + std::size_t(source_x) * 4;
            auto *pixel = pixels.data() + (std::size_t(y) * out_width + x) * channels;
            pixel[0] = bgra[2];
            pixel[1] = bgra[1];
            pixel[2] = bgra[0];

            if (channels == 4) {
                pixel[3] = bgra[3];
            }
        }
    }

    if (format == PixelFormat::png) {
        return output::png_frame(pixels, out_width, out_height);
    }

    return pixels;
}
} // namespace

// ImageFormat and Image fields from the pinned Android SDK's emulator_controller.proto.
Request parse_request(std::span<const std::uint8_t> body) {
    if (body.size() > maximum_request_size) {
        invalid_request();
    }

    ProtoReader reader(body);
    Request request;

    while (!reader.empty()) {
        const auto [field, wire] = reader.tag();

        switch (field) {
        case 1: {
            const auto format = reader.integer(wire);

            if (format > static_cast<unsigned>(PixelFormat::rgb888)) {
                invalid_request();
            }

            request.format = static_cast<PixelFormat>(format);
            break;
        }
        case 3:
            request.width = reader.integer(wire);
            break;
        case 4:
            request.height = reader.integer(wire);
            break;
        case 5:
            if (reader.integer(wire) != 0) {
                invalid_request();
            }

            break;
        case 6:
            check_inline_transport(reader.bytes(wire));
            break;
        default:
            reader.skip(wire);
            break;
        }
    }

    return request;
}

media::Bytes encode_frame(const Request &request, const Frame &frame, std::uint32_t sequence) {
    if (frame.width != width || frame.height != height || frame.stride != stride ||
        frame.bgra.size() != pixel_bytes) {
        throw VoyemuError("QNX_FRAME_GRPC", "complete QNX Screen frame required");
    }

    const auto [out_width, out_height] = output_size(request);
    media::Bytes descriptor;
    append_integer(descriptor, 1, static_cast<unsigned>(request.format));
    append_integer(descriptor, 3, out_width);
    append_integer(descriptor, 4, out_height);

    media::Bytes body;
    {
        const auto pixels = image_bytes(request.format, frame, out_width, out_height);
        body.reserve(pixels.size() + 64);
        append_bytes(body, 1, descriptor);
        append_bytes(body, 4, pixels);
    }
    append_integer(body, 5, sequence);
    append_integer(body, 6, unix_timestamp_us(frame.timestamp_ns));

    return media::grpc_message(body);
}
} // namespace voyemu::frames::grpc_wire
