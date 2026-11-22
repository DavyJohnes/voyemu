#include "error.hpp"
#include "profile_catalog.hpp"
#include "session_internal.hpp"

#include <sstream>

namespace voyemu {
using namespace session_detail;

bool observe_android_boot(std::string &retained, const std::string &observed) {
    if (!android_boot_id_valid(observed)) {
        throw VoyemuError("ANDROID_ROOT", "valid Linux boot identity required");
    }

    const bool changed = !retained.empty() && retained != observed;
    retained = observed;

    return changed;
}

void bootstrap_owned_adbd(
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<bool()> &owned, const std::function<void(int)> &identity,
        int timeout_ms) {
    if (timeout_ms < 1 || timeout_ms > 15000) {
        throw VoyemuError("ANDROID_ROOT", "invalid deadline");
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string last_unavailable;
    auto remaining = [&]() {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          deadline - std::chrono::steady_clock::now())
                          .count();

        if (ms <= 0) {
            throw std::runtime_error(last_unavailable.empty()
                                             ? std::string{"ANDROID_ROOT: deadline exceeded"}
                                             : last_unavailable + "; deadline exceeded");
        }

        return static_cast<int>(ms);
    };
    auto require_owned = [&]() {
        if (!owned()) {
            throw VoyemuError("ANDROID_IDENTITY_MISMATCH",
                              "root bootstrap requires retained live SDK ownership");
        }
    };
    auto pause = [&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(25, remaining())));
    };
    auto fence = [&]() {
        for (;;) {
            require_owned();
            try {
                identity(remaining());
                require_owned();

                return;
            } catch (const std::runtime_error &error) {
                // Only unavailable observations are retryable. A successfully read different
                // token, AVD or boot remains a hard failure of this incarnation.
                if (std::string(error.what()).rfind("ANDROID_IDENTITY_NOT_READY:", 0) != 0) {
                    throw;
                }

                last_unavailable = error.what();
                require_owned();
                pause();
            }
        }
    };
    auto call = [&](std::vector<std::string> args) {
        const bool mutation = args == std::vector<std::string>{"root"};

        for (;;) {
            require_owned();
            auto result = run(args, remaining());
            require_owned();

            if (!result.exit_code && !result.timed_out) {
                return result.output;
            }

            // Never replay root after an ambiguous submission; a new guest boot is
            // required to rearm that privilege attempt.
            if (mutation) {
                throw VoyemuError("ANDROID_ROOT",
                                  "SDK root submission failed; outcome not confirmed");
            }

            last_unavailable = "ANDROID_IDENTITY_NOT_READY: SDK bootstrap " +
                               std::string(args == std::vector<std::string>{"wait-for-device"}
                                                   ? "wait"
                                           : args == std::vector<std::string>{"shell", "id", "-u"}
                                                   ? "UID read"
                                                   : "Enforcing read") +
                               " unavailable";
            pause();
        }
    };
    auto trim = [](std::string value) {
        while (!value.empty() && (value.back() == 10 || value.back() == 13)) {
            value.pop_back();
        }

        return value;
    };
    fence();
    const auto uid = trim(call({"shell", "id", "-u"}));

    if (uid != "0") {
        if (uid != "2000") {
            throw VoyemuError("ANDROID_ROOT", "unexpected SDK UID");
        }

        fence();
        call({"root"});
        call({"wait-for-device"});
        fence();

        if (trim(call({"shell", "id", "-u"})) != "0") {
            throw VoyemuError("ANDROID_ROOT", "SDK root not established");
        }
    }

    if (trim(call({"shell", "getenforce"})) != "Enforcing") {
        throw VoyemuError("ANDROID_ROOT", "Enforcing required");
    }

    fence();
}

static void retire_android_network_failure(Json &android, const char *status) {
    if (!android.contains("interguest_network_failure")) {
        return;
    }

    auto previous = android.at("interguest_network_failure");
    previous["status"] = status;
    android["interguest_network_last_failure"] = std::move(previous);
    android.erase("interguest_network_failure");
}

