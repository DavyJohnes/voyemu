#include "media_wire.hpp"
#include "error.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>

namespace voyemu::media {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("MEDIA_PROTO", "invalid bounded SDK frame");
}

void varint(Bytes &result, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 127);
        value >>= 7;
        result.push_back(byte | (value ? 128 : 0));
    } while (value);
}

void integer(Bytes &result, unsigned field, std::uint64_t value) {
    varint(result, static_cast<std::uint64_t>(field) * 8);
    varint(result, value);
}

void bytes(Bytes &result, unsigned field, std::span<const std::uint8_t> value) {
    varint(result, field * 8 + 2);
    varint(result, value.size());
    result.insert(result.end(), value.begin(), value.end());
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> source) : source_(source) {}

    bool remaining() const {
        return offset_ < source_.size();
    }

    std::uint64_t number() {
        std::uint64_t value = 0;

        for (unsigned i = 0; i < 10; ++i) {
            if (!remaining()) {
                invalid();
            }

            const auto byte = source_[offset_++];

            if (i == 9 && byte > 1) {
                invalid();
            }

            value |= std::uint64_t(byte & 127) << (i * 7);

            if (!(byte & 128)) {
                return value;
            }
        }

        invalid();
    }

    std::span<const std::uint8_t> data() {
        auto size = number();

        if (size > source_.size() - offset_) {
            invalid();
        }

        auto value = source_.subspan(offset_, size);
        offset_ += size;

        return value;
    }

    void skip(unsigned wire) {
        if (wire == 0) {
            number();

            return;
        }

        if (wire == 2) {
            data();

            return;
        }

        if (wire != 1 && wire != 5) {
            invalid();
        }

        const auto size = wire == 1 ? 8u : 4u;

        if (size > source_.size() - offset_) {
            invalid();
        }

        offset_ += size;
    }

private:
    std::span<const std::uint8_t> source_;
    std::size_t offset_{};
};

// An emulator display number of a profile screen whose frame stays within the bound.
void target(GuestDisplay display) {
    if (display.id > 10 || display.width == 0 || display.height == 0 || display.width > 4096 ||
        display.height > 4096 || display.pixel_bytes() > maximum_pixel_bytes) {
        throw VoyemuError("MEDIA_DISPLAY", "expected a profile screen of the emulator");
    }
}

// The change signal keeps the aspect ratio of the screen: 8x3 for 1920x720.
GuestDisplay change_signal(GuestDisplay display) {
    const auto divisor = std::gcd(display.width, display.height);

    return {display.id, display.width / divisor, display.height / divisor};
}
} // namespace

static Bytes request_image(GuestDisplay display, std::string_view shared_handle) {
    target(display);
    Bytes value;
    integer(value, 1, 1);
    integer(value, 3, display.width);
    integer(value, 4, display.height);
    integer(value, 5, display.id);

    if (!shared_handle.empty()) {
        if (!shared_handle.starts_with("file:///") || shared_handle.size() > 4096 ||
            shared_handle.find_first_of("\r\n") != std::string_view::npos ||
            shared_handle.find('\0') != std::string_view::npos) {
            invalid();
        }

        Bytes transport;
        integer(transport, 1, 1);
        bytes(transport, 2,
              {reinterpret_cast<const std::uint8_t *>(shared_handle.data()), shared_handle.size()});
        bytes(value, 6, transport);
    }

    return value;
}

Bytes image_request(GuestDisplay display, std::string_view shared_handle) {
    return request_image(display, shared_handle);
}

// The signal only reports changes; displayed captures keep the screen size.
Bytes frame_change_request(GuestDisplay display, std::string_view shared_handle) {
    if (shared_handle.empty()) {
        invalid();
    }

    target(display);

    return request_image(change_signal(display), shared_handle);
}

