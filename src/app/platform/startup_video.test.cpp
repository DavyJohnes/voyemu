#include "startup_video.hpp"
#include "startup_animation_fixture.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>

using namespace voyemu;
namespace {
void need(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string png(const std::filesystem::path &directory, unsigned width, unsigned height,
                std::uint8_t gray) {
    std::vector<std::uint8_t> bgra(std::size_t(width) * height * 4, gray);

    for (std::size_t at = 3; at < bgra.size(); at += 4) {
        bgra[at] = 255;
    }

    const auto file = directory / "frame.png";
    window::save_png(file, int(width), int(height), bgra);
    std::ifstream source(file, std::ios::binary);

    return {std::istreambuf_iterator<char>(source), {}};
}

bool gray(const window::StartupVideoFrame &frame, std::uint8_t value) {
    for (std::size_t i = 0; i < frame.image.rgba.size(); ++i) {
        if (frame.image.rgba[i] != (i % 4 == 3 ? 255 : value)) {
            return false;
        }
    }

    return true;
}

// Polls the worker until a frame becomes available.
std::optional<window::StartupVideoFrame> wait_take(window::StartupVideoPlayer &player,
                                                   double seconds) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while (std::chrono::steady_clock::now() < deadline) {
        if (auto frame = player.take(seconds)) {
            return frame;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    return std::nullopt;
}
} // namespace

int main() {
    try {
        const auto directory = std::filesystem::temp_directory_path() /
                               ("voyemu-startup-video-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(directory);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{directory};
        std::vector<std::uint8_t> levels;
        std::vector<std::pair<std::string, std::string>> entries = {
                {"desc.txt", "64 24 10\np 1 0 part0\n"}};

        for (unsigned i = 0; i < 12; ++i) {
            char name[32];
            std::snprintf(name, sizeof name, "part0/%05u.png", i);
            levels.push_back(std::uint8_t(10 + 20 * i));
            entries.emplace_back(name, png(directory, 64, 24, levels.back()));
        }

        const auto file = directory / "video.zip";
        startup_fixture::zip(file, entries);
        const auto video = window::read_startup_animation(file);

        // Two threads decode ahead of the clock: frames come in order, each once, late frames
        // are dropped, no more than workers + 2 frames wait, and a due frame overtakes the frames
        // before it.
        {
            window::StartupVideoPlayer player(video, 2);
            need(player.seconds() == 1.2, "video length differs");
            const auto first = wait_take(player, 0);
            need(first && first->index == 0 && first->image.width == 64 &&
                         first->image.height == 24 && gray(*first, levels[0]),
                 "first frame differs");
            need(!player.take(.05), "a frame must not be handed twice or early");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const auto third = player.take(.25);
            need(third && third->index == 2 && gray(*third, levels[2]),
                 "late frames must be dropped for the due one");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            const auto bounded = player.take(60);
            need(bounded && bounded->index == 6 && gray(*bounded, levels[6]),
                 "decoding must stop four frames ahead of the last handed frame");
            need(!player.finished(1.2), "playback must not finish early");
            const auto last = wait_take(player, 60);
            need(last && last->index == 11 && gray(*last, levels[11]),
                 "a due last frame must overtake the frames before it");
            need(player.finished(1.2) && player.error().empty(), "playback must finish cleanly");
        }

        // Four threads may finish frames out of order, yet a window polling in real time gets them
        // in order, each with its own picture, up to the last frame.
        {
            window::StartupVideoPlayer player(video, 4);
            const auto started = std::chrono::steady_clock::now();
            const auto elapsed = [&] {
                return std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
                        .count();
            };
            std::optional<std::size_t> previous;

            double seconds = 0;

            while (!player.finished(seconds)) {
                if (const auto frame = player.take(seconds)) {
                    need((!previous || frame->index > *previous) && frame->index < levels.size() &&
                                 gray(*frame, levels[frame->index]),
                         "frames must come in order with their own pictures");
                    previous = frame->index;
                }

                need(seconds < 10, "real-time playback must finish");
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                seconds = elapsed();
            }

            need(previous == 11 && player.error().empty(), "real-time playback must end cleanly");
        }

        // Stopping returns promptly while threads decode and while they wait for a free slot.
        {
            const auto started = std::chrono::steady_clock::now();
            { window::StartupVideoPlayer player(video, 4); }
            {
                window::StartupVideoPlayer player(video, 2);
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            need(std::chrono::steady_clock::now() - started < std::chrono::seconds(2),
                 "stopping must not wait for the whole video");
        }

        // Three videos at once share the cores but one, each with one to three threads.
        {
            const unsigned cores = std::thread::hardware_concurrency();
            const auto shared = window::startup_video_workers(3),
                       single = window::startup_video_workers(1);
            need(shared >= 1 && shared <= 3 && (shared == 1 || shared * 3 < cores),
                 "simultaneous videos must share the decoding budget");
            need(single >= shared && single <= 3, "one video must get a bounded budget");
        }

        // A palette frame, as a boot animation may store, decodes with its colors and alpha.
        {
            static constexpr char indexed[] =
                    "\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00"
                    "\x00\x02\x00\x00\x00\x01\x08\x03\x00\x00\x00\xc3\xfc\x8f\xb8\x00\x00\x00"
                    "\x06\x50\x4c\x54\x45\xc8\x0a\x14\x1e\x28\xfa\xdb\x9a\xc6\xad\x00\x00\x00"
                    "\x02\x74\x52\x4e\x53\xff\x80\x08\x0f\xb3\x6a\x00\x00\x00\x0b\x49\x44\x41"
                    "\x54\x78\xda\x63\x60\x60\x04\x00\x00\x04\x00\x02\x2c\xde\x48\xad\x00\x00"
                    "\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82";
            const auto image =
                    window::decode_png_bytes(std::string_view(indexed, sizeof indexed - 1));
            need(image.width == 2 && image.height == 1 &&
                         image.rgba ==
                                 std::vector<std::uint8_t>{200, 10, 20, 255, 30, 40, 250, 128},
                 "palette frame differs");
        }

        // The black stand-in of an undecoded video: one frame held for the video length.
        startup_fixture::zip(file, {{"desc.txt", "64 24 10\np 1 7 part0\n"},
                                    {"part0/00000.png", png(directory, 64, 24, 0)}});
        {
            window::StartupVideoPlayer player(window::read_startup_animation(file), 2);
            const auto black = wait_take(player, 0);
            need(black && black->index == 0 && gray(*black, 0), "black frame differs");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            need(!player.finished(.7) && !player.take(.2),
                 "a held frame must not be decoded again or end early");
            need(player.finished(.8) && player.error().empty(),
                 "the held frame must end at the video length");
        }

        // A frame of another size than desc.txt stops playback with an error.
        startup_fixture::zip(file, {{"desc.txt", "64 24 10\np 1 0 part0\n"},
                                    {"part0/00000.png", png(directory, 32, 24, 0)}});
        {
            window::StartupVideoPlayer player(window::read_startup_animation(file), 2);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

            while (!player.finished(0) && std::chrono::steady_clock::now() < deadline) {
                player.take(60);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            need(player.finished(0) && player.error().starts_with("WINDOW_STARTUP: "),
                 "a foreign frame size must stop playback with an error");
        }

        // The videos play from each entry into the startup phase and stop when it ends.
        {
            window::StartupVideoTrigger trigger;
            need(!trigger.update(false, 1), "no video before the startup phase");
            need(trigger.update(true, 2) == 0. && trigger.update(true, 3.5) == 1.5,
                 "video time counts from the startup phase entry");
            need(!trigger.update(false, 5), "no video after the startup phase");
            need(trigger.update(true, 6) == 0., "the next startup phase plays the videos again");
        }

        std::cout << "startup video tests passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
