// A stand-in for the system libavcodec with the functions, enum values and leading AVFrame fields
// that the Linux decoder backend declares. It decodes only the synthetic videos of
// startup_video_fixture.hpp: every slice is I_PCM in one color, so its last macroblock's samples
// give the picture. It holds one picture back until the next packet or the drain and refuses a
// third pending picture, as a frame-threaded decoder can, and accepts a sequence parameter set
// only at level 5.1.
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Frame {
    std::uint8_t *data[8];
    int linesize[8];
    std::uint8_t **extended_data;
    int width, height, nb_samples, format;
    std::vector<std::uint8_t> planes;
};
struct Packet {
    void *buf;
    std::int64_t pts, dts;
    std::uint8_t *data;
    int size;
};
struct Context {
    unsigned width = 0, height = 0;
    bool opened = false, draining = false;
    std::deque<std::array<std::uint8_t, 3>> pending;
};
constexpr int codec_h264 = 27;
constexpr int error_again = -EAGAIN, error_invalid = -0x41444e49,
              error_eof = -int(unsigned('E') | unsigned('O') << 8 | unsigned('F') << 16 |
                               unsigned(' ') << 24);
const char decoder_token = 0;

std::string rbsp(std::string_view nal) {
    std::string result;
    unsigned zeros = 0;

    for (const char c : nal.substr(1)) {
        const auto byte = static_cast<unsigned char>(c);

        if (zeros >= 2 && byte == 3) {
            zeros = 0;
            continue;
        }

        result += c;
        zeros = byte == 0 ? zeros + 1 : 0;
    }

    return result;
}

class Bits {
public:
    explicit Bits(std::string bytes) : bytes_(std::move(bytes)) {}

    unsigned u(unsigned count) {
        unsigned value = 0;

        while (count-- > 0 && at_ < bytes_.size() * 8) {
            value = value << 1 |
                    (static_cast<unsigned char>(bytes_[at_ / 8]) >> (7 - at_ % 8) & 1U);
            ++at_;
        }

        return value;
    }

    unsigned ue() {
        unsigned zeros = 0;

        while (at_ < bytes_.size() * 8 && u(1) == 0) {
            ++zeros;
        }

        return (1U << zeros) - 1 + u(zeros);
    }

private:
    std::string bytes_;
    std::size_t at_{};
};