void Session::prepare_android_navigation_bus() {
    if (!android_spec_.contains("qinggan09_owned_packages") ||
        !android_spec_.at("qinggan09_owned_packages").contains("navigation_bus")) {
        return;
    }

    assert_android_identity();

    if (!android_transport_ready_ || android_transport_boot_.empty()) {
        throw VoyemuError("ANDROID_NAVIGATION_BUS_STARTUP", "owned transport required");
    }

    const auto boot = android_transport_boot_;
    auto fence = [&] {
        assert_android_identity();
        const auto observed = adb({"shell", "cat", "/proc/sys/kernel/random/boot_id"}, 1500);
        auto value = observed.output;
        const auto end = value.find_first_of("\r\n");

        if (end != std::string::npos) {
            value.resize(end);
        }

        if (observed.exit_code || observed.timed_out || value != boot) {
            throw VoyemuError("ANDROID_NAVIGATION_BUS_STALE", "verified Android boot required");
        }
    };
    fence();
    const auto reply = qinggan09_canbus_startup(
            android_navigation_bus_startup_, boot, android_generation_,
            [&] {
                deliveries_["android"]["qinggan09_navigation_bus_package"] =
                        ensure_qinggan09_navigation_bus_package(
                                android_spec_, android_resume_ ? android_boot_nonce_ : session_id_,
                                boot,
                                [&](const auto &args, int timeout) {
                                    return adb(args, timeout);
                                },
                                fence);
            },
            [&] {
                fence();
                const auto result = adb({"shell", "am", "startservice", "-n",
                                         "com.qinggan.QGBus/com.voyemu.oem.NavigationBusService"},
                                        5000);
                fence();

                if (result.timed_out || result.exit_code ||
                    result.output.find("Error") != std::string::npos) {
                    throw VoyemuError("ANDROID_NAVIGATION_BUS_STARTUP",
                                      "service dispatch unconfirmed");
                }
            },
            [&] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

                do {
                    fence();
                    const auto result =
                            adb({"shell", "dumpsys", "activity", "services",
                                 "com.qinggan.QGBus/com.voyemu.oem.NavigationBusService"},
                                1500);
                    fence();

                    if (!result.exit_code && !result.timed_out) {
                        auto observed = qinggan09_navigation_bus_process(result.output, boot);

                        if (qinggan09_navigation_bus_ready(observed, boot)) {
                            return observed;
                        }
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } while (std::chrono::steady_clock::now() < deadline);

                throw VoyemuError("ANDROID_NAVIGATION_BUS_STARTUP",
                                  "service process readiness deadline");
            },
            qinggan09_navigation_bus_ready, "ANDROID_NAVIGATION_BUS");

    if (!reply.is_null()) {
        deliveries_["android"]["navigation_bus_startup"] = reply;
    }
}

void Session::prepare_android_canbus() {
    if (android_spec_.contains("qinggan09_owned_packages")) {
        qinggan09_owned_packages_descriptor(android_spec_.at("qinggan09_owned_packages"), false);
    }

    prepare_android_navigation_bus();

    if (!android_spec_.contains("qinggan09_owned_packages") ||
        !android_spec_.at("qinggan09_owned_packages").contains("canbus")) {
        return;
    }

    assert_android_identity();

    if (!android_transport_ready_ || android_transport_boot_.empty()) {
        throw VoyemuError("ANDROID_CANBUS_STARTUP", "owned transport required");
    }

    const auto boot = android_transport_boot_;
    auto fence = [&] {
        assert_android_identity();
        const auto observed = adb({"shell", "cat", "/proc/sys/kernel/random/boot_id"}, 1500);
        auto value = observed.output;
        const auto end = value.find_first_of("\r\n");

        if (end != std::string::npos) {
            value.resize(end);
        }

        if (observed.exit_code || observed.timed_out || value != boot) {
            throw VoyemuError("ANDROID_CANBUS_STALE", "verified Android boot required");
        }
    };
    fence();
    const auto reply = qinggan09_canbus_startup(
            android_canbus_startup_, boot, android_generation_,
            [&] {
                deliveries_["android"]["qinggan09_canbus_package"] =
                        ensure_qinggan09_canbus_package(
                                android_spec_, android_resume_ ? android_boot_nonce_ : session_id_,
                                boot,
                                [&](const auto &args, int timeout) {
                                    return adb(args, timeout);
                                },
                                fence);
            },
            [&] {
                fence();
                const auto result = adb({"shell", "am", "startservice", "-n",
                                         qinggan09_canbus_service_component(android_spec_)},
                                        5000);
                fence();

                if (result.timed_out || result.exit_code ||
                    result.output.find("Error") != std::string::npos) {
                    throw VoyemuError("ANDROID_CANBUS_STARTUP", "service dispatch unconfirmed");
                }
            },
            [&] {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);

                do {
                    fence();
                    const auto service = adb({"shell", "service", "check", "qg.canbus"}, 1500);
                    const auto result = adb(
                            {"shell",
                             "content call --uri content://com.voyemu.output.vehicle --method observe --arg '{\"schema\":1,\"limit\":1}'"},
                            2000);
                    fence();

                    if (!service.exit_code && !service.timed_out &&
                        service.output.find("Service qg.canbus: found") != std::string::npos &&
                        !result.exit_code && !result.timed_out) {
                        const auto observed = parse_android_provider_reply(result.output);

                        if (qinggan09_canbus_ready(observed, boot)) {
                            return observed;
                        }
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                } while (std::chrono::steady_clock::now() < deadline);

                throw VoyemuError("ANDROID_CANBUS_STARTUP",
                                  "registered service readiness deadline");
            });

    if (!reply.is_null()) {
        deliveries_["android"]["canbus_startup"] = reply;
    }
}

