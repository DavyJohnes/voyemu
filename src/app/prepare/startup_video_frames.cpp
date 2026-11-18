#include "startup_video_frames.hpp"
#include "check.hpp"
#include "error.hpp"
#include "logging.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <list>
#include <miniz.h>
#include <stdexcept>

namespace voyemu {
namespace {
constexpr std::uint64_t maximum_video = 64ULL << 20, maximum_index = 4ULL << 20,
                        maximum_sample = 16ULL << 20;
constexpr std::size_t maximum_frames = 18000;
// The OEM players fill a display from the full frame; larger videos are not OEM startup videos.
constexpr unsigned maximum_width = 3840, maximum_height = 2160;

constexpr Check need{"STARTUP_VIDEO"};

std::uint64_t big(std::string_view bytes, std::size_t at, unsigned width) {
    need(at <= bytes.size() && width <= bytes.size() - at, "truncated MP4 box");
    std::uint64_t value = 0;

    for (unsigned i = 0; i < width; ++i) {
        value = (value << 8) | static_cast<unsigned char>(bytes[at + i]);
    }

    return value;
}

// The boxes directly inside one box body, by type.
struct Box {
    std::string type;
    std::string_view body;
};

std::vector<Box> children(std::string_view body) {
    std::vector<Box> result;

    for (std::size_t at = 0; at < body.size();) {
        auto size = big(body, at, 4);
        std::size_t header = 8;

        if (size == 1) {
            size = big(body, at + 8, 8);
            header = 16;
        } else if (size == 0) {
            size = body.size() - at;
        }

        need(size >= header && size <= body.size() - at && result.size() < 4096,
             "MP4 box exceeds its parent");
        result.push_back({std::string(body.substr(at + 4, 4)),
                          body.substr(at + header, std::size_t(size) - header)});
        at += std::size_t(size);
    }

    return result;
}

std::string_view child(std::string_view body, std::string_view type) {
    std::string_view found;
    bool present = false;

    for (const auto &box : children(body)) {
        if (box.type == type) {
            need(!present, "duplicate MP4 box");
            found = box.body;
            present = true;
        }
    }

    need(present, "required MP4 box missing");

    return found;
}

bool has_child(std::string_view body, std::string_view type) {
    for (const auto &box : children(body)) {
        if (box.type == type) {
            return true;
        }
    }

    return false;
}

// Exp-Golomb reader of an RBSP: the NAL unit body without its emulation prevention bytes.
class Bits {
public:
    explicit Bits(std::string_view nal) {
        unsigned zeros = 0;

        for (const char c : nal.substr(1)) {
            const auto byte = static_cast<unsigned char>(c);

            if (zeros >= 2 && byte == 3) {
                zeros = 0;
                continue;
            }

            rbsp_ += c;
            zeros = byte == 0 ? zeros + 1 : 0;
        }
    }

    unsigned u(unsigned count) {
        unsigned value = 0;

        while (count-- > 0) {
            need(at_ < rbsp_.size() * 8, "truncated sequence parameter set");
            value = value << 1 | (static_cast<unsigned char>(rbsp_[at_ / 8]) >> (7 - at_ % 8) & 1U);
            ++at_;
        }

        return value;
    }

    unsigned ue() {
        unsigned zeros = 0;

        while (u(1) == 0) {
            need(++zeros < 32, "invalid sequence parameter set");
        }

        return (1U << zeros) - 1 + u(zeros);
    }

