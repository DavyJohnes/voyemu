#include "android_audio.hpp"
#include "android_audio_test_host.hpp"
#include "os/platform.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
using namespace voyemu;

static void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

int main() {
    try {
        check(!android_audio_output_enabled(Json::object()), "existing launch defaults to silent");
        bool rejected = false;
        try {
            android_audio_output_enabled({{"audioOutput", "yes"}});
        } catch (...) {
            rejected = true;
        }

        check(rejected, "invalid audio type rejected before launch");
        ProcessSpec silent;
        configure_android_audio(silent, false);
        check(silent.arguments == std::vector<std::string>{"-no-audio"},
              "silent mode still disables audio");

        check(!android_audio_output_enabled({{"audioOutput", false}}),
              "explicit legacy silence remains supported");
        check(android_audio_output_enabled({{"audioOutput", true}}), "output enabled explicitly");

        if (os::platform_name == "Linux") {
            linux_pulse_audio_case();
        } else {
            ProcessSpec output;
            output.environment["QEMU_AUDIO_IN_DRV"] = "coreaudio";
            configure_android_audio(output, true);
            const std::string expected = os::host_windows ? "dsound" : "coreaudio";
            check(output.environment.at("QEMU_AUDIO_DRV") == expected &&
                          output.environment.at("QEMU_AUDIO_OUT_DRV") == expected &&
                          output.environment.at("QEMU_AUDIO_IN_DRV") == "none",
                  "output backend must match the host and cannot inherit an enabled microphone");
            check(output.arguments.empty(),
                  "output does not add global audio-disable or host-recording flags");
            const auto fixture = std::filesystem::current_path() / ".local/build/tmp" /
                                 ("audio-diagnostic-" + random_token());
            const auto directory = fixture / ".local/machines/profile/sessions" /
                                   std::string(32, 'a');
            std::filesystem::create_directories(directory);
            struct Cleanup {
                std::filesystem::path path;

                ~Cleanup() {
                    std::error_code e;
                    std::filesystem::remove_all(path, e);
                }
            } cleanup{fixture};
            ProcessSpec capture;
            capture.environment["QEMU_WAV_PATH"] = "C:\\foreign.wav";
            capture.environment["QEMU_WAV_IN_PATH"] = "C:\\microphone.wav";
            configure_android_audio(capture, true, "wav", directory, 1);
            check(capture.executable.empty() &&
                          capture.arguments == std::vector<std::string>{"-audio", "wav"},
                  "diagnostic preserves executable and enables actual audio");
            check(capture.environment.at("QEMU_AUDIO_DRV") == "wav" &&
                          capture.environment.at("QEMU_AUDIO_OUT_DRV") == "wav" &&
                          capture.environment.at("QEMU_AUDIO_IN_DRV") == "none" &&
                          capture.environment.at("QEMU_AUDIO_ADC_VOICES") == "0",
                  "WAV output and zero capture voices required");
            check(capture.environment.at("QEMU_WAV_PATH") ==
                                  (directory / "android-audio-g1.wav").string() &&
                          capture.environment.at("QEMU_WAV_IN_PATH") ==
                                  (directory / "android-no-input-g1.wav").string(),
                  "inherited paths escaped owned session");
            check(!std::filesystem::exists(directory / "android-audio-g1.wav"),
                  "configuration itself created WAV");
            auto denied = [&](const Json &value, const std::filesystem::path &at, unsigned g,
                              bool enabled = true) {
                bool failed = false;
                try {
                    ProcessSpec p;
                    configure_android_audio(p, enabled, value, at, g);
                } catch (...) {
                    failed = true;
                }

                check(failed, "unsafe WAV diagnostic accepted");
            };
            denied("dsound", directory, 1);
            denied(true, directory, 1);
            denied("wav", directory, 0);
            denied("wav", directory, 1, false);
            denied("wav", fixture, 1);
            denied("wav", std::filesystem::path("relative"), 1);
            const auto legacy = fixture / "sessions/profile" / std::string(32, 'a');
            std::filesystem::create_directories(legacy);
            denied("wav", legacy, 1);
            const auto foreign = fixture / "foreign" / std::string(32, 'a');
            std::filesystem::create_directories(foreign);
            denied("wav", foreign, 1);
            denied("wav", directory / ".." / std::string(32, 'a'), 1);
            const auto invalid_id = directory.parent_path() / std::string(32, 'g');
            std::filesystem::create_directories(invalid_id);
            denied("wav", invalid_id, 1);
            {
                std::ofstream stale(directory / "android-audio-g1.wav");
                stale << "owned old capture";
            }
            denied("wav", directory, 1);
            ProcessSpec next;
            configure_android_audio(next, true, "wav", directory, 2);
            check(next.environment.at("QEMU_WAV_PATH") ==
                          (directory / "android-audio-g2.wav").string(),
                  "restart generation reused capture");
            {
                std::ofstream stale_input(directory / "android-no-input-g4.wav");
                stale_input << "must never be read";
            }
            denied("wav", directory, 4);
            std::error_code symlink_error;
            std::filesystem::create_symlink(directory / "android-audio-g1.wav",
                                            directory / "android-audio-g5.wav", symlink_error);

            if (!symlink_error) {
                denied("wav", directory, 5);
            }

            ensure_private_directory(fixture / "linked");
            const auto linked = fixture / "linked/sessions";
            std::error_code link_error;
            std::filesystem::create_directory_symlink(directory.parent_path(), linked, link_error);

            if (!link_error) {
                denied("wav", linked / std::string(32, 'a'), 6);
            }

            ProcessSpec conflict;
            conflict.arguments = {"-audio", "dsound"};
            rejected = false;
            try {
                configure_android_audio(conflict, true, "wav", directory, 3);
            } catch (...) {
                rejected = true;
            }

            check(rejected, "explicit inherited audio override accepted");
            wine_volume_root_case(directory);
        }

        {
            const auto directory = std::filesystem::current_path() / ".local/build/tmp" /
                                   ("preparation-audio-" + random_token());
            ensure_private_directory(directory);
            struct Cleanup {
                std::filesystem::path path;

                ~Cleanup() {
                    std::error_code error;
                    std::filesystem::remove_all(path, error);
                }
            } cleanup{directory};
            const auto wave = directory / "android-audio-g1.wav",
                       input = directory / "android-no-input-g1.wav";
            ProcessSpec capture;
            capture.arguments = {"-no-window"};
            capture.environment = {{"QEMU_AUDIO_DRV", "coreaudio"},
                                   {"QEMU_AUDIO_OUT_DRV", "coreaudio"},
                                   {"QEMU_AUDIO_IN_DRV", "coreaudio"},
                                   {"QEMU_WAV_PATH", "foreign.wav"}};
            configure_android_wav_audio(capture, wave, input);
            check(capture.arguments == std::vector<std::string>({"-no-window", "-audio", "wav"}),
                  "preparation must preserve guest audio and background launch");
            check(capture.environment.at("QEMU_AUDIO_DRV") == "wav" &&
                          capture.environment.at("QEMU_AUDIO_OUT_DRV") == "wav" &&
                          capture.environment.at("QEMU_AUDIO_IN_DRV") == "none" &&
                          capture.environment.at("QEMU_AUDIO_ADC_VOICES") == "0",
                  "preparation opened host playback or microphone");
            check(capture.environment.at("QEMU_WAV_PATH") == wave.string() &&
                          capture.environment.at("QEMU_WAV_IN_PATH") == input.string(),
                  "preparation inherited foreign capture paths");
            auto deny = [&](ProcessSpec command, const std::filesystem::path &out,
                            const std::filesystem::path &in) {
                bool failed = false;
                try {
                    configure_android_wav_audio(command, out, in);
                } catch (...) {
                    failed = true;
                }

                check(failed, "unsafe preparation audio accepted");
            };
            deny({}, "relative.wav", input);
            deny({}, wave, wave);
            ProcessSpec conflict;
            conflict.arguments = {"-no-audio"};
            deny(conflict, wave, input);
            {
                std::ofstream stale(wave);
                stale << "previous";
            }
            deny({}, wave, input);
            ProcessSpec next;
            configure_android_wav_audio(next, directory / "android-audio-g2.wav",
                                        directory / "android-no-input-g2.wav");
            check(next.environment.at("QEMU_WAV_PATH") != capture.environment.at("QEMU_WAV_PATH"),
                  "next preparation boot overwrote previous audio evidence");
        }
        std::cout << "Android output-only plan passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
