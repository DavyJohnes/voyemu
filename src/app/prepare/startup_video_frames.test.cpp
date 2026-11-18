#include "startup_video_frames.hpp"
#include "portable_resources.hpp"
#include "process.hpp"
#include "startup_video_fixture.hpp"
#include <chrono>
#include <cmath>
#include <deque>
#include <iostream>
#include <miniz.h>

using namespace voyemu;
namespace {
void need(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <class Function> void rejects(Function function, const char *message) {
    bool failed = false;

    try {
        function();
    } catch (const std::exception &error) {
        failed = std::string_view(error.what()).starts_with("STARTUP_VIDEO: ");
    }

    need(failed, message);
}

std::string bytes(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);

    return {std::istreambuf_iterator<char>(input), {}};
}

// Replaces the only occurrence of a byte pattern.
std::string replaced(std::string data, std::string_view before, std::string_view after) {
    const auto at = data.find(before);

    need(at != std::string::npos && data.find(before, at + 1) == std::string::npos,
         "fixture pattern must be unique");

    return data.replace(at, before.size(), after);
}

// The entries of a written animation archive, each required to be stored uncompressed.
std::map<std::string, std::string> stored_entries(const std::filesystem::path &file) {
    const auto archive = bytes(file);
    mz_zip_archive zip{};

    need(mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0), "unreadable archive");
    std::map<std::string, std::string> entries;

    for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&zip); ++i) {
        mz_zip_archive_file_stat stat{};
        need(mz_zip_reader_file_stat(&zip, i, &stat) && stat.m_method == 0 &&
                     stat.m_comp_size == stat.m_uncomp_size,
             "animation entries must be stored");
        std::size_t size = 0;
        void *data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
        need(data != nullptr, "unreadable archive entry");
        entries[stat.m_filename] = std::string(static_cast<const char *>(data), size);
        mz_free(data);
    }

    mz_zip_reader_end(&zip);

    return entries;
}

std::uint32_t big32(const std::string &data, std::size_t at) {
    need(at + 4 <= data.size(), "truncated PNG");

    return std::uint32_t(static_cast<unsigned char>(data[at])) << 24 |
           std::uint32_t(static_cast<unsigned char>(data[at + 1])) << 16 |
           std::uint32_t(static_cast<unsigned char>(data[at + 2])) << 8 |
           static_cast<unsigned char>(data[at + 3]);
}

// The RGB pixels of an 8-bit truecolor PNG whose rows use no filter.
std::vector<std::uint8_t> png_rgb(const std::string &png, unsigned width, unsigned height) {
    need(png.substr(0, 8) == "\x89PNG\r\n\x1a\n", "PNG signature expected");
    std::string compressed;

    for (std::size_t at = 8; at + 8 <= png.size();) {
        const auto length = big32(png, at);
        const auto type = png.substr(at + 4, 4);
        need(at + 12 + length <= png.size(), "truncated PNG chunk");

        if (type == "IHDR") {
            need(big32(png, at + 8) == width && big32(png, at + 12) == height &&
                         png[at + 16] == 8 && png[at + 17] == 2,
                 "frame size or pixel format differs");
        } else if (type == "IDAT") {
            compressed += png.substr(at + 8, length);
        }

        at += 12 + length;
    }

    std::vector<std::uint8_t> rows(std::size_t(width * 3 + 1) * height);
    mz_ulong size = mz_ulong(rows.size());
    need(mz_uncompress(rows.data(), &size,
                       reinterpret_cast<const unsigned char *>(compressed.data()),
                       mz_ulong(compressed.size())) == MZ_OK &&
                 size == rows.size(),
         "PNG pixels do not inflate");
    std::vector<std::uint8_t> rgb;

    for (unsigned y = 0; y < height; ++y) {
        const auto *row = rows.data() + std::size_t(y) * (width * 3 + 1);
        need(row[0] == 0, "unfiltered rows expected");
        rgb.insert(rgb.end(), row + 1, row + 1 + std::size_t(width) * 3);
    }

    return rgb;
}

