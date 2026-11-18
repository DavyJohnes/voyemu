#pragma once
#include "os/video_decoder.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace voyemu {
// One OEM startup video (MP4): the sample tables of its H.264 track.
struct StartupVideo {
    struct Sample {
        std::uint64_t offset{};
        std::uint32_t bytes{};
    };
    unsigned width{}, height{};
    // Constant frame duration from the sample timing table.
    double fps{};
    // Sequence and picture parameter sets as NAL units; level_idc is lowered to the lowest level
    // the stream fits, see startup_video_level.
    std::vector<std::string> parameter_sets;
    // The colour description of the sequence: BT.709 or BT.601 matrix, full or studio range.
    bool bt709 = true, full_range = false;
    unsigned length_bytes{};
    std::vector<Sample> frames;
};
// H.264 level_idc that host decoders accept for a stream whose own level is higher than it needs:
// 5.1, or 5.2 at a higher macroblock rate, when the frame size, macroblock rate and reference
// frames fit; otherwise the level unchanged. The Media Foundation decoder documents levels up to
// 5.1, and the OEM videos declare 6.0 for 3840x1440 frames that fit 5.1.
unsigned startup_video_level(unsigned level, unsigned macroblocks, double fps,
                             unsigned reference_frames);
StartupVideo read_startup_video(std::string_view mp4);
using StartupVideoDecoder =
        std::function<std::unique_ptr<os::VideoDecoder>(const os::VideoStream &)>;
// Writes the video into output as a boot animation archive in the format of the window's startup
// player: stored PNG frames at the video's own size in part0/ and a desc.txt at its frame rate, cut
// to the OEM player limit of `seconds`. Without a host decoder, or when it rejects the video, the
// archive holds one black frame for the same duration. Returns whether it holds the decoded
// frames.
bool write_startup_video_animation(std::string_view mp4, double seconds,
                                   const std::filesystem::path &output,
                                   const StartupVideoDecoder &decoder = os::host_video_decoder);
} // namespace voyemu
