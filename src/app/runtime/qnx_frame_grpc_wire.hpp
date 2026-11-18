#pragma once

#include "frame_transport.hpp"
#include "media_wire.hpp"

namespace voyemu::frames::grpc_wire {
inline constexpr std::size_t maximum_request_size = 4096;

enum class PixelFormat : unsigned {
    png = 0,
    rgba8888 = 1,
    rgb888 = 2
};

struct Request {
    PixelFormat format = PixelFormat::png;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

Request parse_request(std::span<const std::uint8_t> body);
media::Bytes encode_frame(const Request &request, const Frame &frame, std::uint32_t sequence);
} // namespace voyemu::frames::grpc_wire
