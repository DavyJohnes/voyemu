#include "api_internal.hpp"
#include "error.hpp"

namespace voyemu::api {
using namespace detail;

Json Api::cluster_tree(const Json &request, Clock::time_point deadline) {
    const auto host_started = monotonic_us();

    if (deadline == Clock::time_point{}) {
        deadline = Clock::now() +
                   std::chrono::milliseconds(number(request, "timeout_ms", 5000, 1, 30000));
    }

    const auto name = profile(request);
    // The Gauges of the Free/Dreamer platform on the 8155 board carry the Kanzi observer; Passion
    // has no Kanzi Gauge.
    const auto kanzi = [](std::string_view candidate) {
        return embedded_profile_declares(candidate, ProfilePlatform::qinggan09,
                                         ProfileBoard::sa8155) ||
               embedded_profile_declares(candidate, ProfilePlatform::qinggan11,
                                         ProfileBoard::sa8155);
    };

    if (!kanzi(name)) {
        // A local profile copy reaches the Kanzi scene through its retained machine whose
        // profile names a qinggan09 or qinggan11 reference profile on the 8155 board.
        const auto record = session_record(request, false);
        const auto selection_file =
                std::filesystem::path(record.at("directory").get<std::string>()) /
                "machine-selection.json";

        if (!ordinary_native_launch_file(selection_file)) {
            throw VoyemuError("TEST_UNSUPPORTED", "Kanzi requires a pinned machine selection");
        }

        const auto selection = read_private_json(selection_file, 4096);
        const std::filesystem::path seed = text(selection, "seed", 4096);

        if (!machine_selector_matches(root_, {seed, name}, text(selection, "machine")) ||
            std::filesystem::canonical(seed) != seed) {
            throw VoyemuError("TEST_SESSION", "retained Kanzi machine selection differs");
        }

        const auto manifest_bytes = read_private_json_bytes(seed / "runtime.json", 128 * 1024);

        if (sha256_bytes(manifest_bytes) != text(selection, "manifest_sha256")) {
            throw VoyemuError("TEST_SESSION", "retained Kanzi machine manifest changed");
        }

        const auto manifest = Json::parse(manifest_bytes);

        if (!manifest.is_object() || !manifest.contains("schema") || manifest.at("schema") != 1 ||
            manifest.value("kind", std::string{}) != "portable-guest-runtime" ||
            manifest.value("profile", std::string{}) != name) {
            throw VoyemuError("TEST_SESSION", "invalid Kanzi machine manifest identity");
        }

        const auto definition_bytes = read_private_json_bytes(seed / "profile.json", 1024 * 1024);

        if (sha256_bytes(definition_bytes) != text(manifest, "profile_sha256")) {
            throw VoyemuError("TEST_SESSION", "retained Kanzi machine profile changed");
        }

        const auto definition = Json::parse(definition_bytes);
        const auto reference = runtime_profile_reference(definition, name);

        if (definition.value("id", std::string{}) != name || !kanzi(reference) ||
            !manifest.contains("qnx")) {
            throw VoyemuError("TEST_UNSUPPORTED",
                              "selected machine has no compatible Kanzi scene provider");
        }

        (void)qnx_portable_layout(reference, manifest.at("qnx"));
    }

    std::uint64_t generation = 0, producer = 0, sequence = 0, heartbeat = 0;
    bool started = false;

    for (;;) {
        const auto remaining =
                std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();

        if (remaining <= 0) {
            throw VoyemuError("CLUSTER_UI_NOT_READY",
                              "no live Gauge scene snapshot before the deadline");
        }

        Json sample;
        try {
            sample = transports_.qnx->ui(request, static_cast<int>(remaining));
        } catch (const std::exception &error) {
            if (std::string_view(error.what()).starts_with("IPC_OPERATION:")) {
                throw VoyemuError("TEST_UNSUPPORTED",
                                  "running supervisor predates Kanzi UI observation");
            }

            throw;
        }

        if (Clock::now() >= deadline) {
            throw VoyemuError("CLUSTER_UI_NOT_READY", "Gauge observation deadline elapsed");
        }

        if (!sample.contains("generation") || !sample.at("generation").is_number_unsigned() ||
            !sample.contains("status") || !sample.at("status").is_string()) {
            throw VoyemuError("CLUSTER_UI_PROTOCOL", "invalid supervisor envelope");
        }

        const auto current_generation = sample.at("generation").get<std::uint64_t>();

        if (started && current_generation != generation) {
            throw VoyemuError("CLUSTER_UI_STALE",
                              "QNX generation changed during scene observation");
        }

        started = true;
        generation = current_generation;

        if (sample.at("status") == "off") {
            throw VoyemuError("CLUSTER_UI_NOT_READY", "cluster is powered off");
        }

        if (sample.at("status") == "sample") {
            for (const auto *field : {"owner", "sequence", "heartbeat_ns", "guest_monotonic_ns"}) {
                if (!sample.contains(field) || !sample.at(field).is_number_unsigned() ||
                    sample.at(field) == 0) {
                    throw VoyemuError("CLUSTER_UI_PROTOCOL", "invalid Gauge observation identity");
                }
            }

            const auto window_side = [&](const char *side) {
                return sample.contains("window") && sample.at("window").is_object() &&
                       sample.at("window").contains(side) &&
                       sample.at("window").at(side).is_number_unsigned() &&
                       sample.at("window").at(side) != 0;
            };

            if (sample.contains("complete") && sample.at("complete") == false &&
                (!sample.contains("partial_reason") || !sample.at("partial_reason").is_string() ||
                 sample.at("partial_reason").get<std::string>().empty())) {
                throw VoyemuError("CLUSTER_UI_PROTOCOL", "partial snapshot lacks reason");
            }

            if (!generation || !sample.contains("complete") ||
                !sample.at("complete").is_boolean() || !sample.contains("nodes") ||
                !sample.at("nodes").is_array() || sample.at("nodes").empty() ||
                !sample.contains("presented_ns") ||
                !sample.at("presented_ns").is_number_unsigned() || !window_side("width") ||
                !window_side("height")) {
                throw VoyemuError("CLUSTER_UI_PROTOCOL", "invalid Gauge observation shape");
            }

            const auto current_owner = sample.at("owner").get<std::uint64_t>();
            const auto current_sequence = sample.at("sequence").get<std::uint64_t>();
            const auto current_heartbeat = sample.at("heartbeat_ns").get<std::uint64_t>();

            if (current_sequence & 1) {
                throw VoyemuError("CLUSTER_UI_PROTOCOL", "uncommitted Gauge sequence");
            }

            if (producer == current_owner && current_sequence >= sequence &&
                current_heartbeat > heartbeat) {
                const bool complete = sample.at("complete").get<bool>();
                auto nodes = std::move(sample.at("nodes"));

                for (auto &node : nodes) {
                    if (Clock::now() >= deadline) {
                        throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "public response deadline");
                    }

                    if (!node.is_object() || !node.contains("id") ||
                        !node.at("id").is_number_unsigned() || !node.contains("parent") ||
                        (!node.at("parent").is_null() && !node.at("parent").is_number_unsigned())) {
                        throw VoyemuError("CLUSTER_UI_PROTOCOL", "invalid Gauge node identity");
                    }

                    node["id"] = std::to_string(node.at("id").get<std::uint64_t>());

                    if (!node.at("parent").is_null()) {
                        node["parent"] = std::to_string(node.at("parent").get<std::uint64_t>());
                    }
                }

                const auto &session_id = request.at("session").at("id");
                const auto presented = sample.at("presented_ns").get<std::uint64_t>();
                const auto sampled = sample.at("guest_monotonic_ns").get<std::uint64_t>();
                // Node and window IDs are valid only within this observation.
                const auto observation_id = std::to_string(generation) + "-" +
                                            std::to_string(current_owner) + "-" +
                                            std::to_string(current_sequence);
                Json window = {{"id", "0"},
                               {"source", "qnx.screen"},
                               {"binding", "posted_before_snapshot"},
                               {"buffer_size", sample.at("window")}};
                Json screen = {{"screen", "cluster"},
                               {"status", complete ? "ok" : "partial"},
                               {"complete", complete},
                               {"selection_coverage", "gauge_node2d_tree"},
                               {"windows", Json::array({std::move(window)})},
                               {"nodes", std::move(nodes)}};

                if (!complete) {
                    screen["partial_reason"] = sample.at("partial_reason");
                }

                Json result = {{"format", "voyemu.ui.v1"},
                               {"profile", profile(request)},
                               {"session_id", session_id},
                               {"source", "qnx.kanzi"},
                               {"source_clock_domain", "qnx.monotonic"},
                               {"layer", "gauge"},
                               {"scope", "gauge_node2d_tree"},
                               {"observation", "last_sampled_present"},
                               {"observation_id", observation_id},
                               {"freshness", presented <= sampled ? "latest_present"
                                                                  : "older_than_latest_present"},
                               {"latest_present_guest_monotonic_ns", std::max(presented, sampled)},
                               {"qnx_generation", generation},
                               {"sequence", current_sequence},
                               {"guest_monotonic_ns", sample.at("guest_monotonic_ns")},
                               {"host_interval",
                                {{"clock_domain", "host.steady"},
                                 {"clock_id", session_id},
                                 {"start_ns", std::to_string(host_started * 1000)},
                                 {"end_ns", std::to_string(monotonic_us() * 1000)}}},
                               {"status", complete ? "ready" : "partial"},
                               {"complete", complete},
                               {"atomic_ui_state", false},
                               {"screens", Json::array({std::move(screen)})}};

                if (!complete) {
                    result["partial_reason"] = sample.at("partial_reason");
                }

                if (Clock::now() >= deadline) {
                    throw VoyemuError("CLUSTER_UI_RESOURCE_LIMIT", "public response deadline");
                }

                return result;
            }

            if (producer && producer != current_owner) {
                throw VoyemuError("CLUSTER_UI_STALE",
                                  "Gauge producer changed during scene observation");
            }

            if (producer && (current_sequence < sequence || current_heartbeat < heartbeat)) {
                throw VoyemuError("CLUSTER_UI_STALE",
                                  "Gauge scene publisher regressed during observation");
            }

            producer = current_owner;
            sequence = current_sequence;
            heartbeat = current_heartbeat;
        } else if (sample.at("status") == "unavailable") {
            throw VoyemuError("CLUSTER_UI_NOT_READY", text(sample, "reason", 64));
        } else if (sample.at("status") != "awaiting") {
            throw VoyemuError("CLUSTER_UI_PROTOCOL", "unrecognized supervisor response");
        }

        std::this_thread::sleep_until(
                std::min(deadline, Clock::now() + std::chrono::milliseconds(75)));
    }
}

