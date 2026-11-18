#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace voyemu::os {
// One H.264 stream: its frame size, its range and its sequence and picture parameter sets as NAL
// units without start codes.
struct VideoStream {
    unsigned width{}, height{};
    bool full_range{};
    std::vector<std::string> parameter_sets;
};
// One decoded 8-bit 4:2:0 picture in decoder memory, valid only during the sink call. It may be
// larger than the stream when the decoder keeps the coded size. Planar chroma has step 1;
// interleaved chroma (NV12) has v == u + 1 and step 2.
struct VideoPicture {
    unsigned width{}, height{};
    const std::uint8_t *luma{}, *u{}, *v{};
    std::size_t luma_stride{}, chroma_stride{};
    unsigned chroma_step{1};
};
using VideoPictureSink = std::function<void(const VideoPicture &)>;
// The host's own H.264 decoder for one stream, fed whole access units in decoding order. It throws
// when it rejects the stream.
class VideoDecoder {
public:
    VideoDecoder() = default;
    virtual ~VideoDecoder() = default;
    VideoDecoder(const VideoDecoder &) = delete;
    VideoDecoder &operator=(const VideoDecoder &) = delete;
    // One access unit as NAL units without start codes or length prefixes.
    virtual void decode(const std::vector<std::string_view> &units,
                        const VideoPictureSink &sink) = 0;
    // The pictures the decoder still holds after the last access unit.
    virtual void flush(const VideoPictureSink &sink) = 0;
};
// The decoder of this host for the stream: VideoToolbox on macOS, the Media Foundation H.264
// decoder on Windows and the system libavcodec on Linux. Throws when the host has none or it
// refuses the stream.
std::unique_ptr<VideoDecoder> host_video_decoder(const VideoStream &stream);

// The Annex B byte stream of NAL units, as the Media Foundation and libavcodec decoders take it.
inline std::string annex_b(const std::vector<std::string_view> &units) {
    std::string stream;

    for (const auto unit : units) {
        stream.append("\0\0\0\1", 4).append(unit);
    }

    return stream;
}
} // namespace voyemu::os