// BT.709 or BT.601 conversion of one solid color at studio or full range.
std::array<double, 3> expected_rgb(std::array<std::uint8_t, 3> yuv, bool bt709 = true,
                                   bool full_range = false) {
    const double kr = bt709 ? .2126 : .299, kb = bt709 ? .0722 : .114, kg = 1 - kr - kb;
    const double y = full_range ? yuv[0] : (yuv[0] - 16) * 255. / 219,
                 scale = full_range ? 1 : 255. / 224, u = (yuv[1] - 128.) * scale,
                 v = (yuv[2] - 128.) * scale;

    return {y + 2 * (1 - kr) * v, y - 2 * kb * (1 - kb) / kg * u - 2 * kr * (1 - kr) / kg * v,
            y + 2 * (1 - kb) * u};
}

bool solid(const std::vector<std::uint8_t> &rgb, std::array<std::uint8_t, 3> yuv, bool bt709 = true,
           bool full_range = false) {
    const auto expected = expected_rgb(yuv, bt709, full_range);

    for (std::size_t i = 0; i < rgb.size(); ++i) {
        if (std::abs(double(rgb[i]) - std::clamp(expected[i % 3], 0., 255.)) > 2) {
            return false;
        }
    }

    return true;
}

constexpr std::array<std::array<std::uint8_t, 3>, 5> colors{
        {{200, 40, 90}, {16, 128, 128}, {235, 100, 200}, {90, 1, 255}, {120, 0, 3}}};

// The color of a synthetic picture: its last macroblock's I_PCM samples end the slice right before
// the RBSP trailing byte.
std::array<std::uint8_t, 3> pcm_color(std::string_view nal) {
    std::string rbsp;
    unsigned zeros = 0;

    for (const char c : nal.substr(1)) {
        const auto byte = static_cast<unsigned char>(c);

        if (zeros >= 2 && byte == 3) {
            zeros = 0;
            continue;
        }

        rbsp += c;
        zeros = byte == 0 ? zeros + 1 : 0;
    }

    need(rbsp.size() > 385, "synthetic slice expected");
    const auto at = rbsp.size() - 385;

    return {std::uint8_t(rbsp[at]), std::uint8_t(rbsp[at + 256]), std::uint8_t(rbsp[at + 320])};
}

// A decoder of the synthetic videos in place of the host's: planar or interleaved chroma, the
// exact or a larger coded size, pictures at once or one access unit later, and failures.
struct FakeDecoder final : os::VideoDecoder {
    struct Behavior {
        bool interleaved = false, padded = false, delayed = false, lose_last = false;
        std::size_t fail_at = std::size_t(-1);
    };

    FakeDecoder(const os::VideoStream &stream, Behavior behavior, std::vector<unsigned> &levels,
                std::size_t &units)
        : stream_(stream), behavior_(behavior), levels_(levels), units_(units) {
        for (const auto &set : stream.parameter_sets) {
            record(set);
        }
    }

    void decode(const std::vector<std::string_view> &units,
                const os::VideoPictureSink &sink) override {
        if (++units_ == behavior_.fail_at) {
            throw std::runtime_error("STARTUP_VIDEO: fake decoder rejects the stream");
        }

        for (const auto unit : units) {
            record(unit);

            if ((unit[0] & 0x1f) == 5 || (unit[0] & 0x1f) == 1) {
                pending_.push_back(pcm_color(unit));
            }
        }

        while (pending_.size() > (behavior_.delayed ? 1U : 0U)) {
            emit(sink);
        }
    }

    void flush(const os::VideoPictureSink &sink) override {
        if (behavior_.lose_last && !pending_.empty()) {
            pending_.pop_back();
        }

        while (!pending_.empty()) {
            emit(sink);
        }
    }

private:
    void record(std::string_view unit) {
        if ((unit[0] & 0x1f) == 7) {
            levels_.push_back(static_cast<unsigned char>(unit[3]));
        }
    }

    void emit(const os::VideoPictureSink &sink) {
        const auto [y, u, v] = pending_.front();
        const unsigned width = stream_.width + (behavior_.padded ? 16 : 0),
                       height = stream_.height + (behavior_.padded ? 16 : 0);
        const std::size_t stride = width + (behavior_.padded ? 32 : 0);
        std::vector<std::uint8_t> luma(stride * height, y), chroma(stride * height / 2);

        for (std::size_t i = 0; i < chroma.size(); ++i) {
            chroma[i] = behavior_.interleaved ? (i % 2 ? v : u) : (i < chroma.size() / 2 ? u : v);
        }

        pending_.pop_front();
        const auto *second = chroma.data() + (behavior_.interleaved ? 1 : chroma.size() / 2);
        sink({width, height, luma.data(), chroma.data(), second, stride,
              behavior_.interleaved ? stride : stride / 2, behavior_.interleaved ? 2U : 1U});
    }