Json Api::tree(const Json &request, Clock::time_point deadline, bool parallel) {
    const auto screens = requested_screens(request, true);

    validate_ui_filter(request);
    const auto selector = request.value("selector", Json());

    if (deadline == Clock::time_point{}) {
        deadline = Clock::now() +
                   std::chrono::milliseconds(number(request, "timeout_ms", 5000, 1, 30000));
    }

    const auto host_started = monotonic_us();
    Json surfaces = Json::array();
    std::set<std::string> requested;

    for (const auto &screen : screens) {
        const auto name = "media_" + screen.get<std::string>();
        surfaces.push_back(name);
        requested.insert(name);
    }

    const auto remaining = [&]() {
        const auto value =
                std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();

        if (value <= 0) {
            throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "tree deadline elapsed");
        }

        return static_cast<int>(value);
    };
    const auto record = session_record(request, false);
    const auto metadata_path = std::filesystem::path(record.at("directory").get<std::string>()) /
                               "ui-observer.json";
    Json owner;

    if (std::filesystem::exists(metadata_path)) {
        owner = read_private_json(metadata_path, 4096);

        if (owner.value("schema", 0) != 1 ||
            owner.at("session_id") != request.at("session").at("id")) {
            throw VoyemuError("TREE_STALE", "observer connection belongs to another session");
        }
    } else {
        try {
            const auto budget = remaining();
            owner = transports_.android->ui_observer(request, false, budget);
        } catch (const std::exception &error) {
            if (std::string_view(error.what()).starts_with("IPC_OPERATION:")) {
                throw VoyemuError("TEST_UNSUPPORTED",
                                  "running native session predates UI observer support");
            }

            throw;
        }
    }

    auto connection = owner.at("connection");
    const auto exchange = [&](const Json &message, int budget) {
        return transports_.android->ui_exchange(request, connection, message, budget);
    };
    Json result, hello, parallel_epoch;
    unsigned attempts = 0;
    bool recovered_transport = false;
    Json rejections = Json::object();
    std::string last_rejection;
    const auto retry_remaining = [&]() {
        if (Clock::now() >= deadline && !last_rejection.empty()) {
            throw VoyemuError("TREE_TIMEOUT_UNKNOWN",
                              "UI read deadline exhausted; cause=TREE_REJECTED; reason=" +
                                      last_rejection + "; attempts=" + std::to_string(attempts));
        }

        return remaining();
    };

    for (;;) {
        Json observation = {
                {"op", "ui"}, {"surfaces", surfaces}, {"timeout_ms", retry_remaining()}};

        if (request.contains("scope")) {
            if (request.at("scope") == "interactive" && connection.value("api_level", 0) != 30) {
                throw VoyemuError("TEST_UNSUPPORTED",
                                  "interactive UI scope requires Android API30");
            }

            observation["scope"] = {{"windows", request.at("scope")}};
        }

        if (!selector.is_null()) {
            observation["selector"] = selector;
        }

        try {
            ++attempts;

            if (parallel) {
                const auto capabilities = exchange({{"op", "hello"}}, retry_remaining());

                if (!capabilities.value("ok", false) ||
                    capabilities.at("boot_id") != connection.at("boot_id") ||
                    capabilities.at("observer_pid") != connection.at("observer_pid")) {
                    throw VoyemuError("TREE_STALE",
                                      "observer lifetime changed before parallel read");
                }

                if (capabilities.value("parallel_surfaces", Json(false)) != true) {
                    throw VoyemuError("TEST_UNSUPPORTED",
                                      "running Android observer has no parallel UI support");
                }

                parallel_epoch = capabilities.value("observer_epoch", Json());

                if (!parallel_epoch.is_string() ||
                    parallel_epoch.get_ref<const std::string &>().empty() ||
                    parallel_epoch.get_ref<const std::string &>().size() > 128) {
                    throw VoyemuError("TREE_PROTOCOL", "missing parallel observer epoch");
                }

                observation["parallel_surfaces"] = true;
                observation["scope"] = {{"windows", "all_attached"}};
                observation["timeout_ms"] = retry_remaining();
            }

            if (request.value("scope", std::string{}) == "interactive") {
                const auto capabilities = exchange({{"op", "hello"}}, retry_remaining());

                if (!capabilities.value("ok", false) ||
                    capabilities.at("boot_id") != connection.at("boot_id") ||
                    capabilities.at("observer_pid") != connection.at("observer_pid")) {
                    throw VoyemuError("TREE_STALE",
                                      "interactive observer lifetime changed before read");
                }

                const auto scopes = capabilities.value("ui_scopes", Json());

                if (!scopes.is_array() || scopes.size() > 4 ||
                    std::find(scopes.begin(), scopes.end(), Json("interactive")) == scopes.end()) {
                    throw VoyemuError("TEST_UNSUPPORTED",
                                      "running observer predates interactive UI scope");
                }
            }

            result = exchange(observation, retry_remaining());

            if (result.value("schema", 0) != 1) {
                throw VoyemuError("TREE_PROTOCOL", "unsupported internal observer schema");
            }

            if (!result.value("ok", false)) {
                if (result.contains("reason") && result.at("reason").is_string() &&
                    result.at("reason") == "deadline") {
                    throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "guest observation deadline elapsed");
                }

                const auto reason = result.contains("reason") && result.at("reason").is_string()
                                            ? result.at("reason").get<std::string>()
                                            : std::string{};
                const std::set<std::string> transient = {"stale_child", "stale_node",
                                                         "window_changed", "node_window_changed",
                                                         "snapshot_busy"};

                if (transient.contains(reason)) {
                    last_rejection = reason;
                    rejections[reason] = rejections.value(reason, 0u) + 1;
                    const auto fence = control(request, {{"op", "status"}}, false,
                                               retry_remaining());
                    const auto identity = exchange({{"op", "hello"}}, retry_remaining());

                    if (fence.value("stopped", true) ||
                        fence.at("android_pid") != owner.at("android_pid") ||
                        !identity.value("ok", false) ||
                        identity.at("boot_id") != connection.at("boot_id") ||
                        identity.at("observer_pid") != connection.at("observer_pid")) {
                        throw VoyemuError("TREE_STALE",
                                          "session or boot changed between UI observations");
                    }

                    std::this_thread::yield();
                    continue;
                }

                std::string diagnostic = "TREE_REJECTED: guest could not verify current UI tree";

                for (const auto *field : {"reason", "error_type", "cause_type"}) {
                    if (!result.contains(field) || !result.at(field).is_string()) {
                        continue;
                    }

                    const auto value = result.at(field).get<std::string>();

                    if (value.empty() || value.size() > 160 ||
                        value.find_first_not_of(
                                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-$") !=
                                std::string::npos) {
                        continue;
                    }

                    const std::set<std::string> reasons = {"accessibility_service",
                                                           "application_window_unverified",
                                                           "boot_changed",
                                                           "boot_identity",
                                                           "child_limit",
                                                           "cursor_bounds",
                                                           "deadline",
                                                           "display_changed",
                                                           "display_unavailable",
                                                           "duplicate_surface",
                                                           "empty_tree_unverified",
                                                           "event_reader_limit",
                                                           "incomplete_request",
                                                           "interactive_scope_unsupported",
                                                           "node_limit",
                                                           "node_window_changed",
                                                           "observer_unavailable",
                                                           "output_limit",
                                                           "owned_api28_observer_required",
                                                           "peer_owner",
                                                           "request_limit",
                                                           "request_owner",
                                                           "secondary_display_ambiguous",
                                                           "selector_changed",
                                                           "selector_key",
                                                           "selector_limit",
                                                           "selector_package",
                                                           "selector_resource",
                                                           "selector_unavailable",
                                                           "snapshot_busy",
                                                           "source_restart",
                                                           "stale_child",
                                                           "stale_match",
                                                           "stale_node",
                                                           "subscription_cancelled",
                                                           "subscription_expired",
                                                           "subscription_limit",
                                                           "surface_count",
                                                           "timeout_bounds",
                                                           "unsupported_operation",
                                                           "unsupported_surface",
                                                           "window_changed",
                                                           "window_display_conflict",
                                                           "window_limit",
                                                           "wm_display_conflict"};
                    const std::set<std::string> types = {"ClassNotFoundException",
                                                         "IOException",
                                                         "IllegalArgumentException",
                                                         "IllegalStateException",
                                                         "IndexOutOfBoundsException",
                                                         "InvocationTargetException",
                                                         "JSONException",
                                                         "NoSuchMethodException",
                                                         "NullPointerException",
                                                         "RemoteException",
                                                         "RuntimeException",
                                                         "SecurityException",
                                                         "UnsupportedOperationException"};

                    if (!(std::string_view(field) == "reason" ? reasons : types).contains(value)) {
                        continue;
                    }

                    diagnostic += std::string("; ") + field + "=" + value;
                }

                throw std::runtime_error(diagnostic);
            }

            if (parallel && result.value("parallel_surfaces", Json(false)) != true) {
                throw VoyemuError("TREE_PROTOCOL", "observer did not confirm parallel surfaces");
            }

            hello = exchange({{"op", "hello"}}, remaining());
            break;
        } catch (const std::exception &error) {
            if (!last_rejection.empty() &&
                std::string_view(error.what()).starts_with("TREE_TIMEOUT_UNKNOWN:") &&
                std::string_view(error.what()).find("cause=TREE_REJECTED") ==
                        std::string_view::npos) {
                throw std::runtime_error(std::string(error.what()) +
                                         "; cause=TREE_REJECTED; reason=" + last_rejection +
                                         "; attempts=" + std::to_string(attempts));
            }

            if (recovered_transport ||
                !std::string_view(error.what()).starts_with("TREE_TRANSPORT_UNKNOWN:")) {
                throw;
            }

            recovered_transport = true;
            auto recovered = transports_.android->ui_observer(request, true, remaining());
            const auto fresh = recovered.at("connection");

            if (recovered.at("session_id") != owner.at("session_id") ||
                recovered.at("android_pid") != owner.at("android_pid") ||
                fresh.at("boot_id") != connection.at("boot_id")) {
                throw VoyemuError("TREE_STALE",
                                  "session or boot changed while reconnecting observer");
            }

            owner = std::move(recovered);
            connection = fresh;
        }
    }

    const auto current = control(request, {{"op", "status"}}, false, remaining());

    if (parallel) {
        if (current.value("stopped", true)) {
            throw VoyemuError("TEST_SESSION", "session stopped during UI observation");
        }

        if (Clock::now() >= deadline) {
            throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "parallel UI fence exceeded deadline");
        }

        if (result.value("observer_epoch", Json()) != parallel_epoch ||
            hello.value("observer_epoch", Json()) != parallel_epoch) {
            throw VoyemuError("TREE_STALE", "parallel observer epoch changed");
        }
    }

    if (current.value("stopped", true) || current.at("android_pid") != owner.at("android_pid") ||
        result.at("boot_id") != connection.at("boot_id") ||
        result.at("observer_pid") != connection.at("observer_pid") || !hello.value("ok", false) ||
        hello.at("boot_id") != connection.at("boot_id") ||
        hello.at("observer_pid") != connection.at("observer_pid")) {
        throw VoyemuError("TREE_STALE", "session or guest lifetime changed during observation");
    }

    if (!result.contains("surfaces") || !result.at("surfaces").is_array() ||
        result.at("surfaces").size() != requested.size()) {
        throw VoyemuError("TREE_PROTOCOL", "surface results missing");
    }

    std::set<std::string> seen;
    bool all_complete = true;
    bool observed = false;
    const auto observation_start =
            parallel ? number(result, "started_uptime_ms", UINT64_MAX, 0, INT64_MAX) : 0;
    const auto observation_end = parallel ? number(result, "completed_uptime_ms", UINT64_MAX,
                                                   observation_start, INT64_MAX)
                                          : 0;

    for (auto &surface : result.at("surfaces")) {
        const auto name = surface.value("surface", std::string{});

        if (!requested.contains(name) || !seen.insert(name).second) {
            throw VoyemuError("TREE_PROTOCOL", "wrong logical screen identity");
        }

        const auto slot = observed_slot(name);

        if (surface.contains("screen") && surface.at("screen") != observed_screens[slot]) {
            throw VoyemuError("TREE_PROTOCOL", "mismatched logical screen");
        }

        if (surface.contains("sdk_display_id") && surface.at("sdk_display_id") != slot) {
            throw VoyemuError("TREE_PROTOCOL", "wrong SDK display slot");
        }

        const auto status = surface.value("status", std::string{});
        const bool unobserved = status == "unsupported" || (parallel && status == "unavailable");
        const auto display = surface.value("display_id", Json());

        if (!(parallel && unobserved && display.is_null()) &&
            (!display.is_number_integer() || display.get<int>() < 0 ||
             (slot == 0 && display != 0) || (slot == 2 && display == 0) ||
             (parallel && slot == 1 && display == 0))) {
            throw VoyemuError("TREE_PROTOCOL", "invalid guest display identity");
        }

        if (parallel) {
            const auto started = number(surface, "started_uptime_ms", UINT64_MAX, observation_start,
                                        observation_end);
            const auto finished = number(surface, "completed_uptime_ms", UINT64_MAX, started,
                                         observation_end);

            if (surface.value("clock_domain", std::string{}) != "android_uptime_ms") {
                throw VoyemuError("TREE_PROTOCOL", "missing parallel surface clock");
            }

            surface["started_uptime_ms"] = std::to_string(started);
            surface["completed_uptime_ms"] = std::to_string(finished);
        }

        if (request.value("scope", std::string{}) == "interactive") {
            if (surface.value("scope", std::string{}) != "api30_interactive" ||
                surface.value("selection_coverage", std::string{}) != "interactive_windows_only") {
                throw VoyemuError("TREE_PROTOCOL", "interactive UI scope provenance missing");
            }

            if (!surface.contains("windows") || !surface.at("windows").is_array()) {
                throw VoyemuError("TREE_PROTOCOL", "interactive window identities missing");
            }

            for (const auto &window : surface.at("windows")) {
                if (!window.contains("display_id") ||
                    window.at("display_id") != surface.at("display_id")) {
                    throw VoyemuError("TREE_PROTOCOL",
                                      "interactive window belongs to another display");
                }
            }
        }

        surface["screen"] = observed_screens[slot];
        surface["sdk_display_id"] = slot;
        surface.erase("surface");

        if (unobserved) {
            if (parallel) {
                surface["complete"] = false;
                surface["nodes"] = Json::array();
                surface["windows"] = Json::array();

                if (display.is_null()) {
                    surface.erase("display_id");
                }
            }

            all_complete = false;
            continue;
        }

        if ((status != "ok" && status != "partial") || !surface.contains("nodes") ||
            !surface.at("nodes").is_array() || surface.at("nodes").size() > 12000 ||
            !surface.contains("windows") || !surface.at("windows").is_array() ||
            surface.at("windows").size() > 64) {
            throw VoyemuError("TREE_PROTOCOL", "bounded node/window arrays required");
        }

        if (parallel) {
            if (surface.value("scope", std::string{}) != "all_attached" ||
                surface.value("complete", Json()) != Json(status == "ok")) {
                throw VoyemuError("TREE_PROTOCOL", "invalid parallel surface coverage");
            }

            for (const auto &window : surface.at("windows")) {
                if (!window.contains("display_id") || window.at("display_id") != display) {
                    throw VoyemuError("TREE_PROTOCOL",
                                      "parallel window belongs to another display");
                }
            }
        }

        observed = true;
        all_complete &= surface.value("complete", false);

        for (auto &window : surface.at("windows")) {
            for (const char *key : {"bounds", "frame"}) {
                if (window.contains(key) && window.at(key).is_array()) {
                    const auto b = window.at(key);

                    if (b.size() != 4 || !std::all_of(b.begin(), b.end(), [](const Json &v) {
                            return v.is_number_integer();
                        })) {
                        throw VoyemuError("TREE_PROTOCOL", "invalid window rectangle");
                    }

                    if (b[2] < b[0] || b[3] < b[1]) {
                        throw VoyemuError("TREE_PROTOCOL", "inverted window rectangle");
                    }

                    window[key] = {{"x", b[0]},
                                   {"y", b[1]},
                                   {"width", b[2].get<std::int64_t>() - b[0].get<std::int64_t>()},
                                   {"height", b[3].get<std::int64_t>() - b[1].get<std::int64_t>()}};
                }
            }
        }

        const bool context_selection = surface.value("selection", std::string{}) ==
                                       "resource_id_with_tree_context";

        if (!selector.is_null() &&
            ((surface.value("selection", std::string{}) != "resource_id" && !context_selection) ||
             surface.value("matched_count", 12001u) > 64 ||
             (!context_selection && surface.at("matched_count") != surface.at("nodes").size()))) {
            throw VoyemuError("TREE_PROTOCOL", "bounded resource selection required");
        }

        std::size_t matched = 0;
        const auto width = number(surface, "width", 1920, 1, 3840),
                   height = number(surface, "height", 720, 1, 2160);

        for (auto &node : surface.at("nodes")) {
            const bool selected = !selector.is_null() &&
                                  (!context_selection || node.value("matched", false));

            if (selected) {
                ++matched;
            }

            if (selected && (node.value("resource_id", std::string{}) !=
                                     selector.at("resource_id").get<std::string>() ||
                             (selector.contains("package") &&
                              node.value("package", std::string{}) !=
                                      selector.at("package").get<std::string>()) ||
                             (!context_selection &&
                              (!node.contains("parent") || !node.at("parent").is_null())))) {
                throw VoyemuError("TREE_PROTOCOL", "selector returned unrelated node");
            }

            if (node.value("text_redacted", false) &&
                ((node.contains("text") && !node.at("text").is_null() && node.at("text") != "") ||
                 (node.contains("description") && !node.at("description").is_null() &&
                  node.at("description") != ""))) {
                throw VoyemuError("TREE_PROTOCOL", "password text was not redacted");
            }

            const auto clipped = node.value("clipped_bounds", Json());

            if (clipped.is_null() && !node.value("actionable", false)) {
                continue;
            }

            if (!clipped.is_array() || clipped.size() != 4) {
                throw VoyemuError("TREE_PROTOCOL", "clipped bounds required");
            }

            for (std::size_t i = 0; i < 4; ++i) {
                if (!clipped[i].is_number_integer() || clipped[i].get<std::int64_t>() < 0 ||
                    clipped[i].get<std::int64_t>() >
                            static_cast<std::int64_t>(i % 2 ? height : width)) {
                    throw VoyemuError("TREE_PROTOCOL", "clipped bounds outside viewport");
                }
            }

            if (clipped[2] < clipped[0] || clipped[3] < clipped[1]) {
                throw VoyemuError("TREE_PROTOCOL", "inverted clipped bounds");
            }

            if (node.value("actionable", false) &&
                (!node.value("visible", false) || !node.value("enabled", false) ||
                 !node.value("clickable", false) || clipped[2] == clipped[0] ||
                 clipped[3] == clipped[1])) {
                throw VoyemuError("TREE_PROTOCOL", "offscreen or disabled actionable node");
            }

            for (const char *key : {"bounds", "clipped_bounds"}) {
                if (node.contains(key) && node.at(key).is_array() && node.at(key).size() == 4) {
                    const auto b = node.at(key);

                    for (const auto &coordinate : b) {
                        if (!coordinate.is_number_integer() ||
                            (coordinate.is_number_unsigned()
                                     ? coordinate.get<std::uint64_t>() > INT32_MAX
                                     : (coordinate.get<std::int64_t>() < INT32_MIN ||
                                        coordinate.get<std::int64_t>() > INT32_MAX))) {
                            throw VoyemuError("TREE_PROTOCOL", "invalid node rectangle");
                        }
                    }

                    const auto w = b[2].get<std::int64_t>() - b[0].get<std::int64_t>(),
                               h = b[3].get<std::int64_t>() - b[1].get<std::int64_t>();

                    if ((w < 0 || h < 0) &&
                        (node.value("visible", false) || node.value("actionable", false))) {
                        throw VoyemuError("TREE_PROTOCOL", "inverted visible node rectangle");
                    }

                    node[key] = {{"x", b[0]},
                                 {"y", b[1]},
                                 {"width", std::max<std::int64_t>(0, w)},
                                 {"height", std::max<std::int64_t>(0, h)}};
                }
            }
        }

        if (!selector.is_null() && matched != surface.at("matched_count").get<std::size_t>()) {
            throw VoyemuError("TREE_PROTOCOL", "incorrect match count");
        }
    }

    if (seen != requested) {
        throw VoyemuError("TREE_PROTOCOL", "observer omitted a requested screen");
    }

    if (!observed && !parallel) {
        throw VoyemuError("TEST_UNSUPPORTED", "requested display tree has no verified backend");
    }

    result.erase("ok");
    result.erase("schema");
    result["format"] = "voyemu.ui.v1";
    result["source"] = "android.ui";
    result["screens"] = result.at("surfaces");
    result.erase("surfaces");
    result["profile"] = profile(request);
    result["session_id"] = owner.at("session_id");
    result["android_pid"] = owner.at("android_pid");
    result["complete"] = all_complete;
    result["host_interval"] = {{"clock_domain", "host.steady"},
                               {"clock_id", owner.at("session_id")},
                               {"start_ns", std::to_string(host_started * 1000)},
                               {"end_ns", std::to_string(monotonic_us() * 1000)}};
    result["vehicle_revision"] = std::to_string(current.at("revision").get<std::uint64_t>());

    for (const char *key : {"started_uptime_ms", "completed_uptime_ms"}) {
        if (result.contains(key) && result.at(key).is_number_integer()) {
            result[key] = std::to_string(result.at(key).get<std::uint64_t>());
        }
    }

    result["read_attempts"] = attempts;
    result["transient_rejections"] = std::move(rejections);
    result["atomic_ui_state"] = false;
    result["status"] = all_complete ? "ready" : "partial";
    result["source_clock_domain"] = "android.uptime";

    if (result.contains("completed_uptime_ms")) {
        result["source_timestamp_ms"] = result.at("completed_uptime_ms");
    }

    if (result.contains("observer_epoch")) {
        result["source_instance"] = result.at("observer_epoch");
    }

    if (result.contains("observation_id")) {
        result["revision"] = result.at("observation_id");
    }

    if (!selector.is_null()) {
        result["selector"] = selector;
    }

    if (connection.contains("auth") &&
        result.dump().find(connection.at("auth").get<std::string>()) != std::string::npos) {
        throw VoyemuError("TREE_PROTOCOL", "private credential in public tree");
    }

    return result;
}