void Session::prepare_android_audio(bool snapshot) {
    prepare_android_canbus();

    if (!android_audio_startup_required(android_spec_)) {
        return;
    }

    assert_android_identity();

    if (!android_transport_ready_ || android_transport_boot_.empty()) {
        throw VoyemuError("ANDROID_AUDIO_STARTUP", "owned transport required");
    }

    const auto previous = deliveries_["android"].value("audio_startup", Json::object());
    const bool completed = previous.value("launch_session", std::string{}) == session_id_ &&
                           previous.value("boot_id", std::string{}) == android_transport_boot_ &&
                           previous.value("android_generation", std::uint64_t{}) ==
                                   android_generation_ &&
                           previous.value("completed", false);

    if (!completed && !snapshot) {
        if (!android_spec_.contains("qinggan09_owned_packages")) {
            throw VoyemuError("ANDROID_AUDIO_STARTUP", "source-defined packages required");
        }

        deliveries_["android"]["qinggan09_owned_packages"] = ensure_qinggan09_owned_packages(
                android_spec_, android_resume_ ? android_boot_nonce_ : session_id_,
                android_transport_boot_,
                [&](const auto &args, int timeout) {
                    return adb(args, timeout);
                },
                [&] {
                    assert_android_identity();
                });
    }

    const std::string method = snapshot    ? "prepare_snapshot"
                               : completed ? "status"
                                           : "reset_launch";
    const auto result =
            adb({"shell", "content", "call", "--uri", "content://com.voyemu.output.audio.startup",
                 "--method", method, "--extra", "launch_session:s:" + session_id_, "--extra",
                 "boot_id:s:" + android_transport_boot_, "--extra",
                 "android_generation:l:" + std::to_string(android_generation_)},
                5000);

    if (result.timed_out || result.exit_code) {
        throw VoyemuError("ANDROID_AUDIO_STARTUP", "provider unavailable");
    }

    const auto reply = parse_android_provider_reply(result.output);
    validate_android_audio_startup_reply(reply, session_id_, android_transport_boot_,
                                         android_generation_, snapshot || !completed);
    assert_android_identity();
    deliveries_["android"]["audio_startup"] = reply;

    if (snapshot) {
        const auto synced = adb({"shell", "sync"}, 15000);

        if (synced.timed_out || synced.exit_code) {
            throw VoyemuError("ANDROID_AUDIO_STARTUP", "presave sync failed");
        }
    }
}

