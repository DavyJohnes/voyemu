#pragma once
#include "check.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "os/socket.hpp"
#include "process.hpp"
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <stdexcept>
namespace voyemu {
inline bool android_audio_output_enabled(const Json &specification) {
    if (specification.contains("audioOutput") && !specification.at("audioOutput").is_boolean()) {
        throw VoyemuError("ANDROID_AUDIO_MODE", "boolean audioOutput required");
    }

    return specification.value("audioOutput", false);
}

// The caller validates its owned directory before selecting these private files.
inline void configure_android_wav_audio(ProcessSpec &process, const std::filesystem::path &wave,
                                        const std::filesystem::path &input) {
    const auto reject = [](bool accepted, const char *why) {
        if (!accepted) {
            throw VoyemuError("ANDROID_AUDIO_DIAGNOSTIC", why);
        }
    };

    reject(std::none_of(process.arguments.begin(), process.arguments.end(),
                        [](const auto &arg) {
                            return arg == "-audio" || arg == "-no-audio" || arg == "-noaudio" ||
                                   arg == "-no_audio" || arg == "-allow-host-audio" ||
                                   arg == "-allow_host_audio";
                        }),
           "incoming audio override rejected");
    reject(wave.is_absolute() && input.is_absolute() && wave != input,
           "distinct absolute WAV paths required");

    for (const auto &file : {wave, input}) {
        reject(!std::filesystem::exists(file) && !std::filesystem::is_symlink(file),
               "fresh private WAV paths required");
    }

    process.environment["QEMU_AUDIO_DRV"] = "wav";
    process.environment["QEMU_AUDIO_OUT_DRV"] = "wav";
    process.environment["QEMU_AUDIO_IN_DRV"] = "none";
    process.environment["QEMU_AUDIO_ADC_VOICES"] = "0";
    process.environment["QEMU_WAV_PATH"] = wave.string();
    process.environment["QEMU_WAV_IN_PATH"] = input.string();
    process.environment["QEMU_WAV_FREQUENCY"] = "44100";
    process.environment["QEMU_WAV_FORMAT"] = "S16";
    process.environment["QEMU_WAV_DAC_FIXED_CHANNELS"] = "2";
    process.arguments.insert(process.arguments.end(), {"-audio", "wav"});
}

inline std::optional<std::string> android_pulse_audio_server() {
    std::filesystem::path socket;
    const char *server = std::getenv("PULSE_SERVER");
    const bool explicit_server = server && *server;

    if (explicit_server) {
        const std::string_view address(server);
        constexpr std::string_view prefix = "unix:";
        socket = address.starts_with(prefix) ? address.substr(prefix.size()) : address;

        if (!socket.is_absolute() || address.find_first_of(" \t\n\r\f\v") != address.npos) {
            throw VoyemuError("ANDROID_AUDIO_MODE",
                              "one local PulseAudio socket required in PULSE_SERVER");
        }
    } else if (const char *runtime = std::getenv("XDG_RUNTIME_DIR"); runtime && *runtime) {
        socket = std::filesystem::path(runtime) / "pulse/native";
    }

    const auto path = socket.string();

    if (!socket.is_absolute() || path.find_first_of(" \t\n\r\f\v") != path.npos) {
        return std::nullopt;
    }

    std::error_code error;
    const auto status = std::filesystem::status(socket, error);

    if (explicit_server && std::filesystem::exists(status) && !std::filesystem::is_socket(status)) {
        throw VoyemuError("ANDROID_AUDIO_MODE", "PULSE_SERVER does not name a local socket");
    }

    if (!std::filesystem::is_socket(status) || !os::unix_socket_listening(path)) {
        return std::nullopt;
    }

    return "unix:" + path;
}

inline bool configure_android_audio(ProcessSpec &process, bool output,
                                    const Json &diagnostic = nullptr,
                                    const std::filesystem::path &session = {},
                                    unsigned generation = 0) {
    if (!diagnostic.is_null()) {
        if (os::platform_name == "Linux") {
            throw VoyemuError("ANDROID_AUDIO_DIAGNOSTIC",
                              "direct WAV route requires macOS or Windows");
        }

        const auto reject = [](bool accepted, const char *why) {
            if (!accepted) {
                throw VoyemuError("ANDROID_AUDIO_DIAGNOSTIC", why);
            }
        };
        reject(output && diagnostic.is_string() && diagnostic == "wav",
               "exact enabled WAV opt-in required");
        reject(std::none_of(process.arguments.begin(), process.arguments.end(),
                            [](const auto &arg) {
                                return arg == "-audio" || arg == "-no-audio" || arg == "-noaudio" ||
                                       arg == "-no_audio" || arg == "-allow-host-audio" ||
                                       arg == "-allow_host_audio";
                            }),
               "incoming audio override rejected");
        const auto id = session.filename().string();
        reject(generation > 0 && session.is_absolute() && session.lexically_normal() == session &&
                       std::filesystem::canonical(session) == session &&
                       std::filesystem::is_directory(session) && hex_text(id, 32) &&
                       session.parent_path().filename() == "sessions",
               "owned canonical session directory required");

        for (const auto &path : os::ancestors(session)) {
            reject(!os::reparse_point(path), "session reparse path rejected");
            reject(!std::filesystem::is_symlink(std::filesystem::symlink_status(path)),
                   "session symlink path rejected");
        }

        const auto wave = session / ("android-audio-g" + std::to_string(generation) + ".wav"),
                   input = session / ("android-no-input-g" + std::to_string(generation) + ".wav");
        configure_android_wav_audio(process, wave, input);

        return true;
    }

    if (!output) {
        process.arguments.push_back("-no-audio");

        return false;
    }

    // SDK documents directional backends. Never open a host recording device.
    const char *driver = os::host_windows ? "dsound" : "coreaudio";

    if (os::platform_name == "Linux") {
        const auto server = android_pulse_audio_server();

        if (!server) {
            process.arguments.push_back("-no-audio");

            return false;
        }

        driver = "pa";
        process.environment["PULSE_SERVER"] = *server;
        process.environment["QEMU_AUDIO_ADC_VOICES"] = "0";
        // The SDK executable selected by -no-window lacks PulseAudio.
        std::replace(process.arguments.begin(), process.arguments.end(),
                     std::string_view{"-no-window"}, std::string_view{"-qt-hide-window"});
    }

    process.environment["QEMU_AUDIO_DRV"] = driver;
    process.environment["QEMU_AUDIO_OUT_DRV"] = driver;
    process.environment["QEMU_AUDIO_IN_DRV"] = "none";

    // -allow-host-audio is deliberately absent; guest input remains silent.
    return true;
}
} // namespace voyemu