    os::VideoStream stream_;
    Behavior behavior_;
    std::vector<unsigned> &levels_;
    std::size_t &units_;
    std::deque<std::array<std::uint8_t, 3>> pending_;
};
struct FakeHost {
    FakeDecoder::Behavior behavior;
    std::vector<unsigned> levels;
    std::size_t units = 0;

    StartupVideoDecoder decoder() {
        return [this](const os::VideoStream &stream) {
            return std::make_unique<FakeDecoder>(stream, behavior, levels, units);
        };
    }
};

std::unique_ptr<os::VideoDecoder> no_decoder(const os::VideoStream &) {
    throw std::runtime_error("STARTUP_VIDEO: no host decoder");
}

// Every decoded frame of the archive matches its synthetic color.
void decoded_colors(const std::filesystem::path &output, unsigned width, unsigned height,
                    std::size_t count, bool bt709, bool full_range) {
    const auto entries = stored_entries(output);

    need(entries.size() == count + 1 && entries.at("desc.txt") == std::to_string(width) + " " +
                                                                          std::to_string(height) +
                                                                          " 10\np 1 0 part0\n",
         "decoded archive layout differs");

    for (std::size_t i = 0; i < count; ++i) {
        char name[32];
        std::snprintf(name, sizeof name, "part0/%05zu.png", i);
        need(solid(png_rgb(entries.at(name), width, height), colors[i], bt709, full_range),
             "decoded frame differs");
    }
}

// The archive of a video the host cannot decode: one black frame held for the frames of the video
// up to the player limit.
void fallback_checks(const std::filesystem::path &directory, const std::string &clip) {
    const auto output = directory / "fallback.zip";
    const auto black = [&](unsigned held, const char *message) {
        const auto entries = stored_entries(output);
        need(entries.size() == 2 &&
                     entries.at("desc.txt") == "48 32 10\np 1 " + std::to_string(held) + " part0\n",
             message);
        const auto pixels = png_rgb(entries.at("part0/00000.png"), 48, 32);
        need(std::all_of(pixels.begin(), pixels.end(),
                         [](std::uint8_t value) {
                             return value == 0;
                         }),
             "the fallback frame must be black");
    };

    for (const auto &[seconds, held] :
         std::vector<std::pair<double, unsigned>>{{60, 4}, {.3, 2}, {.01, 0}}) {
        need(!write_startup_video_animation(clip, seconds, output, no_decoder),
             "an animation without a decoder must not report decoded frames");
        black(held, "the black animation must last the video up to the player limit");
    }

    need(bytes(output) ==
                 (write_startup_video_animation(clip, .01, output, no_decoder), bytes(output)),
         "the same video must give the same archive");
    need(!write_startup_video_animation(clip, 60, output,
                                        [](const os::VideoStream &) {
                                            return std::unique_ptr<os::VideoDecoder>();
                                        }),
         "a missing decoder must leave the black animation");
    black(4, "a missing decoder must leave the black animation");
    FakeHost failing{{.fail_at = 3}};
    need(!write_startup_video_animation(clip, 60, output, failing.decoder()),
         "a decoder that rejects a frame must leave the black animation");
    black(4, "a rejected frame must leave the black animation of the whole video");
    FakeHost losing{{.delayed = true, .lose_last = true}};
    need(!write_startup_video_animation(clip, 60, output, losing.decoder()),
         "a decoder that loses a frame must leave the black animation");
    black(4, "a lost frame must leave the black animation of the whole video");
}