    int se() {
        const auto code = ue();

        return code % 2 ? int(code / 2 + 1) : -int(code / 2);
    }

private:
    std::string rbsp_;
    std::size_t at_{};
};
// The fields of a sequence parameter set that decide the level and the colour conversion.
struct Sequence {
    unsigned macroblocks{}, reference_frames{};
    bool bt709 = true, full_range = false;
};

Sequence sequence(std::string_view nal) {
    Bits bits(nal);
    Sequence result;
    const auto profile = bits.u(8);

    bits.u(16);
    bits.ue();

    if (profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 ||
        profile == 83 || profile == 86 || profile == 118 || profile == 128 || profile == 138 ||
        profile == 139 || profile == 134 || profile == 135) {
        const auto chroma = bits.ue();
        need(chroma == 1 && bits.ue() == 0 && bits.ue() == 0, "8-bit 4:2:0 H.264 required");
        bits.u(1);

        if (bits.u(1)) {
            for (unsigned list = 0; list < 8; ++list) {
                if (!bits.u(1)) {
                    continue;
                }

                for (int j = 0, last = 8, next = 8; j < (list < 6 ? 16 : 64); ++j) {
                    if (next != 0) {
                        next = (last + bits.se() + 256) % 256;
                    }

                    last = next == 0 ? last : next;
                }
            }
        }
    }

    bits.ue();
    const auto order = bits.ue();

    if (order == 0) {
        bits.ue();
    } else if (order == 1) {
        bits.u(1);
        bits.se();
        bits.se();

        for (auto cycle = bits.ue(); cycle > 0; --cycle) {
            bits.se();
        }
    }

    result.reference_frames = bits.ue();
    bits.u(1);
    const auto width = bits.ue() + 1, height = bits.ue() + 1;
    result.macroblocks = width * height * (bits.u(1) ? 1 : 2);
    bits.u(1);

    if (bits.u(1)) {
        bits.ue();
        bits.ue();
        bits.ue();
        bits.ue();
    }

    if (bits.u(1)) {
        if (bits.u(1) && bits.u(8) == 255) {
            bits.u(32);
        }

        if (bits.u(1)) {
            bits.u(1);
        }

        if (bits.u(1)) {
            bits.u(3);
            result.full_range = bits.u(1) != 0;

            if (bits.u(1)) {
                bits.u(16);
                const auto matrix = bits.u(8);
                // BT.470 System B/G and SMPTE 170M are the BT.601 matrix.
                result.bt709 = matrix != 5 && matrix != 6;
            }
        }
    }

    return result;
}

// Lowers level_idc of a sequence parameter set as startup_video_level allows. level_idc is the
// fourth byte; no emulation prevention byte precedes it because profile_idc is never zero.
Sequence lower_level(std::string &sps, double fps) {
    need(sps.size() >= 4 && (sps[0] & 0x1f) == 7, "sequence parameter set required");
    const auto described = sequence(sps);

    sps[3] = char(startup_video_level(static_cast<unsigned char>(sps[3]), described.macroblocks,
                                      fps, described.reference_frames));

    return described;
}

// Converts one decoded 4:2:0 picture to packed RGB rows at the video size.
void convert(const os::VideoPicture &picture, const StartupVideo &video,
             std::vector<std::uint8_t> &rgb) {
    need(picture.width >= video.width && picture.height >= video.height && picture.luma &&
                 picture.u && picture.v && picture.luma_stride >= video.width &&
                 (picture.chroma_step == 1 || picture.chroma_step == 2) &&
                 picture.chroma_stride >= std::size_t(video.width / 2) * picture.chroma_step,
         "decoded picture differs from the video description");
    // Fixed-point (x256) luma scale and offset, then the V to red, U and V to green and U to blue
    // weights of BT.601 or BT.709 at studio or full range.
    struct Matrix {
        int luma, offset, red, green_u, green_v, blue;
    };
    constexpr std::array<Matrix, 4> matrices{{{298, 16, 409, 100, 208, 516},
                                              {298, 16, 459, 55, 136, 541},
                                              {256, 0, 359, 88, 183, 454},
                                              {256, 0, 403, 48, 120, 475}}};
    const auto &m = matrices[(video.full_range ? 2 : 0) + (video.bt709 ? 1 : 0)];
    const auto clamp = [](int value) {
        return std::uint8_t(std::clamp(value, 0, 255));
    };
    const auto step = picture.chroma_step;

    rgb.resize(std::size_t(video.width) * video.height * 3);
    auto *to = rgb.data();

    for (unsigned y = 0; y < video.height; ++y) {
        const auto *luma = picture.luma + std::size_t(y) * picture.luma_stride;
        const auto *u = picture.u + std::size_t(y / 2) * picture.chroma_stride;
        const auto *v = picture.v + std::size_t(y / 2) * picture.chroma_stride;

        for (unsigned x = 0; x < video.width; ++x) {
            const int c = m.luma * (luma[x] - m.offset), d = u[std::size_t(x / 2) * step] - 128,
                      e = v[std::size_t(x / 2) * step] - 128;
            *to++ = clamp((c + m.red * e + 128) >> 8);
            *to++ = clamp((c - m.green_u * d - m.green_v * e + 128) >> 8);
            *to++ = clamp((c + m.blue * d + 128) >> 8);
        }
    }
}

// A ZIP archive of stored entries written to a file. Every entry carries the DOS time zero
// (1980-01-01 00:00), so that the same frames give the same bytes on every host.
class StoredZip {
public:
    explicit StoredZip(const std::filesystem::path &file)
        : output_(file, std::ios::binary | std::ios::trunc) {
        need(bool(output_), "cannot write the startup animation");
    }