void Session::bootstrap_android_transport() {
    const auto port = android_spec_.at("port").get<unsigned>();
    auto owned = [&]() {
        return android_.alive() && android_.owns_tcp_listener(port) &&
               android_.owns_tcp_listener(port + 1);
    };

    // Not-ready SDK startup is retried; ownership and identity precede the one privileged attempt.
    if (!owned()) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY", "SDK ownership not ready");
    }

    assert_android_identity();
    auto boot = adb({"shell", "cat", "/proc/sys/kernel/random/boot_id"}, 1500);
    auto boot_id = boot.output;

    while (!boot_id.empty() && (boot_id.back() == 10 || boot_id.back() == 13)) {
        boot_id.pop_back();
    }

    const bool ownership_checked = !boot.exit_code && !boot.timed_out;
    const bool boot_owned = ownership_checked && owned();
    try {
        logging::write(logging::Level::debug, "android", "bootstrap.boot_observation",
                       {{"exit_code", boot.exit_code},
                        {"timed_out", boot.timed_out},
                        {"output_bytes", boot.output.size()},
                        {"ownership_checked", ownership_checked},
                        {"owned", boot_owned}});
    } catch (...) { /* Ignored: diagnostics must preserve bootstrap behavior. */
    }

    if (boot.exit_code || boot.timed_out || !boot_owned) {
        throw VoyemuError("ANDROID_IDENTITY_NOT_READY", "guest boot observation unavailable");
    }

    assert_android_identity();
    const auto previous_boot = android_transport_boot_;
    auto verified_boot = previous_boot;

    if (observe_android_boot(verified_boot, boot_id)) {
        // Installer-triggered guest reboots bypass reboot_android. The verified
        // new boot invalidates both the observer and its persisted old lease.
        if (ui_observer_) {
            ui_observer_->retire_for_boot(previous_boot, boot_id);
            ui_observer_.reset();
        }

        clear_ui_observer_metadata(directory_, session_id_);

#ifndef _WIN32
        advance_android_launch_generation(directory_, session_id_, android_.pid(),
                                          android_generation_);
#endif
        reset_android_identity_for_new_boot();
        ++android_generation_;
        android_transport_boot_ = std::move(verified_boot);
        android_transport_ready_ = false;
        android_transport_attempted_ = false;
        android_clock_observed_ = false;
        android_clock_correction_attempted_ = false;
        android_clock_exit_code_ = -1;
        android_clock_arrival_ = {};
        wheel_provisioned_ = false;
        android_revision_ = 0;
        android_boot_completed_ = false;
        android_displays_ready_ = false;
        android_display_activated_ = false;
        android_canvas_refreshed_ = false;
        primary_home_ready_ = false;
        clear_android_home_delivery(deliveries_["android"]);
        clear_android_camera_delivery(deliveries_["android"]);
        theme_.reset();
        oem_language_.reset();
        next_oem_settings_ = {};
        next_display_observation_ = {};
        next_display_activation_ = {};
        test_ui_connection_ = nullptr;
        test_ui_subscription_ = nullptr;
        test_ui_sample_ = {};
        test_ui_sampled_ms_ = 0;
        test_can_cursor_ = nullptr;
        test_can_generation_ = 0;
        media_connection_written_ = false;
        std::filesystem::remove(directory_ / "media-connection.json");
        deliveries_["android"].erase("transport");
        deliveries_["android"].erase("transport_failure");
        deliveries_["android"]["status"] = "booting";
        deliveries_["android"].erase("interguest_network");
        retire_android_network_failure(deliveries_["android"], "superseded_by_boot");
        deliveries_["android"]["generation"] = android_generation_;
        deliveries_["android"]["external_reboot"] = {
                {"boot_id", boot_id}, {"source", "owned Linux boot identity change"}};
    } else {
        android_transport_boot_ = std::move(verified_boot);
    }

    if (android_transport_ready_) {
        return;
    }

    if (android_transport_attempted_) {
        const auto failure = deliveries_["android"].value("transport_failure", Json::object());
        throw std::runtime_error(failure.value(
                "reason",
                std::string{
                        "ANDROID_ROOT: startup bootstrap previously failed; restart guest explicitly"}));
    }

    android_transport_attempted_ = true;
    try {
        bootstrap_owned_adbd(
                [&](const auto &args, int timeout) {
                    return adb(args, timeout);
                },
                owned,
                [&](int timeout) {
                    const auto deadline = std::chrono::steady_clock::now() +
                                          std::chrono::milliseconds(timeout);
                    auto token = adb({"shell", "getprop", "qemu.voyemu.token"},
                                     std::min(timeout, 1500));
                    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                   deadline - std::chrono::steady_clock::now())
                                                   .count();

                    if (remaining <= 0) {
                        throw VoyemuError("ANDROID_ROOT", "identity deadline exceeded");
                    }

                    auto name = adb({"emu", "avd", "name"},
                                    static_cast<int>(std::min<std::int64_t>(remaining, 1500)));
                    auto line = [](std::string value) {
                        auto end = value.find_first_of(std::string{char(13), char(10)});

                        return value.substr(0, end);
                    };

                    if (token.exit_code || token.timed_out || name.exit_code || name.timed_out) {
                        throw VoyemuError("ANDROID_IDENTITY_NOT_READY",
                                          "SDK bootstrap identity observation unavailable");
                    }

                    if (line(name.output) != android_spec_.at("avd_name").get<std::string>() ||
                        (line(token.output) != session_id_ &&
                         (!android_resume_ || line(token.output) != android_boot_nonce_))) {
                        throw VoyemuError("ANDROID_IDENTITY_MISMATCH",
                                          "SDK bootstrap target changed");
                    }

                    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              deadline - std::chrono::steady_clock::now())
                                              .count();

                    if (left <= 0) {
                        throw VoyemuError("ANDROID_ROOT", "boot fence deadline exceeded");
                    }

                    auto incarnation = adb({"shell", "cat", "/proc/sys/kernel/random/boot_id"},
                                           static_cast<int>(std::min<std::int64_t>(left, 1500)));

                    if (incarnation.exit_code || incarnation.timed_out) {
                        throw VoyemuError("ANDROID_IDENTITY_NOT_READY",
                                          "SDK bootstrap boot observation unavailable");
                    }

                    if (line(incarnation.output) != android_transport_boot_) {
                        throw VoyemuError("ANDROID_IDENTITY_MISMATCH",
                                          "guest rebooted during SDK bootstrap");
                    }
                },
                10000);
    } catch (const std::runtime_error &error) {
        // Preserve the first bounded, locally defined diagnostic for this boot;
        // later readiness polls must not replace it with a generic sticky error.
        const std::string reason = error.what();
        const bool fixed = reason.rfind("ANDROID_ROOT:", 0) == 0 ||
                           reason.rfind("ANDROID_IDENTITY_", 0) == 0;
        deliveries_["android"]["transport_failure"] = {
                {"stage", "startup_bootstrap"},
                {"reason", fixed ? reason.substr(0, 256)
                                 : std::string{"ANDROID_ROOT: startup bootstrap failed"}},
                {"root_retry_allowed", false}};
        throw;
    }

    android_transport_ready_ = true;
    deliveries_["android"]["transport"] = {{"status", "root_ready"},
                                           {"uid", 0},
                                           {"selinux", "Enforcing"},
                                           {"source", "owned startup SDK bootstrap"}};
}