Json Api::session_ui(const Json &request, Clock::time_point deadline) {
    const bool explicit_screens = request.contains("screen") || request.contains("screens");
    const auto screens = explicit_screens ? requested_screens(request)
                                          : Json::array({"cluster", "primary", "secondary"});
    const auto selected = [&](std::string_view screen) {
        return std::find(screens.begin(), screens.end(), screen) != screens.end();
    };
    const bool want_cluster = selected("cluster"), want_primary = selected("primary"),
               want_secondary = selected("secondary"), want_rear = selected("rear"),
               want_hud = selected("hud");

    if (want_cluster && (request.contains("scope") || request.contains("selector"))) {
        throw VoyemuError("TEST_ARGUMENT", "UI scope and selector require multimedia screens only");
    }

    validate_ui_filter(request);
    auto media_request = request;
    media_request.erase("screen");
    media_request["screens"] = Json::array();

    for (const auto &screen : screens) {
        if (screen != "cluster" && screen != "hud") {
            media_request["screens"].push_back(screen);
        }
    }

    if (deadline == Clock::time_point{}) {
        deadline = Clock::now() +
                   std::chrono::milliseconds(number(request, "timeout_ms", 5000, 1, 30000));
    }

    const auto active_session = [&] {
        auto record = session_record(request, false);
        const auto terminal = std::filesystem::path(record.at("directory").get<std::string>()) /
                              "runtime.json";

        if (ordinary_native_launch_file(terminal)) {
            const auto status = read_private_json(terminal);

            if (status.value("session_id", Json()) != record.at("session_id") ||
                status.value("stopped", false)) {
                throw VoyemuError("TEST_SESSION", "session stopped or terminal identity changed");
            }
        }

        return record;
    };
    const auto original = active_session();

    if (want_cluster && (want_primary || want_secondary || want_rear)) {
        const auto remaining =
                std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();

        if (remaining <= 0) {
            throw VoyemuError("TREE_TIMEOUT", "session UI deadline elapsed before status check");
        }

        if (control(request, {{"op", "status"}}, false, static_cast<int>(remaining))
                    .value("stopped", true)) {
            throw VoyemuError("TEST_SESSION", "session is stopped");
        }
    }

    const auto unavailable = [&](const char *screen, const Json &failure) {
        const auto &code = failure.at("code");
        Json slot{{"screen", screen},
                  {"status", code == "TEST_UNSUPPORTED" ? "unsupported" : "unavailable"},
                  {"complete", false},
                  {"windows", Json::array()},
                  {"nodes", Json::array()},
                  {"error_code", code}};

        if (explicit_screens) {
            slot["error"] = failure;
        }

        return slot;
    };
    const auto read = [](auto observation) {
        try {
            return observation();
        } catch (const std::exception &failure) {
            auto detail = error(failure);
            const auto &code = detail.at("code");

            if (code == "TEST_SESSION" || code == "TEST_OWNERSHIP" || code == "PROFILE_NAME") {
                throw;
            }

            return Json{{"error", std::move(detail)}};
        }
    };
    const auto read_cluster = [&] {
        return read([&] {
            return cluster_tree(request, deadline);
        });
    };
    Json cluster, android;

    if (want_primary || want_secondary || want_rear) {
        std::future<Json> qnx;

        if (want_cluster) {
            qnx = std::async(std::launch::async, read_cluster);
        }

        // The parallel read covers exactly the two multimedia screens.
        const bool parallel = want_primary && want_secondary && !want_rear &&
                              !request.contains("selector") &&
                              request.value("scope", std::string{}) != "interactive";
        android = read([&] {
            return tree(media_request, deadline, parallel);
        });

        if (want_cluster) {
            cluster = qnx.get();
        }
    } else if (want_cluster) {
        cluster = read_cluster();
    }

    const auto current = active_session();

    if (original.at("directory") != current.at("directory") ||
        original.at("endpoint") != current.at("endpoint")) {
        throw VoyemuError("TEST_SESSION", "active endpoint changed during UI collection");
    }

    const auto attach = [](Json &slot, const Json &source,
                           std::initializer_list<const char *> fields) {
        for (const auto *field : fields) {
            if (source.contains(field)) {
                slot[field] = source.at(field);
            }
        }
    };
    Json cluster_slot;

    if (cluster.contains("error")) {
        cluster_slot = unavailable("cluster", cluster.at("error"));
    } else if (!cluster.is_null()) {
        cluster_slot = std::move(cluster.at("screens").at(0));
        attach(cluster_slot, cluster,
               {"source", "source_clock_domain", "host_interval", "layer", "scope", "observation",
                "qnx_generation", "sequence", "guest_monotonic_ns", "observation_id", "freshness",
                "latest_present_guest_monotonic_ns"});
    }

    Json media = Json::array({nullptr, nullptr}), rear;

    if (android.contains("error")) {
        if (want_primary) {
            media[0] = unavailable("primary", android.at("error"));
        }

        if (want_secondary) {
            media[1] = unavailable("secondary", android.at("error"));
        }

        if (want_rear) {
            rear = unavailable("rear", android.at("error"));
        }
    } else if (!android.is_null()) {
        for (auto &slot : android.at("screens")) {
            const auto index = slot.at("screen") == "primary" ? 0 : 1;
            const bool rear_slot = slot.at("screen") == "rear";

            if (slot.contains("source")) {
                slot["observer_source"] = std::move(slot.at("source"));
            }

            attach(slot, android,
                   {"source", "source_clock_domain", "host_interval", "boot_id", "android_pid",
                    "observer_pid", "source_instance", "revision", "observation_id",
                    "vehicle_revision", "selector", "read_attempts", "transient_rejections",
                    "observer_epoch", "source_timestamp_ms"});

            if (slot.at("status") == "unsupported") {
                slot["complete"] = false;
                slot["nodes"] = Json::array();
                slot["windows"] = Json::array();
            }

            (rear_slot ? rear : media[index]) = std::move(slot);
        }
    }

    const auto complete_slot = [](const Json &slot) {
        return slot.is_null() || slot.at("complete").get<bool>();
    };
    // Explicitly requested screens outside the main window: the rear screen tree and the HUD,
    // which has no UI observer.
    Json extra = Json::array();

    if (!rear.is_null()) {
        extra.push_back(std::move(rear));
    }

    if (want_hud) {
        extra.push_back(unavailable("hud", {{"code", "TEST_UNSUPPORTED"},
                                            {"message", "the screen has no UI observer"}}));
    }

    bool complete = complete_slot(cluster_slot) && complete_slot(media[0]) &&
                    complete_slot(media[1]);
    const auto observed = [](const Json &slot) {
        return !slot.is_null() && (slot.at("status") == "ok" || slot.at("status") == "partial");
    };
    bool any = observed(cluster_slot) || observed(media[0]) || observed(media[1]);

    for (const auto &slot : extra) {
        complete = complete && complete_slot(slot);
        any = any || observed(slot);
    }

    Json response = {{"format", "voyemu.session.ui.v1"},
                     {"profile", profile(request)},
                     {"session_id", original.at("session_id")},
                     {"status", complete ? "ready"
                                : any    ? "partial"
                                         : "unavailable"},
                     {"complete", complete},
                     {"atomic_ui_state", false},
                     {"cluster", std::move(cluster_slot)},
                     {"multimedia", std::move(media)}};

    if (!extra.empty()) {
        response["screens"] = std::move(extra);
    }

    return response;
}
} // namespace voyemu::api