    void add(const std::string &name, const void *data, std::size_t bytes) {
        need(bytes <= 0xffffffffU && offset_ + 30 + name.size() + bytes <= 0xffffffffU &&
                     entries_ < 0xffff,
             "startup animation exceeds a ZIP without ZIP64");
        const auto crc = std::uint32_t(
                mz_crc32(MZ_CRC32_INIT, static_cast<const unsigned char *>(data), bytes));
        std::string local;
        word(local, 0x04034b50, 4);
        fields(local, crc, bytes, name.size());
        word(local, 0, 2);
        local += name;
        output_.write(local.data(), static_cast<std::streamsize>(local.size()));
        output_.write(static_cast<const char *>(data), static_cast<std::streamsize>(bytes));
        need(bool(output_), "cannot add a startup animation entry");
        word(directory_, 0x02014b50, 4);
        word(directory_, 20, 2);
        fields(directory_, crc, bytes, name.size());
        // No extra field or comment, disk 0, no attributes.
        word(directory_, 0, 2);
        word(directory_, 0, 2);
        word(directory_, 0, 2);
        word(directory_, 0, 2);
        word(directory_, 0, 4);
        word(directory_, offset_, 4);
        directory_ += name;
        offset_ += local.size() + bytes;
        ++entries_;
    }

    void finish() {
        std::string end;

        word(end, 0x06054b50, 4);
        word(end, 0, 4);
        word(end, entries_, 2);
        word(end, entries_, 2);
        word(end, directory_.size(), 4);
        word(end, offset_, 4);
        word(end, 0, 2);
        output_.write(directory_.data(), static_cast<std::streamsize>(directory_.size()));
        output_.write(end.data(), static_cast<std::streamsize>(end.size()));
        need(directory_.size() <= 0xffffffffU - offset_ && bool(output_.flush()),
             "cannot finish the startup animation");
    }

private:
    static void word(std::string &bytes, std::uint64_t value, unsigned width) {
        for (unsigned i = 0; i < width; ++i) {
            bytes += char(value >> (8 * i));
        }
    }

    // Version, flags, stored method, DOS time and date, CRC, sizes and name length.
    static void fields(std::string &bytes, std::uint32_t crc, std::size_t size, std::size_t name) {
        word(bytes, 20, 2);
        word(bytes, 0, 2);
        word(bytes, 0, 2);
        word(bytes, 0, 2);
        word(bytes, 0x21, 2);
        word(bytes, crc, 4);
        word(bytes, size, 4);
        word(bytes, size, 4);
        word(bytes, name, 2);
    }