void configure_owned_android_network(
        const std::string &boot, const std::string &dex, const std::string &pin,
        std::string_view payload, const Json &inputs, Json &android,
        std::vector<AndroidNetworkArtifact> &pending,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run,
        const std::function<std::string()> &nonce) {
    if (!android_boot_id_valid(boot) || !std::regex_match(pin, std::regex("[0-9a-f]{64}")) ||
        payload.empty() || payload.size() > 8 * 1024 * 1024 || sha256_bytes(payload) != pin) {
        throw VoyemuError("INTERGUEST_ANDROID", "invalid setup identity");
    }

    if (android.contains("interguest_network_failure") &&
        android.at("interguest_network_failure").value("boot_id", std::string{}) != boot) {
        retire_android_network_failure(android, "superseded_by_boot");
    }

    const auto previous = android.value("interguest_network", Json::object());

    if (previous.value("boot_id", std::string{}) != boot ||
        previous.value("inputs", Json()) != inputs) {
        android.erase("interguest_network");
    }

    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    auto diagnostic = [](const ProcessResult &result) {
        return Json{{"exit_code", result.exit_code},
                    {"timed_out", result.timed_out},
                    {"output", result.output.substr(0, 16384)}};
    };
    auto call = [&](const std::vector<std::string> &args) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       end - std::chrono::steady_clock::now())
                                       .count();

        if (remaining < 1) {
            throw VoyemuError("INTERGUEST_TIMEOUT", "startup network deadline elapsed");
        }

        const auto result = run(args, static_cast<int>(remaining));

        if (result.timed_out || result.exit_code) {
            if (!android.contains("interguest_network_failure")) {
                android["interguest_network_failure"] = diagnostic(result);
                android["interguest_network_failure"]["boot_id"] = boot;
            }

            throw VoyemuError("INTERGUEST_ANDROID", "owned startup command failed");
        }

        return result.output;
    };
    auto fence = [&]() {
        const auto observed = call({"shell", "cat", "/proc/sys/kernel/random/boot_id"});

        if (observed.substr(0, observed.find_first_of("\r\n")) != boot) {
            android.erase("interguest_network");
            throw VoyemuError("ANDROID_IDENTITY_MISMATCH", "guest rebooted during network setup");
        }
    };
    auto cleanup = [&]() {
        while (!pending.empty()) {
            const auto &owned = pending.front();

            if (!std::regex_match(owned.path,
                                  std::regex("/data/local/tmp/voyemu-network-[0-9a-f]{32}")) ||
                !std::regex_match(owned.inode, std::regex("[1-9][0-9]*")) ||
                !std::regex_match(owned.sha256, std::regex("[0-9a-f]{64}"))) {
                throw VoyemuError("INTERGUEST_CLEANUP", "invalid retained ownership");
            }

            // A retained exclusive mkdir is authority only for this exact inode.
            // ART may add oat files; no symlink or foreign-owned descendant is admitted.
            const auto guard =
                    "d=" + android_shell_quote(owned.path) +
                    "; if [ ! -e \"$d\" ] && [ ! -L \"$d\" ]; then echo ABSENT; exit 0; fi; "
                    "[ ! -L \"$d\" ] && [ -d \"$d\" ] && [ \"$(stat -c '%u:%g:%a:%i' \"$d\")\" = '0:0:700:" +
                    owned.inode +
                    "' ] || exit 71; "
                    "links=$(find \"$d\" -type l -print) && owners=$(find \"$d\" ! -user root -print) && [ -z \"$links$owners\" ] || exit 72; ";
            std::string expected_hash = owned.sha256, expected_metadata = "0:0:600",
                        payload_guard = "[ -f \"$d/classes.dex\" ] || exit 73; ";

            if (!owned.execution_started) {
                // An interrupted push is removable only when it is an exact
                // prefix of the immutable, pinned bytes retained before push.
                const auto observed = run(
                        {"shell",
                         guard + "if [ ! -e \"$d/classes.dex\" ]; then echo EMPTY; else [ -f \"$d/classes.dex\" ] || exit 73; stat -c '%u:%g:%a:%s:%i' \"$d/classes.dex\" && sha256sum \"$d/classes.dex\"; fi"},
                        1500);
                std::smatch match;

                if (!observed.exit_code && !observed.timed_out && observed.output == "ABSENT\n") {
                    pending.erase(pending.begin());
                    continue;
                }

                if (!observed.exit_code && !observed.timed_out && observed.output == "EMPTY\n") {
                    payload_guard =
                            "[ ! -e \"$d/classes.dex\" ] && [ ! -L \"$d/classes.dex\" ] || exit 73; ";
                } else if (
                        !observed.exit_code && !observed.timed_out &&
                        std::regex_match(
                                observed.output, match,
                                std::regex(
                                        "(0:0:(?:600|644)):([0-9]{1,8}):([1-9][0-9]*)\\n([0-9a-f]{64})  [^\\r\\n]+\\n")) &&
                        std::stoull(match[2].str()) <= owned.source_bytes.size() &&
                        !owned.source_bytes.empty() &&
                        owned.source_bytes.size() <= 8 * 1024 * 1024 &&
                        sha256_bytes(owned.source_bytes) == owned.sha256 &&
                        sha256_bytes(std::string_view(owned.source_bytes)
                                             .substr(0, std::stoull(match[2].str()))) ==
                                match[4].str()) {
                    expected_hash = match[4].str();
                    expected_metadata = match[1].str() + ":" + match[2].str() + ":" +
                                        match[3].str();
                    payload_guard =
                            "[ -f \"$d/classes.dex\" ] && [ \"$(stat -c '%u:%g:%a:%s:%i' \"$d/classes.dex\")\" = '" +
                            expected_metadata + "' ] || exit 73; ";
                } else {
                    if (!android.contains("interguest_network_cleanup")) {
                        android["interguest_network_cleanup"] = diagnostic(observed);
                        android["interguest_network_cleanup"]["path"] = owned.path;
                    }

                    android["interguest_network_cleanup"]["status"] = "pending";
                    throw VoyemuError("INTERGUEST_CLEANUP", "transfer identity not proven");
                }
            }

            const auto script =
                    guard + payload_guard +
                    "if [ -e \"$d/classes.dex\" ]; then [ -f \"$d/classes.dex\" ] && " +
                    (owned.execution_started
                             ? "[ \"$(stat -c '%u:%g:%a' \"$d/classes.dex\")\" = '0:0:600' ] && "
                             : std::string{}) +
                    "[ \"$(sha256sum \"$d/classes.dex\" | cut -d ' ' -f 1)\" = '" + expected_hash +
                    "' ] || exit 73; fi; rm -rf \"$d\"";
            const auto removed = run({"shell", script}, 1500);

            if (removed.timed_out || removed.exit_code) {
                if (!android.contains("interguest_network_cleanup")) {
                    android["interguest_network_cleanup"] = diagnostic(removed);
                    android["interguest_network_cleanup"]["path"] = owned.path;
                }

                android["interguest_network_cleanup"]["status"] = "pending";
                throw VoyemuError("INTERGUEST_CLEANUP", "owned startup DEX/ART removal failed");
            }

            pending.erase(pending.begin());
        }

        if (android.contains("interguest_network_cleanup")) {
            android["interguest_network_cleanup"]["status"] = "recovered";
        }
    };
    // Pending ownership outlives failed attempts and boot changes. Do not redeploy
    // a configured helper merely to retry its cleanup.
    fence();
    cleanup();

    if (android.contains("interguest_network") &&
        previous.value("status", std::string{}) == "configured") {
        fence();
        retire_android_network_failure(android, "recovered");

        return;
    }

    const auto attempt = nonce();

    if (!std::regex_match(attempt, std::regex("[0-9a-f]{32}"))) {
        throw VoyemuError("INTERGUEST_ANDROID", "invalid setup nonce");
    }

    const std::string remote = "/data/local/tmp/voyemu-network-" + attempt,
                      leaf = remote + "/classes.dex";
    Json result;
    try {
        fence();
        const auto identity =
                call({"shell", "umask 077; mkdir " + android_shell_quote(remote) +
                                       " && stat -c '%u:%g:%a:%i' " + android_shell_quote(remote)});
        std::smatch match;

        if (!std::regex_match(identity, match, std::regex("0:0:700:([1-9][0-9]*)[\\r\\n]*"))) {
            throw VoyemuError("INTERGUEST_ANDROID", "created directory identity unavailable");
        }

        pending.push_back({remote, match[1].str(), pin, std::string(payload), false});
        call({"push", dex, leaf});
        call({"shell", "chmod", "600", leaf});

        if (!call({"shell", "sha256sum", leaf}).starts_with(pin + " ")) {
            throw VoyemuError("LAUNCH_PIN", "guest startup network DEX mismatch");
        }

        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       end - std::chrono::steady_clock::now())
                                       .count();

        if (remaining < 2000) {
            throw VoyemuError("INTERGUEST_TIMEOUT", "no bounded guest execution budget");
        }

        const auto guest_seconds = std::min<std::int64_t>(7, (remaining - 1000) / 1000);
        pending.back().execution_started = true;
        const auto output =
                call({"shell",
                      "CLASSPATH=" + android_shell_quote(leaf) + " /system/bin/toybox timeout " +
                              std::to_string(guest_seconds) +
                              " /system/bin/app_process /system/bin com.voyemu.api.NetworkSetup"});
        constexpr std::string_view marker = "VOYEMU_NETWORK_V1 ";
        const auto start = output.find(marker);

        if (start == std::string::npos ||
            output.find(marker, start + marker.size()) != std::string::npos) {
            throw VoyemuError("INTERGUEST_ANDROID", "unique startup report required");
        }

        const auto begin = start + marker.size(), finish = output.find_first_of("\r\n", begin);
        result = Json::parse(
                output.substr(begin, finish == std::string::npos ? finish : finish - begin));

        if (!result.is_object() || result.size() != 7 || result.value("schema", 0) != 1 ||
            !result.value("ok", false) ||
            result.value("address", std::string{}) != "172.16.100.21" ||
            result.value("prefix", 0) != 24 || !result.value("local_route_requested", false) ||
            !result.at("interface").is_string() ||
            !std::regex_match(result.at("interface").get<std::string>(),
                              std::regex("[A-Za-z0-9_.:-]{1,15}")) ||
            !result.at("network_ids").is_array() || result.at("network_ids").size() > 64) {
            throw VoyemuError("INTERGUEST_ANDROID", "fixed startup report mismatch");
        }

        for (const auto &id : result.at("network_ids")) {
            if (!id.is_number_integer() || id.get<std::int64_t>() < 1 ||
                id.get<std::int64_t>() > 65535) {
                throw VoyemuError("INTERGUEST_ANDROID", "invalid configured network identity");
            }
        }

        fence();
        cleanup();
        fence();
    } catch (const std::exception &error) {
        if (!android.contains("interguest_network_failure")) {
            android["interguest_network_failure"] = {{"reason", error.what()}, {"boot_id", boot}};
        } else if (!android["interguest_network_failure"].contains("reason")) {
            android["interguest_network_failure"]["reason"] = error.what();
        }

        const auto failure = std::current_exception();
        try {
            fence();
            cleanup();
        } catch (
                ...) { /* Ignored: the cleanup receipt is separate; preserve the primary failure. */
        }

        std::rethrow_exception(failure);
    }

    retire_android_network_failure(android, "recovered");
    result["status"] = "configured";
    result["boot_id"] = boot;
    result["inputs"] = inputs;
    result["transport"] = "system_server Binder/netd; Enforcing";
    android["interguest_network"] = std::move(result);
}