// The frame size of the fixture's sequence parameter set, whose fields before the size are fixed.
bool sequence(std::string_view nal, Context &context) {
    Bits bits(rbsp(nal));

    const auto profile = bits.u(8);

    bits.u(8);

    if (profile != 100 || bits.u(8) != 51) {
        return false;
    }

    for (unsigned field = 0; field < 4; ++field) {
        bits.ue();
    }

    bits.u(2);
    bits.ue();

    if (bits.ue() != 2) {
        return false;
    }

    bits.ue();
    bits.u(1);
    context.width = (bits.ue() + 1) * 16;
    context.height = (bits.ue() + 1) * 16;

    return true;
}
} // namespace
extern "C" {
const void *avcodec_find_decoder(int id) {
    return id == codec_h264 ? &decoder_token : nullptr;
}

void *avcodec_alloc_context3(const void *codec) {
    return codec == &decoder_token ? new Context : nullptr;
}

int avcodec_open2(void *context, const void *codec, void **options) {
    const auto *values = options ? static_cast<std::map<std::string, std::string> *>(*options)
                                 : nullptr;

    if (codec != &decoder_token || !values || values->at("threads") != "auto") {
        return error_invalid;
    }

    static_cast<Context *>(context)->opened = true;

    return 0;
}

void avcodec_free_context(void **context) {
    delete static_cast<Context *>(*context);
    *context = nullptr;
}

int avcodec_send_packet(void *opaque, const void *packet_opaque) {
    auto &context = *static_cast<Context *>(opaque);

    if (!context.opened) {
        return error_invalid;
    }

    if (!packet_opaque) {
        if (context.draining) {
            return error_eof;
        }

        context.draining = true;

        return 0;
    }

    if (context.pending.size() >= 2) {
        return error_again;
    }

    const auto &packet = *static_cast<const Packet *>(packet_opaque);
    const std::string_view stream(reinterpret_cast<const char *>(packet.data),
                                  std::size_t(packet.size));
    const std::string_view start("\0\0\0\1", 4);

    if (!stream.starts_with(start)) {
        return error_invalid;
    }

    for (std::size_t at = start.size(); at < stream.size();) {
        const auto next = std::min(stream.find(start, at), stream.size());
        const auto nal = stream.substr(at, next - at);
        const auto type = nal.empty() ? 0 : nal[0] & 0x1f;

        if (type == 7 && !sequence(nal, context)) {
            return error_invalid;
        }

        if (type == 5 || type == 1) {
            const auto body = rbsp(nal);

            if (context.width == 0 || body.size() <= 385) {
                return error_invalid;
            }

            const auto last = body.size() - 385;
            context.pending.push_back({std::uint8_t(body[last]), std::uint8_t(body[last + 256]),
                                       std::uint8_t(body[last + 320])});
        }

        at = next + start.size();
    }

    return 0;
}

int avcodec_receive_frame(void *opaque, void *frame_opaque) {
    auto &context = *static_cast<Context *>(opaque);
    auto &frame = *static_cast<Frame *>(frame_opaque);

    if (context.pending.size() < (context.draining ? 1U : 2U)) {
        return context.draining ? error_eof : error_again;
    }

    const auto [y, u, v] = context.pending.front();
    const unsigned stride = context.width + 32, chroma = stride / 2;

    context.pending.pop_front();
    frame.planes.assign(std::size_t(stride) * context.height * 3 / 2, 0);
    std::memset(frame.planes.data(), y, std::size_t(stride) * context.height);
    std::memset(frame.planes.data() + std::size_t(stride) * context.height, u,
                std::size_t(chroma) * context.height / 2);
    std::memset(frame.planes.data() + std::size_t(stride) * context.height +
                        std::size_t(chroma) * context.height / 2,
                v, std::size_t(chroma) * context.height / 2);
    frame.data[0] = frame.planes.data();
    frame.data[1] = frame.data[0] + std::size_t(stride) * context.height;
    frame.data[2] = frame.data[1] + std::size_t(chroma) * context.height / 2;
    frame.linesize[0] = int(stride);
    frame.linesize[1] = frame.linesize[2] = int(chroma);
    frame.width = int(context.width);
    frame.height = int(context.height);
    frame.format = 0;

    return 0;
}

void *av_packet_alloc() {
    return new Packet{};
}

int av_packet_from_data(void *packet, std::uint8_t *data, int size) {
    static_cast<Packet *>(packet)->data = data;
    static_cast<Packet *>(packet)->size = size;

    return 0;
}

void av_packet_unref(void *packet) {
    std::free(static_cast<Packet *>(packet)->data);
    *static_cast<Packet *>(packet) = {};
}

void av_packet_free(void **packet) {
    if (*packet) {
        av_packet_unref(*packet);
    }

    delete static_cast<Packet *>(*packet);
    *packet = nullptr;
}

void *av_frame_alloc() {
    return new Frame{};
}

void av_frame_unref(void *frame) {
    auto &value = *static_cast<Frame *>(frame);

    value.planes.clear();
    std::fill(std::begin(value.data), std::end(value.data), nullptr);
}

void av_frame_free(void **frame) {
    delete static_cast<Frame *>(*frame);
    *frame = nullptr;
}

void *av_malloc(std::size_t size) {
    return std::malloc(size);
}

void av_free(void *data) {
    std::free(data);
}

int av_dict_set(void **dictionary, const char *key, const char *value, int) {
    if (!*dictionary) {
        *dictionary = new std::map<std::string, std::string>;
    }

    (*static_cast<std::map<std::string, std::string> *>(*dictionary))[key] = value;

    return 0;
}

void av_dict_free(void **dictionary) {
    delete static_cast<std::map<std::string, std::string> *>(*dictionary);
    *dictionary = nullptr;
}

void av_log_set_level(int) {}
}