    std::ofstream output_;
    std::string directory_;
    std::uint64_t offset_{};
    unsigned entries_{};
};

void add_png(StoredZip &zip, const std::string &name, const std::vector<std::uint8_t> &rgb,
             unsigned width, unsigned height) {
    std::size_t bytes = 0;
    void *png = tdefl_write_image_to_png_file_in_memory_ex(rgb.data(), int(width), int(height), 3,
                                                           &bytes, MZ_BEST_SPEED, MZ_FALSE);

    need(png != nullptr, "cannot encode a startup frame");

    try {
        zip.add(name, png, bytes);
    } catch (...) {
        mz_free(png);
        throw;
    }

    mz_free(png);
}

std::string frame_name(std::size_t index) {
    char name[32];

    std::snprintf(name, sizeof name, "part0/%05zu.png", index);

    return name;
}
} // namespace

unsigned startup_video_level(unsigned level, unsigned macroblocks, double fps,
                             unsigned reference_frames) {
    // Level, MaxMBPS, MaxFS and MaxDpbMbs of H.264 Table A-1.
    constexpr std::array<std::array<double, 4>, 2> levels{
            {{51, 983040, 36864, 184320}, {52, 2073600, 36864, 184320}}};

    for (const auto &[lower, rate, frame, buffer] : levels) {
        if (level > lower && macroblocks <= frame && macroblocks * fps <= rate &&
            double(macroblocks) * reference_frames <= buffer) {
            return unsigned(lower);
        }
    }

    return level;
}

StartupVideo read_startup_video(std::string_view mp4) {
    StartupVideo video;
    const auto size = mp4.size();

    need(size >= 16 && size <= maximum_video, "bounded MP4 required");
    std::string_view moov;
    bool indexed = false;

    for (const auto &box : children(mp4)) {
        if (box.type == "moov") {
            need(!indexed && box.body.size() <= maximum_index, "one bounded MP4 index required");
            moov = box.body;
            indexed = true;
        }
    }

    need(indexed, "MP4 index missing");
    std::string_view stbl;
    bool tracked = false;
    std::uint64_t timescale = 0;

    for (const auto &box : children(moov)) {
        if (box.type != "trak") {
            continue;
        }

        const auto mdia = child(box.body, "mdia");
        const auto handler = child(mdia, "hdlr");

        if (big(handler, 8, 4) != 0x76696465) {
            continue;
        }

        need(!tracked, "one video track required");
        const auto mdhd = child(mdia, "mdhd");
        timescale = big(mdhd, 0, 1) == 1 ? big(mdhd, 20, 4) : big(mdhd, 12, 4);
        stbl = child(child(mdia, "minf"), "stbl");
        tracked = true;
    }

    need(tracked && timescale > 0, "video track required");
    const auto stsd = child(stbl, "stsd");
    need(big(stsd, 4, 4) == 1, "one video sample description required");
    const auto entries = children(stsd.substr(8));
    need(entries.size() == 1 && entries[0].type == "avc1", "H.264 avc1 video required");
    const auto entry = entries[0].body;
    video.width = unsigned(big(entry, 24, 2));
    video.height = unsigned(big(entry, 26, 2));
    need(video.width >= 16 && video.height >= 16 && video.width <= maximum_width &&
                 video.height <= maximum_height && video.width % 2 == 0 && video.height % 2 == 0,
         "even video size up to 3840x2160 required");
    need(entry.size() >= 78, "truncated avc1 description");
    const auto avcc = child(entry.substr(78), "avcC");
    need(big(avcc, 0, 1) == 1, "avcC version 1 required");
    video.length_bytes = unsigned(big(avcc, 4, 1) & 3) + 1;
    need(video.length_bytes != 3, "NAL length of 1, 2 or 4 bytes required");
    std::size_t at = 5;

    for (const unsigned mask : {0x1fU, 0xffU}) {
        const auto count = big(avcc, at, 1) & mask;
        ++at;

        for (unsigned i = 0; i < count; ++i) {
            const auto bytes = big(avcc, at, 2);
            need(bytes > 0 && at + 2 + bytes <= avcc.size(), "truncated parameter set");
            video.parameter_sets.emplace_back(avcc.substr(at + 2, bytes));
            at += 2 + bytes;
        }

        need(count > 0, "sequence and picture parameter sets required");
    }

    const auto stts = child(stbl, "stts");
    const auto timings = big(stts, 4, 4);
    std::uint64_t timed = 0, delta = 0;
    need(timings > 0 && timings <= maximum_frames, "bounded sample timing required");

    for (std::uint64_t i = 0; i < timings; ++i) {
        const auto count = big(stts, 8 + i * 8, 4), duration = big(stts, 12 + i * 8, 4);
        need(duration > 0 && (delta == 0 || duration == delta), "constant frame rate required");
        delta = duration;
        timed += count;
    }

    video.fps = double(timescale) / double(delta);
    need(std::isfinite(video.fps) && video.fps >= 1 && video.fps <= 60,
         "1..60 frames per second required");
    // Frames are written in decoding order, so it must be the presentation order.
    need(!has_child(stbl, "ctts"), "frames without composition offsets required");
    const auto described = lower_level(video.parameter_sets.front(), video.fps);
    video.bt709 = described.bt709;
    video.full_range = described.full_range;
    const auto stsz = child(stbl, "stsz");
    const auto constant = big(stsz, 4, 4), count = big(stsz, 8, 4);
    need(count > 0 && count <= maximum_frames && count == timed, "bounded timed frames required");
    std::vector<std::uint32_t> sizes;

    for (std::uint64_t i = 0; i < count; ++i) {
        sizes.push_back(std::uint32_t(constant ? constant : big(stsz, 12 + i * 4, 4)));
    }

    const bool wide = has_child(stbl, "co64");
    const auto stco = child(stbl, wide ? "co64" : "stco");
    const auto chunks = big(stco, 4, 4);
    const auto stsc = child(stbl, "stsc");
    const auto runs = big(stsc, 4, 4);
    need(chunks > 0 && chunks <= maximum_frames && runs > 0 && runs <= chunks,
         "bounded chunk tables required");

    for (std::uint64_t run = 0, sample = 0; run < runs; ++run) {
        const auto first = big(stsc, 8 + run * 12, 4), per = big(stsc, 12 + run * 12, 4);
        const auto last = run + 1 < runs ? big(stsc, 20 + run * 12, 4) - 1 : chunks;
        need(first >= 1 && first <= last && last <= chunks && per > 0 && per <= count,
             "chunk table out of order");

        for (auto chunk = first; chunk <= last; ++chunk) {
            auto offset = big(stco, 8 + (chunk - 1) * (wide ? 8 : 4), wide ? 8 : 4);

            for (std::uint64_t i = 0; i < per; ++i, ++sample) {
                need(sample < count, "chunks hold more frames than timed");
                const auto bytes = sizes[sample];
                need(bytes > 0 && bytes <= maximum_sample && offset <= size &&
                             bytes <= size - offset,
                     "frame outside the MP4");
                video.frames.push_back({offset, bytes});
                offset += bytes;
            }
        }
    }

    need(video.frames.size() == count, "chunks hold fewer frames than timed");

    return video;
}

bool write_startup_video_animation(std::string_view mp4, double seconds,
                                   const std::filesystem::path &output,
                                   const StartupVideoDecoder &decoder) {
    const auto video = read_startup_video(mp4);
    const auto fps = unsigned(std::lround(video.fps));

    need(std::isfinite(seconds) && seconds > 0, "positive player limit required");
    const auto frames = std::min<std::size_t>(
            video.frames.size(), std::max<std::size_t>(1, std::size_t(seconds * fps + 1e-6)));
    const auto header = std::to_string(video.width) + " " + std::to_string(video.height) + " " +
                        std::to_string(fps) + "\n";

    try {
        StoredZip zip(output);
        const auto host =
                decoder({video.width, video.height, video.full_range, video.parameter_sets});
        need(host != nullptr, "no host decoder");
        std::vector<std::uint8_t> rgb;
        std::size_t decoded = 0;
        const os::VideoPictureSink sink = [&](const os::VideoPicture &picture) {
            if (decoded < frames) {
                convert(picture, video, rgb);
                add_png(zip, frame_name(decoded++), rgb, video.width, video.height);
            }
        };

        for (std::size_t sample = 0; sample < video.frames.size() && decoded < frames; ++sample) {
            const auto bytes = mp4.substr(video.frames[sample].offset, video.frames[sample].bytes);
            std::vector<std::string_view> units;
            // Sequence parameter sets inside the frames get the same lowered level.
            std::list<std::string> lowered;

            for (std::size_t at = 0; at < bytes.size();) {
                const auto length = big(bytes, at, video.length_bytes);
                at += video.length_bytes;
                need(length > 0 && length <= bytes.size() - at, "NAL unit outside its frame");
                units.push_back(bytes.substr(at, length));

                if ((units.back()[0] & 0x1f) == 7) {
                    lower_level(lowered.emplace_back(units.back()), video.fps);
                    units.back() = lowered.back();
                }

                at += length;
            }

            host->decode(units, sink);
        }

        if (decoded < frames) {
            host->flush(sink);
        }

        need(decoded == frames, "video ended before its declared frames");
        const auto desc = header + "p 1 0 part0\n";
        zip.add("desc.txt", desc.data(), desc.size());
        zip.finish();

        return true;
    } catch (const std::exception &error) {
        logging::write(logging::Level::info, "prepare", "startup_video.undecoded",
                       {{"reason", error.what()}});
    }

    StoredZip zip(output);
    add_png(zip, frame_name(0),
            std::vector<std::uint8_t>(std::size_t(video.width) * video.height * 3), video.width,
            video.height);
    const auto desc = header + "p 1 " + std::to_string(frames - 1) + " part0\n";
    zip.add("desc.txt", desc.data(), desc.size());
    zip.finish();

    return false;
}
} // namespace voyemu
