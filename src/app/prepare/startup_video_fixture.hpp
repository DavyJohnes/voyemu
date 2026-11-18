#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

// Synthetic H.264 video in MP4, built by tests: every frame is an IDR picture of lossless I_PCM
// macroblocks in one solid YUV color, so decoded planes are exact.
namespace voyemu::startup_video_fixture {
class Bits {
public:
    void bit(unsigned value) {
        current_ = std::uint8_t(current_ << 1 | (value & 1));

        if (++used_ == 8) {
            bytes_ += char(current_);
            current_ = 0;
            used_ = 0;
        }
    }

    void bits(std::uint32_t value, unsigned count) {
        while (count-- > 0) {
            bit(value >> count);
        }
    }

    // Unsigned Exp-Golomb code.
    void ue(std::uint32_t value) {
        const auto coded = value + 1;
        unsigned length = 0;

        while (coded >> (length + 1)) {
            ++length;
        }

        bits(0, length);
        bits(coded, length + 1);
    }

    void align_zero() {
        while (used_) {
            bit(0);
        }
    }

    // RBSP stop bit and alignment.
    std::string trailing() {
        bit(1);
        align_zero();

        return bytes_;
    }

private:
    std::string bytes_;
    std::uint8_t current_{};
    unsigned used_{};
};

// Inserts emulation prevention bytes and the NAL header.
inline std::string nal(unsigned header, const std::string &rbsp) {
    std::string result(1, char(header));
    unsigned zeros = 0;

    for (const char c : rbsp) {
        const auto byte = static_cast<unsigned char>(c);

        if (zeros >= 2 && byte <= 3) {
            result += '\3';
            zeros = 0;
        }

        result += c;
        zeros = byte == 0 ? zeros + 1 : 0;
    }

    return result;
}

// The colour description of a sequence: matrix_coefficients and video_full_range_flag.
struct Colour {
    unsigned matrix{};
    bool full_range{};
};

// High profile at level 6.0, as the OEM videos declare, optionally with a colour description.
inline std::string sps(unsigned width, unsigned height, const std::optional<Colour> &colour = {}) {
    Bits b;

    b.bits(100, 8);
    b.bits(0, 8);
    b.bits(60, 8);
    b.ue(0);
    b.ue(1);
    b.ue(0);
    b.ue(0);
    b.bit(0);
    b.bit(0);
    b.ue(0);
    b.ue(2);
    b.ue(1);
    b.bit(0);
    b.ue(width / 16 - 1);
    b.ue(height / 16 - 1);
    b.bit(1);
    b.bit(1);
    b.bit(0);
    b.bit(bool(colour));

    if (colour) {
        // No aspect ratio or overscan; video signal type with primaries, transfer and matrix;
        // no chroma location, timing, HRD, picture structure or bitstream restriction.
        b.bits(0, 2);
        b.bit(1);
        b.bits(5, 3);
        b.bit(colour->full_range);
        b.bit(1);
        b.bits(colour->matrix == 1 ? 1 : 5, 8);
        b.bits(colour->matrix == 1 ? 1 : 6, 8);
        b.bits(colour->matrix, 8);
        b.bits(0, 6);
    }

    return nal(0x67, b.trailing());
}

inline std::string pps() {
    Bits b;

    b.ue(0);
    b.ue(0);
    b.bit(0);
    b.bit(0);
    b.ue(0);
    b.ue(0);
    b.ue(0);
    b.bit(0);
    b.bits(0, 2);
    b.ue(0);
    b.ue(0);
    b.ue(0);
    b.bit(1);
    b.bit(0);
    b.bit(0);

    return nal(0x68, b.trailing());
}

inline std::string idr(unsigned width, unsigned height, unsigned picture,
                       std::array<std::uint8_t, 3> yuv) {
    Bits b;

    b.ue(0);
    b.ue(7);
    b.ue(0);
    b.bits(0, 4);
    b.ue(picture % 2);
    b.bit(0);
    b.bit(0);
    b.ue(0);
    b.ue(1);

    for (unsigned mb = 0; mb < width / 16 * (height / 16); ++mb) {
        b.ue(25);
        b.align_zero();

        for (unsigned i = 0; i < 384; ++i) {
            b.bits(i < 256 ? yuv[0] : i < 320 ? yuv[1] : yuv[2], 8);
        }
    }

    return nal(0x65, b.trailing());
}

inline void word(std::string &bytes, std::uint64_t value, unsigned width) {
    while (width-- > 0) {
        bytes += char(value >> (width * 8));
    }
}

inline std::string box(const char *type, const std::string &body) {
    std::string result;

    word(result, body.size() + 8, 4);
    result += type;

    return result + body;
}

inline std::string full(const char *type, const std::string &body) {
    return box(type, std::string(4, '\0') + body);
}

// How a synthetic video differs from the plain one.
struct Options {
    std::optional<Colour> colour;
    // The first frame repeats the parameter sets in front of its picture, as the OEM videos do.
    bool parameter_sets_in_frame = false;
    // Further boxes of the sample table.
    std::string sample_table;
};

// One frame per chunk with 4-byte NAL lengths; the index follows the media data.
inline void mp4(const std::filesystem::path &file, unsigned width, unsigned height, unsigned fps,
                const std::vector<std::array<std::uint8_t, 3>> &colors,
                const Options &options = {}) {
    std::vector<std::string> samples;
    const auto parameter_sps = sps(width, height, options.colour), parameter_pps = pps();

    for (unsigned i = 0; i < colors.size(); ++i) {
        std::string sample;

        for (const auto &unit :
             i == 0 && options.parameter_sets_in_frame
                     ? std::vector{parameter_sps, parameter_pps, idr(width, height, i, colors[i])}
                     : std::vector{idr(width, height, i, colors[i])}) {
            word(sample, unit.size(), 4);
            sample += unit;
        }

        samples.push_back(sample);
    }

    const auto ftyp = box("ftyp", std::string("isom") + std::string("\0\0\2\0", 4) + "isommp41");
    std::string media;

    for (const auto &sample : samples) {
        media += sample;
    }

    const auto mdat = box("mdat", media);
    std::string avcc = std::string("\1", 1) + parameter_sps.substr(1, 3) + "\xff\xe1";
    word(avcc, parameter_sps.size(), 2);
    avcc += parameter_sps + "\1";
    word(avcc, parameter_pps.size(), 2);
    avcc += parameter_pps;
    std::string entry(6, '\0');
    word(entry, 1, 2);
    entry += std::string(16, '\0');
    word(entry, width, 2);
    word(entry, height, 2);
    word(entry, 0x00480000, 4);
    word(entry, 0x00480000, 4);
    word(entry, 0, 4);
    word(entry, 1, 2);
    entry += std::string(32, '\0');
    word(entry, 0x18, 2);
    word(entry, 0xffff, 2);
    std::string stsd, stts, stsc, stsz, stco;
    word(stsd, 1, 4);
    stsd += box("avc1", entry + box("avcC", avcc));
    word(stts, 1, 4);
    word(stts, samples.size(), 4);
    word(stts, 90000 / fps, 4);
    word(stsc, 1, 4);
    word(stsc, 1, 4);
    word(stsc, 1, 4);
    word(stsc, 1, 4);
    word(stsz, 0, 4);
    word(stsz, samples.size(), 4);
    word(stco, samples.size(), 4);
    auto offset = ftyp.size() + 8;

    for (const auto &sample : samples) {
        word(stsz, sample.size(), 4);
        word(stco, offset, 4);
        offset += sample.size();
    }

    std::string mdhd(8, '\0');
    word(mdhd, 90000, 4);
    word(mdhd, samples.size() * (90000 / fps), 4);
    word(mdhd, 0, 4);
    const auto stbl = box("stbl", full("stsd", stsd) + full("stts", stts) + full("stsc", stsc) +
                                          full("stsz", stsz) + full("stco", stco) +
                                          options.sample_table);
    const auto mdia = box(
            "mdia", full("mdhd", mdhd) +
                            full("hdlr", std::string(4, '\0') + "vide" + std::string(13, '\0')) +
                            box("minf", stbl));
    std::ofstream(file, std::ios::binary) << ftyp << mdat << box("moov", box("trak", mdia));
}
} // namespace voyemu::startup_video_fixture