Bytes touch_request(GuestDisplay display, int x, int y, bool down) {
    target(display);

    if (x < 0 || unsigned(x) >= display.width || y < 0 || unsigned(y) >= display.height) {
        throw VoyemuError("MEDIA_TOUCH", "coordinate outside guest display");
    }

    Bytes touch;
    integer(touch, 1, x);
    integer(touch, 2, y);
    integer(touch, 3, 1);
    integer(touch, 4, down ? 1024 : 0);
    Bytes value;
    bytes(value, 1, touch);
    integer(value, 2, display.id);

    return value;
}

Bytes key_request(std::string_view key, bool text) {
    if (key.empty() || key.size() > 4096) {
        throw VoyemuError("MEDIA_KEY", "bounded key or text required");
    }

    Bytes value;
    integer(value, 2, 2);
    bytes(value, text ? 5 : 4, {reinterpret_cast<const std::uint8_t *>(key.data()), key.size()});

    return value;
}

Bytes grpc_message(std::span<const std::uint8_t> body) {
    if (body.size() > maximum_message) {
        invalid();
    }

    Bytes result(5);

    for (unsigned i = 0; i < 4; ++i) {
        result[4 - i] = static_cast<std::uint8_t>(body.size() >> (i * 8));
    }

    result.insert(result.end(), body.begin(), body.end());

    return result;
}

static std::optional<Image> decode_frame(Bytes storage, GuestDisplay expected,
                                         std::span<const std::uint8_t> mapped,
                                         std::string_view handle, bool metadata_only = false,
                                         bool signal = false) {
    target(expected);
    const auto size = signal ? change_signal(expected) : expected;

    if (storage.size() > maximum_message) {
        invalid();
    }

    Image image;
    unsigned format = 0, seen = 0, channel = 0;
    std::string returned_handle;
    std::span<const std::uint8_t> pixels;
    Reader message(storage);

    while (message.remaining()) {
        const auto tag = message.number();
        const auto field = tag >> 3;
        const auto wire = unsigned(tag & 7);

        if (field == 0) {
            invalid();
        }

        if (field == 1) {
            if (wire != 2 || (seen & 1)) {
                invalid();
            }

            seen |= 1;
            Reader descriptor(message.data());
            unsigned descriptor_seen = 0;

            while (descriptor.remaining()) {
                const auto key = descriptor.number();
                const auto number = key >> 3;
                const auto kind = unsigned(key & 7);

                if (!number) {
                    invalid();
                }

                if (number == 1 || number == 3 || number == 4 || number == 5) {
                    if (kind != 0 || (descriptor_seen & (1u << number))) {
                        invalid();
                    }

                    descriptor_seen |= 1u << number;
                    const auto value = descriptor.number();

                    if (value > std::numeric_limits<unsigned>::max()) {
                        invalid();
                    }

                    if (number == 1) {
                        format = value;
                    }

                    if (number == 3) {
                        image.width = value;
                    }

                    if (number == 4) {
                        image.height = value;
                    }

                    if (number == 5) {
                        image.display = value;
                    }
                } else if (number == 6) {
                    if (kind != 2 || (descriptor_seen & (1u << 6))) {
                        invalid();
                    }

                    descriptor_seen |= 1u << 6;
                    Reader transport(descriptor.data());
                    unsigned transport_seen = 0;

                    while (transport.remaining()) {
                        const auto t = transport.number();
                        const auto field = t >> 3, wire = t & 7;

                        if (!field) {
                            invalid();
                        }

                        if (field == 1) {
                            if (wire != 0 || (transport_seen & 1)) {
                                invalid();
                            }

                            transport_seen |= 1;
                            const auto c = transport.number();

                            if (c > 1) {
                                invalid();
                            }

                            channel = static_cast<unsigned>(c);
                        } else if (field == 2) {
                            if (wire != 2 || (transport_seen & 2)) {
                                invalid();
                            }

                            transport_seen |= 2;
                            auto text = transport.data();

                            if (text.size() > 4096) {
                                invalid();
                            }

                            returned_handle.assign(reinterpret_cast<const char *>(text.data()),
                                                   text.size());
                        } else {
                            transport.skip(static_cast<unsigned>(wire));
                        }
                    }
                } else {
                    descriptor.skip(kind);
                }
            }
        } else if (field == 4) {
            if (wire != 2 || (seen & 2)) {
                invalid();
            }

            seen |= 2;
            pixels = message.data();
        } else if (field == 5 || field == 6) {
            if (wire != 0 || (seen & (field == 5 ? 4 : 8))) {
                invalid();
            }

            seen |= field == 5 ? 4 : 8;
            const auto value = message.number();

            if (field == 5) {
                if (value > UINT32_MAX) {
                    invalid();
                }

                image.sequence = value;
            } else {
                image.timestamp_us = value;
            }
        } else {
            message.skip(wire);
        }
    }

    if (image.width == 0 && image.height == 0 && pixels.empty()) {
        return std::nullopt;
    }

    if (image.width != size.width || image.height != size.height || image.display != expected.id ||
        format != 1) {
        invalid();
    }

    if (!handle.empty()) {
        if (channel != 1 || returned_handle != handle || !pixels.empty() ||
            (!metadata_only && mapped.size() != expected.pixel_bytes())) {
            invalid();
        }

        // The caller performs unary requests serially: this owned copy completes before the next
        // SDK write.
        if (!metadata_only) {
            image.storage.assign(mapped.begin(), mapped.end());
        }

        image.pixel_offset = 0;
    } else {
        if (channel != 0 || pixels.size() != expected.pixel_bytes()) {
            invalid();
        }

        image.pixel_offset = static_cast<std::size_t>(pixels.data() - storage.data());
        image.storage = std::move(storage);
    }

    return image;
}

