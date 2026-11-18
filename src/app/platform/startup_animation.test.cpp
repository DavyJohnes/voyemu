#include "startup_animation.hpp"
#include "startup_animation_fixture.hpp"
#include <iostream>
#include <random>

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
        failed = std::string_view(error.what()).starts_with("WINDOW_STARTUP: ");
    }

    need(failed, message);
}
} // namespace

int main() {
    try {
        const auto directory =
                std::filesystem::temp_directory_path() /
                ("voyemu-startup-animation-" + std::to_string(std::random_device{}()));
        std::filesystem::create_directories(directory);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{directory};
        const auto file = directory / "bootanimation.zip";
        // Parts play in desc.txt order: a looping first part plays once, an empty part adds
        // nothing, a pause repeats the last frame and the final part holds.
        startup_fixture::zip(file, {{"desc.txt", "1920 720 10  \r\np   0   0   part0\r\n"
                                                 "p 2 1 part1\np 1 0 part2\np 0 0 part3\n"},
                                    {"part0/001.png", "a0"},
                                    {"part1/002.png", "b1"},
                                    {"part1/001.png", "b0"},
                                    {"part1/notes.txt", "ignored"},
                                    {"part3/001.png", "d0"},
                                    {"audio.wav", "ignored"}});
        const auto animation = window::read_startup_animation(file);
        need(animation.fps == 10 && animation.parts.size() == 4 && animation.parts[2].empty(),
             "desc.txt header and parts differ");
        const std::vector<std::pair<std::size_t, std::size_t>> expected = {
                {0, 0}, {1, 0}, {1, 1}, {1, 1}, {1, 0}, {1, 1}, {1, 1}, {3, 0}};
        need(animation.timeline == expected, "timeline order differs");
        need(window::startup_frame(animation, 0) == std::pair<std::size_t, std::size_t>{0, 0} &&
                     window::startup_frame(animation, .15) ==
                             std::pair<std::size_t, std::size_t>{1, 0} &&
                     window::startup_frame(animation, .39) ==
                             std::pair<std::size_t, std::size_t>{1, 1} &&
                     window::startup_frame(animation, .7) ==
                             std::pair<std::size_t, std::size_t>{3, 0} &&
                     window::startup_frame(animation, 1e300) ==
                             std::pair<std::size_t, std::size_t>{3, 0} &&
                     window::startup_frame(animation, -1) ==
                             std::pair<std::size_t, std::size_t>{0, 0},
             "frame timing at the desc.txt rate differs");
        need(window::read_startup_frame(animation, {1, 1}) == "b1" &&
                     window::read_startup_frame(animation, {3, 0}) == "d0",
             "stored frame bytes differ");

        const auto bad = directory / "bad.zip";
        const std::vector<std::pair<std::string, std::string>> frame = {{"part0/001.png", "a0"}};
        auto with = [&](std::string desc) {
            auto entries = frame;
            entries.emplace_back("desc.txt", std::move(desc));

            return entries;
        };
        startup_fixture::zip(bad, frame);
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "missing desc.txt accepted");
        // Frames keep their own size up to 3840x2160; the window scales them to the screen.
        startup_fixture::zip(bad, with("3840 1440 10\np 1 0 part0\n"));
        const auto large = window::read_startup_animation(bad);
        need(large.width == 3840 && large.height == 1440, "frame size from desc.txt differs");
        startup_fixture::zip(bad, with("3842 720 10\np 1 0 part0\n"));
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "oversized frames accepted");
        startup_fixture::zip(bad, with("1920 720 0\np 1 0 part0\n"));
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "zero rate accepted");
        startup_fixture::zip(bad, with("1920 720 10\nx 1 0 part0\n"));
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "unknown part type accepted");
        startup_fixture::zip(bad, with("1920 720 10\np 1 0 missing\n"));
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "animation without frames accepted");
        startup_fixture::zip(bad, with("1920 720 10\np 1 0 part0\n"), 8);
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "compressed frame accepted");
        startup_fixture::zip(bad, with("1920 720 10\np 100 999 part0\n"));
        const auto bounded = window::read_startup_animation(bad);
        need(bounded.timeline.size() == 100000 &&
                     window::read_startup_frame(bounded, bounded.timeline.back()) == "a0",
             "exact timeline limit rejected");
        startup_fixture::zip(bad, with("1920 720 10\np 100 1000 part0\n"));
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "repeated pauses exceeding timeline bound accepted");
        std::ofstream(bad, std::ios::binary) << "not a zip archive at all";
        rejects(
                [&] {
                    window::read_startup_animation(bad);
                },
                "non-ZIP accepted");
        std::cout << "Startup animation: desc.txt order, rate, pause, final hold, stored frames "
                     "timeline limit and eight rejections passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
