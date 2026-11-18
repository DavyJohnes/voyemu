#pragma once
#include "check.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include <algorithm>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace voyemu {
inline bool android_audio_output_enabled(const Json &specification) {
    if (specification.contains("audioOutput") && !specification.at("audioOutput").is_boolean()) {
        throw VoyemuError("ANDROID_AUDIO_MODE", "boolean audioOutput required");
    }

    const bool enabled = specification.value("audioOutput", false);
#if !defined(__APPLE__) && !defined(_WIN32)
    if (enabled) {
        throw VoyemuError("ANDROID_AUDIO_MODE", "unsupported output host");
    }
#endif

    return enabled;
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

inline void configure_android_audio(ProcessSpec &process, bool output,
                                    const Json &diagnostic = nullptr,
                                    const std::filesystem::path &session = {},
                                    unsigned generation = 0) {
    if (!diagnostic.is_null()) {
#if defined(__APPLE__) || defined(_WIN32)
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
#ifdef _WIN32
            const auto attributes = GetFileAttributesW(path.c_str());
            reject(attributes != INVALID_FILE_ATTRIBUTES &&
                           !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                   "session reparse path rejected");
#endif
            reject(!std::filesystem::is_symlink(std::filesystem::symlink_status(path)),
                   "session symlink path rejected");
        }

        const auto wave = session / ("android-audio-g" + std::to_string(generation) + ".wav"),
                   input = session / ("android-no-input-g" + std::to_string(generation) + ".wav");
        configure_android_wav_audio(process, wave, input);

        return;
#else
        throw VoyemuError("ANDROID_AUDIO_DIAGNOSTIC", "direct WAV route requires macOS or Windows");
#endif
    }

    if (!output) {
        process.arguments.push_back("-no-audio");

        return;
    }

    // SDK documents directional backends. Never open a host recording device.
#ifdef _WIN32
    const char *driver = "dsound";
#else
    const char *driver = "coreaudio";
#endif
    process.environment["QEMU_AUDIO_DRV"] = driver;
    process.environment["QEMU_AUDIO_OUT_DRV"] = driver;
    process.environment["QEMU_AUDIO_IN_DRV"] = "none";
    // -allow-host-audio is deliberately absent; guest input remains silent.
}
} // namespace voyemu