// The decoder gets the parameter sets and every access unit; its pictures become the frames in
// order whatever their chroma layout, coded size or delay.
void decoder_checks(const std::filesystem::path &directory, const std::string &clip) {
    const auto output = directory / "decoded.zip";

    for (const auto &behavior :
         {FakeDecoder::Behavior{}, FakeDecoder::Behavior{.interleaved = true, .padded = true},
          FakeDecoder::Behavior{.padded = true, .delayed = true}}) {
        FakeHost host{behavior};
        need(write_startup_video_animation(clip, 60, output, host.decoder()),
             "the video must decode");
        decoded_colors(output, 48, 32, colors.size(), true, false);
        need(host.units == colors.size() && host.levels == std::vector<unsigned>{51},
             "the decoder must get every frame and the lowered level");
    }

    FakeHost cut{{.delayed = true}};
    need(write_startup_video_animation(clip, .3, output, cut.decoder()), "the video must decode");
    decoded_colors(output, 48, 32, 3, true, false);
    need(cut.units == 4, "decoding must stop at the player limit");

    for (const auto &[matrix, full_range] :
         std::vector<std::pair<unsigned, bool>>{{6, false}, {5, true}, {1, true}, {2, false}}) {
        const auto file = directory / "colour.mp4";
        startup_video_fixture::mp4(file, 48, 32, 10, std::vector(colors.begin(), colors.end()),
                                   {.colour = startup_video_fixture::Colour{matrix, full_range},
                                    .parameter_sets_in_frame = true});
        FakeHost host;
        need(write_startup_video_animation(bytes(file), 60, output, host.decoder()),
             "the video must decode");
        decoded_colors(output, 48, 32, colors.size(), matrix != 5 && matrix != 6, full_range);
        need(host.levels == std::vector<unsigned>{51, 51},
             "parameter sets inside a frame must get the lowered level");
    }
}

void reader_checks(const std::filesystem::path &directory, const std::string &clip) {
    const auto video = read_startup_video(clip);

    need(video.width == 48 && video.height == 32 && video.fps == 10 && video.frames.size() == 5 &&
                 video.length_bytes == 4 && video.bt709 && !video.full_range,
         "video description differs");
    need(video.parameter_sets.size() == 2 && video.parameter_sets[0][0] == 0x67 &&
                 video.parameter_sets[0][3] == 51 && video.parameter_sets[1][0] == 0x68,
         "the parameter sets must carry the lowered level");
    const auto file = directory / "colour.mp4";
    startup_video_fixture::mp4(file, 48, 32, 10, std::vector(colors.begin(), colors.end()),
                               {.colour = startup_video_fixture::Colour{6, true}});
    const auto described = read_startup_video(bytes(file));
    need(!described.bt709 && described.full_range, "the colour description must be read");
    // 3840x1440 at 30 fps fits 5.1, 3840x2160 at 60 fps 5.2, unless the reference frames
    // exceed the decoded picture buffer.
    need(startup_video_level(60, 21600, 30, 1) == 51 &&
                 startup_video_level(60, 32400, 60, 1) == 52 &&
                 startup_video_level(60, 32400, 60, 6) == 60 &&
                 startup_video_level(60, 40000, 30, 1) == 60 &&
                 startup_video_level(51, 21600, 30, 1) == 51 &&
                 startup_video_level(40, 8160, 30, 4) == 40,
         "level selection differs");
    rejects(
            [&] {
                read_startup_video("not an mp4 file");
            },
            "a non-MP4 file must be rejected");
    rejects(
            [&] {
                read_startup_video(clip.substr(0, clip.size() - 40));
            },
            "a truncated index must be rejected");
    rejects(
            [&] {
                read_startup_video(replaced(clip, "avc1", "hvc1"));
            },
            "a video other than H.264 must be rejected");
    rejects(
            [&] {
                std::string size;
                startup_video_fixture::word(size, 48, 2);
                startup_video_fixture::word(size, 32, 2);
                std::string larger;
                startup_video_fixture::word(larger, 4096, 2);
                startup_video_fixture::word(larger, 32, 2);
                read_startup_video(replaced(clip, size, larger));
            },
            "a video wider than 3840 pixels must be rejected");
    rejects(
            [&] {
                std::string timing;
                startup_video_fixture::word(timing, 5, 4);
                startup_video_fixture::word(timing, 9000, 4);
                std::string none;
                startup_video_fixture::word(none, 5, 4);
                startup_video_fixture::word(none, 0, 4);
                read_startup_video(replaced(clip, timing, none));
            },
            "a zero frame duration must be rejected");
    rejects(
            [&] {
                startup_video_fixture::mp4(file, 48, 32, 10,
                                           std::vector(colors.begin(), colors.end()),
                                           {.sample_table = startup_video_fixture::full(
                                                    "ctts", std::string(4, '\0'))});
                read_startup_video(bytes(file));
            },
            "frames with composition offsets must be rejected");
}

