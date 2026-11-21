#include "api_internal.hpp"
#include "error.hpp"

namespace voyemu::api {
using namespace detail;

Json Api::dispatch_input(const std::string &op, const Json &request, Clock::time_point deadline) {
    if (op == "input.wheel.release") {
        return control(
                request,
                {{"op", "release_owner"}, {"owner", request.at("session").at("ownership_id")}},
                true);
    }

    if (op == "input.wheel.press" || op == "input.voice.trigger") {
        const auto id = op == "input.voice.trigger" ? std::string("voice")
                                                    : text(request, "button");
        const auto hold = number(request, "hold_ms", 100, 1, 2000);
        const auto owner = request.at("session").at("ownership_id");
#ifndef _WIN32
        if (op == "input.wheel.press" && (id == "volume_up" || id == "volume_down") &&
            hold <= 200) {
            const auto current = control(request, {{"op", "status"}});

            if (qinggan11_partial_wheel_capability_matches(
                        current.value("wheel_capability", Json(nullptr)))) {
                auto paired = control(
                        request,
                        {{"op", "wheel_pair"}, {"id", id}, {"owner", owner}, {"hold_ms", hold}},
                        true);
                paired.erase("ok");
                paired.erase("session_id");

                return paired;
            }
        }
#endif
        auto down = control(
                request, {{"op", "wheel"}, {"id", id}, {"phase", "down"}, {"owner", owner}}, true);
        try {
            std::this_thread::sleep_for(std::chrono::milliseconds(hold));
            auto up = control(request,
                              {{"op", "wheel"}, {"id", id}, {"phase", "up"}, {"owner", owner}},
                              true);

            return {{"down", down},
                    {"up", up},
                    {"released", true},
                    {"guest_acknowledged", false},
                    {"visible_effect_verified", false}};
        } catch (const std::exception &failure) {
            try {
                control(request, {{"op", "release_owner"}, {"owner", owner}}, true);
            } catch (const std::exception &) {
                throw VoyemuError("TEST_INPUT_CLEANUP_UNKNOWN",
                                  "wheel release unverified; owner lease remains active");
            }

            if (qinggan11_wheel_provider_profile(profile(request)) &&
                (id == "volume_up" || id == "volume_down") && hold > 200 &&
                std::string_view(failure.what()).starts_with("WHEEL_OWNER:")) {
                throw VoyemuError(
                        "TEST_INPUT_CANCELLED_UNKNOWN",
                        "accepted DOWN; hold released before UP; inspect delivery receipts");
            }

            throw;
        }
    }

    if (op == "input.tap" || op == "input.drag" || op == "input.gesture" ||
        op == "input.key.press") {
        const auto repeat = op == "input.tap" ? tap_repeat(request) : TapRepeat{};

        if (op != "input.tap" && (request.contains("count") || request.contains("interval_ms"))) {
            throw VoyemuError("TEST_ARGUMENT", "repeated dispatch is supported only for input.tap");
        }

        Json input = request;
        input["kind"] = op == "input.key.press" ? "key" : "pointer";
        std::string screen;
        media::GuestDisplay display;

        if (op != "input.key.press") {
            screen = text(request, "screen");

            if (request.contains("screens")) {
                throw VoyemuError("TEST_ARGUMENT", "pointer needs one screen");
            }

            // Every Android screen takes its size from the screens of the machine; the rear
            // screen takes plain taps and gestures in its own pixels.
            if (screen == "cluster" || (!multimedia_screen(screen) &&
                                        (op == "input.drag" || request.contains("selector") ||
                                         request.contains("expected")))) {
                throw VoyemuError("TEST_UNSUPPORTED",
                                  "pointer targets an Android screen; the rear screen takes "
                                  "plain taps and gestures");
            }

            display = android_screen(status_screens(control(request, {{"op", "status"}})), screen);

            input["display_id"] = display.id;
            input["display_width"] = display.width;
            input["display_height"] = display.height;
        }

        for (const auto &[key, value] : request.items()) {
            if (key.starts_with("expected") && key != "expected" && key != "expected_revision" &&
                key != "expected_generation") {
                throw VoyemuError(
                        "TEST_ARGUMENT",
                        "unsupported expected field; use expected boot/source/window/target identity");
            }
        }

        if (request.contains("selector") && op != "input.tap") {
            throw VoyemuError("TEST_ARGUMENT",
                              "selector pointer input currently supports taps only");
        }

        if (request.contains("expected") && op == "input.key.press") {
            throw VoyemuError("TEST_ARGUMENT",
                              "pointer target fencing does not cover keyboard focus");
        }

        if (request.contains("selector") &&
            (request.contains("x") || request.contains("y") || request.contains("points"))) {
            throw VoyemuError("TEST_ARGUMENT",
                              "selector and coordinate input are mutually exclusive");
        }

        if (op == "input.drag" || request.contains("expected") || request.contains("selector")) {
            if (request.value("scope", std::string{}) == "interactive") {
                throw VoyemuError(
                        "TEST_UNSUPPORTED",
                        "interactive windows do not verify full pointer coverage; use coordinate input without target fencing");
            }

            auto read = request;

            if (screen == "primary" && request.value("scope", std::string{}) != "all_attached") {
                read.erase("scope");
            } else {
                read["scope"] = "all_attached";
            }

            read.erase("selector");
            Json observation;
            try {
                observation = tree(read);
            } catch (const std::exception &) {
                throw VoyemuError("TEST_INPUT_UNAVAILABLE",
                                  "fresh pre-dispatch UI observation unavailable");
            }

            if (op == "input.drag") {
                const Json *current = nullptr;

                for (const auto &candidate : observation.at("screens")) {
                    if (candidate.value("screen", std::string{}) == screen) {
                        current = &candidate;
                    }
                }

                const auto screen_status = current ? current->value("status", std::string{})
                                                   : std::string{};
                const bool geometry_ok = current &&
                                         (screen_status == "ok" || screen_status == "partial") &&
                                         current->contains("width") &&
                                         current->contains("height") &&
                                         current->at("width").is_number_integer() &&
                                         current->at("height").is_number_integer() &&
                                         current->at("width").get<std::int64_t>() >= 1 &&
                                         current->at("width").get<std::int64_t>() <= 3840 &&
                                         current->at("height").get<std::int64_t>() >= 1 &&
                                         current->at("height").get<std::int64_t>() <= 2160;

                if (!geometry_ok) {
                    throw VoyemuError("TEST_INPUT_UNAVAILABLE",
                                      "current drag screen geometry unavailable");
                }

                input = voyemu::api::drag_gesture(request, current->at("width").get<int>(),
                                                  current->at("height").get<int>());
                input["kind"] = "pointer";
                input["display_id"] = display.id;
                input["display_width"] = display.width;
                input["display_height"] = display.height;
            }

            if (request.contains("expected") || request.contains("selector")) {
                if (request.contains("selector")) {
                    const auto &selector = request.at("selector");

                    if (!selector.is_object() || !selector.contains("resource_id") ||
                        !selector.at("resource_id").is_string()) {
                        throw VoyemuError("TEST_ARGUMENT", "selector requires resource_id");
                    }

                    for (const auto &[key, value] : selector.items()) {
                        if (key != "resource_id" && key != "package") {
                            throw VoyemuError("TEST_ARGUMENT", "unsupported selector field");
                        }
                    }
                }

                auto fence = request;

                if (op != "input.tap") {
                    if (op == "input.drag") {
                        fence["intent"] = "drag";
                    } else if (fence.contains("intent") && fence.at("intent") != "scroll" &&
                               fence.at("intent") != "drag") {
                        throw VoyemuError("TEST_ARGUMENT", "unsupported gesture intent");
                    }

                    const auto &first = input.at("points").at(0);
                    fence["x"] = first.at("x");
                    fence["y"] = first.at("y");
                    const auto intent = op == "input.gesture" && !fence.contains("intent")
                                                ? InputIntent::Gesture
                                                : InputIntent::Target;
                    (void)fenced_point(fence, observation, intent);
                } else {
                    input["points"] = Json::array({fenced_point(fence, observation)});
                }
            }
        }

        input.erase("expected");
        input.erase("selector");
        input.erase("count");
        input.erase("interval_ms");
        validate_media_input(input);
        auto send = [&] {
            return control(request, {{"op", "media_input"}, {"input", input}}, true, 8000);
        };
        auto result = op == "input.tap" ? repeat_taps(repeat, send,
                                                      [](unsigned ms) {
                                                          std::this_thread::sleep_for(
                                                                  std::chrono::milliseconds(ms));
                                                      })
                                        : send();

        if (request.contains("expected")) {
            result["pre_dispatch_fenced"] = true;
            result["fence_scope"] = "before_first_dispatch";
            result["atomic_ui_input"] = false;
            result["occlusion"] = "known_window_bounds_only";
            result["touchability"] = "unknown";

            if (request.at("expected").at("window").contains("verified_binding")) {
                result["fence_scope"] = "target_identity_geometry_before_first_dispatch";
                result["occlusion"] = "unknown";
                result["input_coverage"] = "unknown";
            }

            if (op == "input.gesture" && !request.contains("intent")) {
                result["target_basis"] = "visible_enabled_geometry";
            }
        }

        if (!screen.empty()) {
            result["screen"] = screen;
            result["sdk_display_id"] = display.id;
            result["display_id"] = nullptr;
        }

        return result;
    }

    if (op == "multimedia.system") {
        const auto timeout = number(request, "timeout_ms", 1000, 0, 60000);

        return transports_.android->system(request, static_cast<int>(timeout)).at("state");
    }

    return unhandled_operation();
}
} // namespace voyemu::api