Json verify_boot_local_android_network(
        const std::string &boot,
        const std::function<ProcessResult(const std::vector<std::string> &, int)> &run) {
    if (!android_boot_id_valid(boot)) {
        throw VoyemuError("INTERGUEST_ANDROID", "valid boot identity required");
    }

    auto read = [&](std::vector<std::string> args) {
        const auto result = run(args, 1500);

        if (result.exit_code || result.timed_out || result.output.size() > 64 * 1024) {
            std::string command;

            for (const auto &argument : args) {
                if (!command.empty()) {
                    command += ' ';
                }

                command += argument;
            }

            auto details = result.output.substr(0, 256);

            for (char &c : details) {
                if (c < 32 || c >= 127) {
                    c = ' ';
                }
            }

            throw VoyemuError("INTERGUEST_ANDROID",
                              "boot-local network readback failed: " + command +
                                      " (exit=" + std::to_string(result.exit_code) +
                                      ", timed_out=" + (result.timed_out ? "true" : "false") +
                                      "): " + details);
        }

        return result.output;
    };
    auto fence = [&] {
        const auto actual = read({"shell", "cat", "/proc/sys/kernel/random/boot_id"});

        if (actual.substr(0, actual.find_first_of("\r\n")) != boot) {
            throw VoyemuError("ANDROID_IDENTITY_MISMATCH",
                              "guest rebooted during network readback");
        }
    };
    auto rows = [](const std::string &output) {
        std::istringstream lines(output);
        std::vector<std::vector<std::string>> result;

        for (std::string line; std::getline(lines, line);) {
            std::istringstream words(line);
            std::vector<std::string> row;

            for (std::string word; words >> word;) {
                row.push_back(std::move(word));
            }

            if (!row.empty()) {
                result.push_back(std::move(row));
            }
        }

        return result;
    };
    auto has_field = [](const auto &row, std::string_view key, std::string_view value) {
        for (std::size_t i = 0; i + 1 < row.size(); ++i) {
            if (row[i] == key && row[i + 1] == value) {
                return true;
            }
        }

        return false;
    };

    fence();
    std::string interface_name;

    const auto hardware = read(
            {"shell",
             R"SH(for file in /sys/class/net/*/address; do interface=${file%/address}; interface=${interface##*/}; IFS= read -r mac < "$file" || continue; printf '%s %s\n' "$interface" "$mac"; done)SH"});

    for (const auto &link : rows(hardware)) {
        if (link.size() != 2 || link[1] != "52:54:00:76:6f:21") {
            continue;
        }

        if (!interface_name.empty() ||
            !std::regex_match(link[0], std::regex("[A-Za-z0-9_.-]{1,15}"))) {
            throw VoyemuError("INTERGUEST_ANDROID", "ambiguous boot-local Ethernet interface");
        }

        interface_name = link[0];
    }

    if (interface_name.empty()) {
        throw VoyemuError("INTERGUEST_ANDROID", "boot-local Ethernet interface absent");
    }

    const auto addresses =
            rows(read({"shell", "ip", "-o", "-4", "addr", "show", "dev", interface_name}));

    if (std::ranges::none_of(addresses, [&](const auto &row) {
            return row.size() >= 4 && row[1] == interface_name && row[2] == "inet" &&
                   row[3] == "172.16.100.21/24";
        })) {
        throw VoyemuError("INTERGUEST_ANDROID", "boot-local Ethernet address not configured");
    }

    for (auto table : {"98", "99"}) {
        const auto routes = rows(read({"shell", "ip", "-4", "route", "show", "table", table}));

        if (std::ranges::none_of(routes, [&](const auto &row) {
                return row.front() == "172.16.100.0/24" && has_field(row, "dev", interface_name) &&
                       std::ranges::find(row, "via") == row.end();
            })) {
            throw VoyemuError("INTERGUEST_ANDROID",
                              "boot-local Ethernet route not installed in table " +
                                      std::string(table));
        }
    }

    const auto routes = rows(read({"shell", "ip", "-4", "route", "show", "table", "all"}));

    if (std::ranges::any_of(routes, [&](const auto &row) {
            return row.front() == "default" && has_field(row, "dev", interface_name);
        })) {
        throw VoyemuError("INTERGUEST_ANDROID", "isolated Ethernet acquired a default route");
    }

    fence();

    return {{"boot_id", boot},
            {"source", "system-server-route-readback"},
            {"interface", interface_name},
            {"address", "172.16.100.21"},
            {"prefix", 24},
            {"legacy_route_tables", Json::array({98, 99})}};
}