// With the host decoder, every frame decodes into a PNG at the video size and the archive plays at
// the video rate up to the player limit. The video declares level 6.0, BT.601 and studio range and
// repeats its parameter sets in the first frame, as the OEM videos do.
int decode_checks(const std::filesystem::path &directory, bool required) {
    const auto file = directory / "host.mp4";

    startup_video_fixture::mp4(
            file, 320, 240, 10, std::vector(colors.begin(), colors.end()),
            {.colour = startup_video_fixture::Colour{6, false}, .parameter_sets_in_frame = true});
    const auto clip = bytes(file);
    const auto video = read_startup_video(clip);

    try {
        os::host_video_decoder({video.width, video.height, video.full_range, video.parameter_sets});
    } catch (const std::exception &error) {
        std::cout << "no host H.264 decoder: " << error.what() << '\n';

        return required ? 1 : 77;
    }

    const auto output = directory / "host.zip";
    need(write_startup_video_animation(clip, 60, output), "the video must decode");
    decoded_colors(output, 320, 240, colors.size(), false, false);
    need(write_startup_video_animation(clip, .3, output), "the video must decode");
    decoded_colors(output, 320, 240, 3, false, false);
    std::cout << "startup video decode tests passed\n";

    return 0;
}

// The OEM startup videos of a profile, extracted from its imported QNX image: their description,
// and with a host decoder their frames up to the OEM player limit.
int oem_checks(const std::filesystem::path &root, const std::filesystem::path &directory,
               const std::string &profile) {
    const auto definition = read_private_json(root / "profiles" / profile / "profile.json",
                                              4 << 20);
    const auto &videos = definition.at("startupVideos");
    bool decoder = true;

    for (const auto &[screen, pin] : videos.items()) {
        const auto image = qnx_originals_directory(root, profile) /
                           pin.at("acquisition").at("image").get<std::string>();

        if (!std::filesystem::is_regular_file(image)) {
            std::cout << "imported QNX image absent: " << image.string() << '\n';

            return 77;
        }

        const auto mp4 = portable_startup_video(root, profile, startup_video_pin(pin));
        const auto video = read_startup_video(mp4);
        // The HUD video is 3840x2160; the multimedia and cluster videos are 3840x1440.
        const unsigned height = screen == "hud" ? 2160 : 1440;
        need(video.width == 3840 && video.height == height && video.fps == 30 && !video.bt709 &&
                     !video.full_range && video.parameter_sets[0][3] == 51,
             "OEM video description differs");
        const auto seconds = pin.at("seconds").get<double>();
        const auto frames = std::min<std::size_t>(video.frames.size(), std::size_t(seconds * 30));
        const auto output = directory / (screen + ".zip");
        const auto start = std::chrono::steady_clock::now();

        if (!write_startup_video_animation(mp4, seconds, output)) {
            decoder = false;
            std::cout << screen << ": not decoded on this host\n";
            continue;
        }

        const auto entries = stored_entries(output);
        need(entries.size() == frames + 1 &&
                     entries.at("desc.txt") ==
                             "3840 " + std::to_string(height) + " 30\np 1 0 part0\n",
             "OEM archive layout differs");
        bool visible = false;

        for (const auto &[name, png] : entries) {
            if (name != "desc.txt" && !visible) {
                const auto rgb = png_rgb(png, 3840, height);
                visible = std::any_of(rgb.begin(), rgb.end(), [](std::uint8_t value) {
                    return value > 32;
                });
            }
        }

        need(visible, "an OEM video must decode into visible frames");
        std::cout << screen << ": " << frames << " frames in "
                  << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                  << " s\n";
    }

    return decoder ? 0 : 77;
}
} // namespace

int main(int argc, char **argv) {
    try {
        need(argc >= 2,
             "usage: startup_video_frames_test ROOT [--decode [--required] | --oem PROFILE]");
        const auto directory = std::filesystem::path(argv[1]) / ".local/build/tmp" /
                               ("startup-video-frames-" + random_token());
        std::filesystem::create_directories(directory);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{directory};
        const std::string mode = argc >= 3 ? argv[2] : "";

        if (mode == "--decode") {
            return decode_checks(directory, argc >= 4 && std::string_view(argv[3]) == "--required");
        }

        if (mode == "--oem") {
            need(argc >= 4, "--oem requires a profile");

            return oem_checks(argv[1], directory, argv[3]);
        }

        startup_video_fixture::mp4(directory / "clip.mp4", 48, 32, 10,
                                   std::vector(colors.begin(), colors.end()));
        const auto clip = bytes(directory / "clip.mp4");
        reader_checks(directory, clip);
        fallback_checks(directory, clip);
        decoder_checks(directory, clip);
        std::cout << "startup video frames tests passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
