#pragma once
#include "model.hpp"
#include <stdexcept>
namespace voyemu {
inline bool android_audio_startup_required(const Json &spec) {
    if (!spec.contains("audio_startup")) {
        return false;
    }

    if (spec.at("audio_startup") !=
        Json{{"schema", 1}, {"authority", "com.voyemu.output.audio.startup"}}) {
        throw VoyemuError("ANDROID_AUDIO_STARTUP", "invalid capability");
    }

    return true;
}

inline void validate_android_audio_startup_reply(const Json &reply, const std::string &session,
                                                 const std::string &boot, std::uint64_t generation,
                                                 bool require_zero) {
    const auto reject = []() {
        throw VoyemuError("ANDROID_AUDIO_STARTUP",
                          "matching all-channel acknowledgement unavailable");
    };

    try {
        if (!reply.is_object() || reply.value("schema", 0) != 1 ||
            !reply.at("schema").is_number_integer() ||
            reply.value("launch_session", std::string{}) != session ||
            reply.value("boot_id", std::string{}) != boot ||
            !reply.contains("android_generation") ||
            !reply.at("android_generation").is_number_unsigned() ||
            reply.at("android_generation") != generation || !reply.value("completed", false)) {
            reject();
        }

        const auto &streams = reply.at("stream_readbacks");

        if (!streams.is_array() || streams.size() != 11 || !reply.at("all_zero").is_boolean() ||
            !reply.at("oem25_zero").is_boolean()) {
            reject();
        }

        bool zero = true;

        for (const auto &value : streams) {
            if (!value.is_number_integer() || value.get<std::int64_t>() < 0) {
                reject();
            }

            zero = zero && value == 0;
        }

        if (reply.at("all_zero") != (zero && reply.at("oem25_zero").get<bool>()) ||
            (require_zero && !reply.at("all_zero").get<bool>())) {
            reject();
        }
    } catch (const Json::exception &) {
        reject();
    }
}
} // namespace voyemu
