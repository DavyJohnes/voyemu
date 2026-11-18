
#pragma once
#include <cstdint>
namespace android { namespace hardware { namespace graphics { namespace common {
namespace V1_0 {
enum class Transform : int32_t { None=0, FLIP_H=1, FLIP_V=2, ROT_180=3, ROT_90=4, ROT_270=5, INVALID=8 };
enum class Dataspace : int32_t { UNKNOWN=0, ARBITRARY=1, STANDARD_SHIFT=16, STANDARD_MASK=4128768, TRANSFORM_SHIFT=21, TRANSFORM_MASK=62914560 };
enum class ColorMode : int32_t {};
enum class BufferUsage : uint64_t {};
enum class Hdr : int32_t {};
} // V1_0
namespace V1_1 {
enum class PixelFormat : uint32_t {
    RGBA_8888=1, RGBX_8888=2, RGB_888=3, RGB_565=4, BLOB=0x21, BLOB_DEPTH=0x22,
    YCBCR_420_888=0x23, BLOB_HW_COMPARE=0x24,
    DEPTH_16=0x30, DEPTH_24=0x31, DEPTH_24_STENCIL_8=0x32, DEPTH_32F=0x33, DEPTH_32F_STENCIL_8=0x34, STENCIL_8=0x35, YCBCR_P010=0x36
};
enum class ColorMode : int32_t {}; enum class Dataspace : int32_t {};
enum class RenderIntent : int32_t { COLORIMETRIC=0, ENHANCE=1, TONE_MAP_COLORIMETRIC=2, TONE_MAP_ENHANCE=3 };
} // V1_1
} } } }
