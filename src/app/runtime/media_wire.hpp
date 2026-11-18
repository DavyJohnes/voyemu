#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace voyemu::media {
using Bytes = std::vector<std::uint8_t>;
// The largest RGBA frame of one screen, see screen_pixel_limit.
constexpr std::size_t maximum_pixel_bytes = static_cast<std::size_t>(1920u * 1080u) * 4u;
constexpr std::size_t maximum_message = maximum_pixel_bytes + 65536;
// One emulator display at the size of its profile screen.
struct GuestDisplay {
    unsigned id{}, width{}, height{};

    std::size_t pixel_bytes() const {
        return static_cast<std::size_t>(width) * height * 4u;
    }

    bool operator==(const GuestDisplay &) const = default;
};
struct Image {
    unsigned display{}, width{}, height{};
    std::uint32_t sequence{};
    std::uint64_t timestamp_us{};
    Bytes storage;
    std::size_t pixel_offset{};

    std::span<const std::uint8_t> pixels() const {
        return {storage.data() + pixel_offset, static_cast<std::size_t>(width) * height * 4u};
    }
};
Bytes image_request(GuestDisplay display, std::string_view shared_handle = {});
Bytes frame_change_request(GuestDisplay display, std::string_view shared_handle);
Bytes touch_request(GuestDisplay display, int x, int y, bool down);
Bytes key_request(std::string_view key, bool text = false);
Bytes grpc_message(std::span<const std::uint8_t> body);
std::optional<Image> decode_image(Bytes storage, GuestDisplay expected);
std::optional<Image> decode_mapped_image(Bytes metadata, GuestDisplay expected,
                                         std::span<const std::uint8_t> pixels,
                                         std::string_view handle);
struct FrameMetadata {
    unsigned display, width, height;
    std::uint32_t sequence;
    std::uint64_t timestamp_us;
};
std::optional<FrameMetadata> decode_mapped_metadata(Bytes metadata, GuestDisplay expected,
                                                    std::string_view handle);
std::optional<FrameMetadata> decode_frame_change(Bytes metadata, GuestDisplay expected,
                                                 std::string_view handle);
class MessageStream {
public:
    using Receiver = std::function<void(Bytes)>;

    explicit MessageStream(Receiver receiver) : receiver_(std::move(receiver)) {}

    void feed(std::span<const std::uint8_t> data);

    bool incomplete() const {
        return header_used_ != 0 || !body_.empty();
    }

private:
    Receiver receiver_;
    std::array<std::uint8_t, 5> header_{};
    std::size_t header_used_{}, body_used_{};
    Bytes body_;
};
} // namespace voyemu::media