std::optional<Image> decode_image(Bytes storage, GuestDisplay expected) {
    return decode_frame(std::move(storage), expected, {}, {});
}

std::optional<Image> decode_mapped_image(Bytes metadata, GuestDisplay expected,
                                         std::span<const std::uint8_t> pixels,
                                         std::string_view handle) {
    if (handle.empty()) {
        invalid();
    }

    return decode_frame(std::move(metadata), expected, pixels, handle);
}

std::optional<FrameMetadata> decode_mapped_metadata(Bytes metadata, GuestDisplay expected,
                                                    std::string_view handle) {
    if (handle.empty()) {
        invalid();
    }

    auto image = decode_frame(std::move(metadata), expected, {}, handle, true);

    if (!image) {
        return std::nullopt;
    }

    return FrameMetadata{image->display, image->width, image->height, image->sequence,
                         image->timestamp_us};
}

std::optional<FrameMetadata> decode_frame_change(Bytes metadata, GuestDisplay expected,
                                                 std::string_view handle) {
    if (handle.empty()) {
        invalid();
    }

    auto image = decode_frame(std::move(metadata), expected, {}, handle, true, true);

    if (!image) {
        return std::nullopt;
    }

    return FrameMetadata{image->display, image->width, image->height, image->sequence,
                         image->timestamp_us};
}

void MessageStream::feed(std::span<const std::uint8_t> data) {
    while (!data.empty()) {
        if (header_used_ < header_.size()) {
            auto size = std::min(data.size(), header_.size() - header_used_);
            std::memcpy(header_.data() + header_used_, data.data(), size);
            header_used_ += size;
            data = data.subspan(size);

            if (header_used_ < 5) {
                continue;
            }

            if (header_[0] != 0) {
                invalid();
            }

            std::uint32_t length = 0;

            for (unsigned i = 1; i < 5; ++i) {
                length = (length << 8) | header_[i];
            }

            if (length > maximum_message) {
                invalid();
            }

            body_.resize(length);
            body_used_ = 0;

            if (length == 0) {
                header_used_ = 0;
                receiver_({});
                continue;
            }
        }

        auto size = std::min(data.size(), body_.size() - body_used_);
        std::memcpy(body_.data() + body_used_, data.data(), size);
        body_used_ += size;
        data = data.subspan(size);

        if (body_used_ == body_.size()) {
            header_used_ = 0;
            body_used_ = 0;
            receiver_(std::move(body_));
            body_.clear();
        }
    }
}
} // namespace voyemu::media
