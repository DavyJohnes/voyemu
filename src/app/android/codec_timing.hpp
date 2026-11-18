#pragma once
#include "model.hpp"
#include <string>
#include <string_view>
namespace voyemu {
struct AndroidCodecTimingPatch {
    std::string bytes;
    Json receipt;
};
// Pinned API28 ARM64 CCodec allocation budget only; no codec selection or other deadlines change.
AndroidCodecTimingPatch android_codec_timing_patch(std::string_view input, bool omx_layout = true);
} // namespace voyemu