void Session::configure_android_interguest_network() {
    if (!android_spec_.contains("interguest_network")) {
        return;
    }

    validate_qnx_interguest_contract(android_spec_.at("interguest_network"));

    if (!android_transport_ready_) {
        throw VoyemuError("INTERGUEST_ANDROID", "root transport not ready");
    }

    if (android_spec_.value("api", 0) == 30 &&
        record_declares(android_spec_, ProfilePlatform::qinggan11, ProfileBoard::sa8155)) {
        auto &delivery = deliveries_["android"];
        const auto boot = android_transport_boot_;
        const auto previous = delivery.value("interguest_network", Json::object());

        if (previous.value("boot_id", std::string{}) == boot &&
            previous.value("source", std::string{}) == "system-server-route-readback") {
            return;
        }

        try {
            auto proof = verify_boot_local_android_network(boot,
                                                           [&](const auto &args, int timeout) {
                                                               return adb(args, timeout);
                                                           });
            delivery["interguest_network"] = std::move(proof);
            delivery.erase("interguest_network_boot_readback_failure");
            retire_android_network_failure(delivery, "recovered");

            return;
        } catch (const VoyemuError &error) {
            if (error.code() == "ANDROID_IDENTITY_MISMATCH") {
                delivery.erase("interguest_network");
                throw;
            }

            delivery["interguest_network_boot_readback_failure"] = {{"boot_id", boot},
                                                                    {"reason", error.what()}};
        }
    }

    const auto definition = android_spec_.value("ui_observer", Json::object());

    if (!definition.contains("dex") || !definition.contains("sha256")) {
        throw VoyemuError("INTERGUEST_ANDROID", "prepared startup DEX required");
    }

    const auto dex = std::filesystem::path(definition.at("dex").get<std::string>());
    const auto pin = definition.at("sha256").get<std::string>();
    const Json inputs{{"dex", definition}, {"network", android_spec_.at("interguest_network")}};
    const auto previous = deliveries_["android"].value("interguest_network", Json::object());

    if (previous.value("boot_id", std::string{}) != android_transport_boot_ ||
        previous.value("inputs", Json()) != inputs) {
        deliveries_["android"].erase("interguest_network");
    }

    if (!dex.is_absolute() || std::filesystem::is_symlink(dex) ||
        !std::filesystem::is_regular_file(dex) ||
        std::filesystem::file_size(dex) > 8 * 1024 * 1024 || pin.size() != 64 ||
        sha256_file(dex) != pin) {
        throw VoyemuError("LAUNCH_PIN", "startup network DEX mismatch");
    }

    std::ifstream source(dex, std::ios::binary);
    const std::string payload{std::istreambuf_iterator<char>(source), {}};
    try {
        configure_owned_android_network(
                android_transport_boot_, dex.string(), pin, payload, inputs, deliveries_["android"],
                android_network_artifacts_,
                [&](const auto &args, int timeout) {
                    return adb(args, timeout);
                },
                [] {
                    return random_token();
                });
    } catch (...) {
        const auto failure = std::current_exception();
        try {
            if (deliveries_["android"].contains("interguest_network_failure")) {
                auto failure_record = deliveries_["android"]["interguest_network_failure"];

                if (deliveries_["android"].contains("interguest_network_boot_readback_failure")) {
                    failure_record["boot_local_readback"] =
                            deliveries_["android"].at("interguest_network_boot_readback_failure");
                }

                write_private_json(directory_ / "interguest-network-command-error.json",
                                   failure_record);
            }
        } catch (...) { /* Ignored: diagnostics cannot replace the setup error. */
        }

        std::rethrow_exception(failure);
    }
}

Json run_android_startup_stages(const std::function<void()> &guard,
                                const std::function<void()> &vehicle,
                                const std::function<void()> &display) {
    Json errors = Json::object();
    auto attempt = [&](const char *stage, const std::function<void()> &work) {
        try {
            work();

            return true;
        } catch (const std::exception &error) {
            std::string message = error.what();

            if (message.size() > 2048) {
                message.resize(2048);
            }

            for (char &c : message) {
                if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) >= 127) {
                    c = '?';
                }
            }

            errors[stage] = message;

            return false;
        }
    };

    if (attempt("guard", guard)) {
        attempt("vehicle", vehicle);
        attempt("display", display);
    }

    return errors;
}
} // namespace voyemu
