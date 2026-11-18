#include "api_internal.hpp"
#include "error.hpp"

namespace voyemu::api {
using namespace detail;

Json Api::dispatch_session(const std::string &op, const Json &request, Clock::time_point deadline) {
    if (op == "session.start") {
        if (request.contains("diagnostic_audio")) {
            throw VoyemuError(
                    "TEST_UNSUPPORTED",
                    "WAV diagnostic capture is not a closed-API field and never a permanent runtime policy; it requires a scoped owner-issued activation");
        }

        if (request.contains("providers")) {
            const auto &providers = request.at("providers");

            if (!providers.is_array() || providers.size() > 4) {
                throw VoyemuError("TEST_ARGUMENT",
                                  "at most four explicit provider manifests required");
            }

            std::set<std::string> unique;

            for (const auto &value : providers) {
                if (!value.is_string()) {
                    throw VoyemuError("TEST_ARGUMENT", "provider manifest path required");
                }

                const auto path = value.get<std::string>();
                const std::filesystem::path file = path;

                if (path.empty() || path.size() > 4096 || !file.is_absolute() ||
                    file.lexically_normal() != file ||
                    path.find_first_of("\r\n") != std::string::npos ||
                    path.find(char(0)) != std::string::npos || !unique.insert(path).second) {
                    throw VoyemuError("TEST_ARGUMENT",
                                      "unique absolute provider manifest paths required");
                }
            }
        }

        const bool refresh = boolean(request, "refresh_host", false);

        if (refresh) {
            const auto &prior = request.at("session");

            for (const char *key : {"id", "ownership_id"}) {
                const auto value = text(prior, key);

                if (!hex_text(value, 32)) {
                    throw VoyemuError("TEST_ARGUMENT",
                                      "refresh_host requires the prior owned session");
                }
            }
        }

        const auto name = profile(request);

        if (request.value("boot", std::string("auto")) != "auto") {
            throw VoyemuError("TEST_UNSUPPORTED", "only actual auto boot selection is implemented");
        }

        const auto scenario = request.value("scenario", std::string("fresh"));

        if (scenario != "fresh" && scenario != "resume") {
            throw VoyemuError("TEST_ARGUMENT", "scenario must be fresh or resume");
        }

        if (request.contains("machine") && request.contains("machine_path")) {
            throw VoyemuError("MACHINE_ARGUMENT",
                              "machine and machine_path are mutually exclusive");
        }

        const bool explicit_path = request.contains("machine_path");
        std::string machine_sha;

        if (request.contains("machine") &&
            (text(request, "machine").empty() || !valid_profile_name(text(request, "machine")))) {
            throw VoyemuError("MACHINE_ARGUMENT", "valid machine child name required");
        }

        auto machine = machine_identity(name, request.value("machine", std::string{}));
        std::filesystem::path selected_seed;

        if (explicit_path) {
            const auto selected = inspect_machine_path(text(request, "machine_path", 4096));

            if (selected.profile != name) {
                throw VoyemuError("MACHINE_SOURCE", "canonical profile differs");
            }

            selected_seed = selected.seed;
            machine = machine_identity(name, machine_path_name(root_, selected));
            machine_sha = sha256_file(selected_seed / "runtime.json");
        } else if (machine.selected) {
            const auto selected = root_ / "machines" / machine.name;

            if (std::filesystem::is_symlink(root_ / "machines") ||
                std::filesystem::is_symlink(selected) ||
                !ordinary_native_launch_file(selected / "runtime.json")) {
                throw VoyemuError("MACHINE_SOURCE", "ordinary selected machine required");
            }

            const auto manifest = read_private_json(selected / "runtime.json", 128 * 1024);

            if (manifest.value("schema", 0) != 1 ||
                manifest.value("kind", std::string{}) != "portable-guest-runtime" ||
                manifest.value("profile", std::string{}) != name) {
                throw VoyemuError("MACHINE_SOURCE", "canonical profile differs");
            }

            selected_seed = selected;
            machine_sha = sha256_file(selected / "runtime.json");
        }

        const auto prepared = profile_launch_directory(root_, name, machine);

        if (!ordinary_native_launch_file(prepared / "android-launch.json") ||
            (boolean(request, "with_cluster", false) &&
             !ordinary_native_launch_file(prepared / "cluster-launch.json"))) {
            const auto portable = explicit_path
                                          ? selected_seed / "runtime.json"
                                          : root_ / "machines" / machine.name / "runtime.json";

            if (!ordinary_native_launch_file(portable)) {
                throw VoyemuError(
                        "PROFILE_NOT_PREPARED",
                        "selected profile has no prepared launch specification or portable runtime");
            }

            const auto manifest = read_private_json(portable, 128 * 1024);

            if (manifest.value("schema", 0) != 1 ||
                manifest.value("kind", std::string{}) != "portable-guest-runtime" ||
                manifest.value("profile", std::string{}) != name ||
                (boolean(request, "with_cluster", false) && !manifest.contains("qnx"))) {
                throw VoyemuError(
                        "PROFILE_NOT_PREPARED",
                        "portable runtime does not support the selected profile and scope");
            }
        }

        if (boolean(request, "with_cluster", false) && request.contains("seconds")) {
            number(request, "seconds", 0, 1, 3600);
        }

        auto created = request;

        if (explicit_path) {
            created["machine_path"] = selected_seed.string();
        }

        created["background"] = boolean(request, "background", true);

        if (!created.contains("ownership_id")) {
            created["ownership_id"] = random_token();
        }

        const auto result = transports_.create(created);

        if (machine.selected &&
            (canonical_machine_name(name, result.value("machine", std::string{})) != machine.name ||
             result.value("machine_seed", std::string{}) != selected_seed.string() ||
             result.value("machine_manifest_sha256", std::string{}) != machine_sha)) {
            throw MachineIdentityError(result, created);
        }

        Json output{
                {"session",
                 {{"id", result.at("session_id")}, {"ownership_id", created.at("ownership_id")}}},
                {"profile", name},
                {"scenario", scenario},
                {"boot_policy", "auto"},
                {"background", created["background"]},
                {"readiness", "session.reachable"},
                {"visible_effect_verified", false},
                {"actual_boot", "read session.status deliveries.*.quickboot after readiness"}};

        if (machine.selected) {
            for (const auto *key : {"machine", "machine_seed", "machine_manifest_sha256"}) {
                output[key] = result.at(key);
            }
        }

        return output;
    }

    if (op == "session.attach") {
        const auto status = control(request, {{"op", "status"}});

        return {{"session", {{"id", request.at("session").at("id")}}},
                {"read_only", true},
                {"status", status}};
    }

    if (op == "session.status" || op == "vehicle.get" || op == "input.wheel.buttons") {
        auto status = control(request, {{"op", "status"}});

        if (op == "vehicle.get") {
            return {{"revision", status.at("revision")},
                    {"state", status.at("state")},
                    {"deliveries", status.at("deliveries")}};
        }

        if (op == "input.wheel.buttons") {
            if (status.at("wheel_routes").is_null()) {
                throw VoyemuError("TEST_UNSUPPORTED",
                                  "stopped runtime no longer exposes live routes");
            }

            return status.at("wheel_routes");
        }

        return status;
    }

    if (phone_input_operation(op) || op == "multimedia.phone.state") {
        return transports_.android->phone(
                request, phone_command(op, request.value("payload", Json::object())));
    }

    if (op == "vehicle.set") {
        if (!request.contains("expected_revision")) {
            throw VoyemuError("TEST_ARGUMENT", "expected_revision required for atomic state patch");
        }

        return control(request, {{"op", "patch"}, {"values", request.at("values")}}, true);
    }

    return unhandled_operation();
}

Json Api::dispatch_lifecycle(const std::string &op, const Json &request,
                             Clock::time_point deadline) {
    if (op == "session.stop") {
        const auto record = session_record(request, true);
        const auto selection_path =
                std::filesystem::path(record.at("directory").get<std::string>()) /
                "machine-selection.json";
        const auto machine = machine_identity(
                profile(request),
                std::filesystem::exists(selection_path)
                        ? read_private_json(selection_path, 4096).at("machine").get<std::string>()
                        : std::string{});
        const auto active_directory = session_active_directory(root_, profile(request), machine);
#ifdef _WIN32
        auto recover = [&] {
            StoppedAndroidLease lease(root_, profile(request), machine,
                                      record.at("session_id").get<std::string>(),
                                      text(request.at("session"), "ownership_id"));
            auto terminal = lease.publish_terminal();
            terminal["accepted"] = true;
            terminal["cleanup"] = {{"verified", true},
                                   {"supervisor_endpoint_closed", true},
                                   {"supervisor_lock_released", true},
                                   {"guest_shutdown_verified", false},
                                   {"guest_shutdown_unverified", true},
                                   {"visible_effect_verified", false}};

            return terminal;
        };

        if (StoppedAndroidLease::endpoint_absent(record.at("endpoint").get<std::string>())) {
            return recover();
        }
#else
        // A free supervisor lock proves that its holder exited; the session receipts then decide
        // what remains to stop.
        const auto supervisor_lock = active_directory / (profile(request) + ".supervisor.lock");

        if (StoppedAndroidLease::endpoint_absent(record.at("endpoint").get<std::string>()) &&
            file_lock_released(supervisor_lock)) {
            const FileLock launch(active_directory / (profile(request) + ".launch.lock")),
                    supervisor(supervisor_lock);
            auto terminal = stop_orphaned_session(record.at("directory").get<std::string>(),
                                                  record.at("session_id").get<std::string>(),
                                                  record.at("endpoint").get<std::string>());
            terminal["accepted"] = true;
            terminal["cleanup"] = {{"verified", true},
                                   {"supervisor_endpoint_closed", true},
                                   {"supervisor_lock_released", true},
                                   {"guest_shutdown_verified", false},
                                   {"guest_shutdown_unverified", true},
                                   {"visible_effect_verified", false}};

            return terminal;
        }
#endif
        Json result;
        try {
            result = control(request, {{"op", "stop"}}, true, request_timeout_limit_ms("stop"));
        } catch (const std::exception &failure) {
#ifdef _WIN32
            const std::string message = failure.what();
            const auto code = message.substr(0, message.find(':'));

            if ((code == "IPC_CONNECT" || code == "IPC_READ_UNKNOWN" ||
                 code == "IPC_WRITE_UNKNOWN" || code == "IPC_TIMEOUT_UNKNOWN") &&
                StoppedAndroidLease::endpoint_absent(record.at("endpoint").get<std::string>())) {
                return recover();
            }
#endif
            throw;
        }

        const auto directory = std::filesystem::path(record.at("directory").get<std::string>());
        const auto end = Clock::now() + std::chrono::seconds(30);

        while (Clock::now() < end) {
            const auto terminal = read_private_json(directory / "runtime.json");

            if (terminal.at("session_id") != record.at("session_id")) {
                throw VoyemuError("TEST_SESSION", "terminal epoch differs");
            }

            if (terminal.value("stopped", false) &&
                StoppedAndroidLease::endpoint_absent(record.at("endpoint").get<std::string>())) {
                const auto lock_path = active_directory / (profile(request) + ".supervisor.lock");

                if (!file_lock_released(lock_path)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }

                const auto deliveries = terminal.at("deliveries"),
                           qnx = deliveries.value("qnx", Json::object());
                const bool cleanup_failed =
                        qnx.value("cleanup", Json::object()).value("status", std::string{}) ==
                        "retained_for_failed_cleanup";
                result["accepted"] = !cleanup_failed;
                result["cleanup"] = {{"verified", !cleanup_failed},
                                     {"supervisor_endpoint_closed", true},
                                     {"supervisor_lock_released", true},
                                     {"guest_shutdown", deliveries},
                                     {"visible_effect_verified", false}};

                return result;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }

        throw VoyemuError("SESSION_STOP_TIMEOUT_UNKNOWN",
                          "own terminal session has not released its endpoint");
    }

    if (op == "session.restart") {
        for (const char *key : {"mode", "boot", "with_cluster", "scenario", "machine_path", "start",
                                "shell", "arguments"}) {
            if (request.contains(key)) {
                throw VoyemuError("TEST_ARGUMENT",
                                  "full restart only accepts its retained machine and cold scope");
            }
        }

        if (!request.contains("expected_boot")) {
            throw VoyemuError("TEST_ARGUMENT", "expected_boot required for full restart");
        }

        validate_restart_expected(request.at("expected_boot"));
        const auto timeout = number(request, "timeout_ms", 180000, 1, 180000);
        Json operation{{"op", "machine_restart"},
                       {"expected_boot", request.at("expected_boot")},
                       {"operation_id", request.at("operation_id")},
                       {"timeout_ms", timeout}};

        if (request.contains("expected_can")) {
            validate_can_boundary_expected(request.at("expected_can"));
            operation["expected_can"] = request.at("expected_can");
        }

        auto result = control(request, operation, true, static_cast<int>(timeout));
        validate_restart_complete(result, request.at("expected_boot"));

        if (request.contains("expected_can")) {
            try {
                validate_can_restart_receipt(result, request.at("expected_boot"),
                                             request.at("expected_can"));
            } catch (...) {
                throw VoyemuError("SESSION_RESTART_UNKNOWN", "CAN cold boundary receipt invalid");
            }
        }

        if (result.value("operation_id", Json()) != request.at("operation_id") ||
            result.value("request_id", Json()) != request.at("request_id")) {
            throw VoyemuError("SESSION_RESTART_UNKNOWN", "restart action identity differs");
        }

        return result;
    }

    if (op == "multimedia.reboot") {
        const auto mode = text(request, "mode");

        if (mode != "warm" && mode != "cold") {
            throw VoyemuError("TEST_ARGUMENT", "reboot mode must be warm or cold");
        }

        const auto timeout = number(request, "timeout_ms", 180000, 1, 180000);

        return transports_.android->reboot(request, mode, static_cast<int>(timeout));
    }

    if (op == "multimedia.power") {
        if (!request.contains("enabled")) {
            throw VoyemuError("TEST_ARGUMENT", "multimedia.power requires enabled");
        }

        const bool enabled = boolean(request, "enabled", false);
        const auto timeout = number(request, "timeout_ms", 90000, 1, 90000);
        auto result = transports_.android->power(request, enabled, static_cast<int>(timeout));
        try {
            const auto &pid = result.at("android_pid");
            const auto &generation = result.at("android_generation");

            if (result.at("enabled") != enabled || result.at("process_alive") != enabled ||
                result.at("verified") != true || result.at("qnx_unchanged") != true ||
                result.at("source") != "supervisor.android_lifecycle" ||
                result.at("visible_effect_verified") != false ||
                !result.at("changed").is_boolean() || !generation.is_number_unsigned() ||
                (enabled ? (generation == 0 || !pid.is_number_integer() || pid <= 1)
                         : !pid.is_null())) {
                throw VoyemuError("ANDROID_POWER_UNKNOWN",
                                  "invalid multimedia power receipt after dispatch");
            }

            if (!enabled && result.at("changed") == true) {
                const auto &shutdown = result.at("shutdown");

                if (shutdown.at("wrapper_terminal") != true ||
                    shutdown.at("ports_released") != true) {
                    throw VoyemuError("ANDROID_POWER_UNKNOWN",
                                      "invalid multimedia shutdown receipt after dispatch");
                }
            }
        } catch (const Json::exception &) {
            throw VoyemuError("ANDROID_POWER_UNKNOWN",
                              "incomplete multimedia power receipt after dispatch");
        }

        result.erase("ok");

        return result;
    }

    if (op == "multimedia.launch") {
        const auto package = text(request, "package", 255),
                   screen = request.contains("screen") ? text(request, "screen")
                                                       : std::string("primary"),
                   boot_id = text(request, "boot_id", 36);
        validate_app_launch(package, screen, boot_id);
        Json app{{"package", package}, {"screen", screen}, {"boot_id", boot_id}};

        if (request.contains("component")) {
            app["component"] = text(request, "component", 512);
        }

        for (const char *field :
             {"screens", "extras", "intent", "arguments", "shell", "expected_generation"}) {
            if (request.contains(field)) {
                throw VoyemuError("TEST_ARGUMENT", "unsupported app launch field");
            }
        }

        return transports_.android->launch(request, app);
    }

    if (op == "cluster.system") {
        const auto timeout = number(request, "timeout_ms", 1000, 1, 30000);
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout);
        const auto started = monotonic_us();
        Json result{{"session_id", request.at("session").at("id")},
                    {"status", "unavailable"},
                    {"complete", false},
                    {"qnx_generation", nullptr},
                    {"processes", Json::array()},
                    {"gauge", nullptr}};
        const auto unavailable = [&](const std::string &reason) {
            result["reason"] = reason;

            return result;
        };
        Json prior;

        for (;;) {
            const auto remaining =
                    std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();

            if (remaining <= 0) {
                return unavailable(prior.is_null() ? "gauge_sample_unavailable"
                                                   : "gauge_heartbeat_not_advanced");
            }

            Json sample;
            try {
                sample = transports_.qnx->system(request, static_cast<int>(remaining));
            } catch (const std::exception &error) {
                const std::string_view message(error.what());

                if (message.starts_with("IPC_OPERATION:")) {
                    return unavailable("supervisor_unsupported");
                }

                if (message.starts_with("IPC_TIMEOUT_UNKNOWN:")) {
                    return unavailable("observation_deadline_elapsed");
                }

                if (message.starts_with("CLUSTER_UI_PROTOCOL:")) {
                    return unavailable("invalid_gauge_sample");
                }

                throw;
            }

            if (Clock::now() >= deadline) {
                return unavailable("observation_deadline_elapsed");
            }

            if (!sample.contains("generation") || !sample.at("generation").is_number_unsigned() ||
                !sample.contains("status") || !sample.at("status").is_string()) {
                throw VoyemuError("CLUSTER_SYSTEM_PROTOCOL", "invalid supervisor observation");
            }

            const auto &generation = sample.at("generation");

            if (!result.at("qnx_generation").is_null() &&
                generation != result.at("qnx_generation")) {
                return unavailable("cluster_generation_changed");
            }

            result["qnx_generation"] = generation;
            const auto &status = sample.at("status");

            if (status == "unavailable") {
                return unavailable(text(sample, "reason", 64));
            }

            if (status == "sample") {
                for (const auto *key : {"owner", "sequence", "guest_pid", "producer_incarnation",
                                        "guest_monotonic_ns", "heartbeat_ns", "node_count"}) {
                    if (!sample.contains(key) || !sample.at(key).is_number_unsigned() ||
                        sample.at(key) == 0) {
                        throw VoyemuError("CLUSTER_SYSTEM_PROTOCOL",
                                          "invalid Gauge sample identity");
                    }
                }

                if (generation == 0 || (sample.at("sequence").get<std::uint64_t>() & 1) ||
                    sample.at("guest_pid") <= 1 || !sample.contains("complete") ||
                    !sample.at("complete").is_boolean()) {
                    throw VoyemuError("CLUSTER_SYSTEM_PROTOCOL", "invalid Gauge diagnostic values");
                }

                if (!prior.is_null()) {
                    for (const auto *key : {"owner", "guest_pid", "producer_incarnation"}) {
                        if (sample.at(key) != prior.at(key)) {
                            return unavailable("gauge_producer_changed");
                        }
                    }

                    if (sample.at("sequence") < prior.at("sequence") ||
                        sample.at("heartbeat_ns") < prior.at("heartbeat_ns") ||
                        sample.at("guest_monotonic_ns") < prior.at("guest_monotonic_ns")) {
                        return unavailable("gauge_sample_regressed");
                    }

                    if (sample.at("heartbeat_ns") > prior.at("heartbeat_ns")) {
                        constexpr auto source = "qnx.kanzi";
                        constexpr auto freshness = "heartbeat_advanced_during_request";
                        result["status"] = "partial";
                        result["processes"].push_back(
                                {{"name", "gauge"},
                                 {"guest_pid", sample.at("guest_pid")},
                                 {"status", "observed"},
                                 {"producer_incarnation", sample.at("producer_incarnation")},
                                 {"source", source},
                                 {"freshness", freshness}});
                        result["gauge"] = {
                                {"source", source},
                                {"freshness", freshness},
                                {"guest_pid", sample.at("guest_pid")},
                                {"producer_incarnation", sample.at("producer_incarnation")},
                                {"sequence", sample.at("sequence")},
                                {"guest_monotonic_ns", sample.at("guest_monotonic_ns")},
                                {"heartbeat_ns", sample.at("heartbeat_ns")},
                                {"node_count", sample.at("node_count")},
                                {"scene_complete", sample.at("complete")},
                                {"host_interval",
                                 {{"clock_domain", "host.steady"},
                                  {"start_us", started},
                                  {"end_us", monotonic_us()}}}};

                        return result;
                    }
                }

                prior = std::move(sample);
            } else if (status != "awaiting") {
                throw VoyemuError("CLUSTER_SYSTEM_PROTOCOL",
                                  "unknown supervisor observation status");
            }

            std::this_thread::sleep_until(
                    std::min(deadline, Clock::now() + std::chrono::milliseconds(75)));
        }
    }

    if (op == "cluster.power" || op == "cluster.reboot") {
        if (!request.contains("expected_generation")) {
            throw VoyemuError("TEST_ARGUMENT", "expected_generation required for QNX lifecycle");
        }

        const bool supported = transports_.qnx->lifecycle_supported(request);

        if ((op == "cluster.reboot" || boolean(request, "enabled", false)) && !supported) {
            throw VoyemuError("TEST_ARGUMENT",
                              "prepared QNX configuration required before power or reboot");
        }

        if (op == "cluster.power") {
            return transports_.qnx->power(request, boolean(request, "enabled", false));
        }

        return transports_.qnx->restart(request);
    }

    return unhandled_operation();
}

Json Api::run(const Json &request) {
    const auto output = artifacts(root_, request);
    Json report{{"schema", 1},
                {"api", "voyemu.test"},
                {"request_id", request.at("request_id")},
                {"operation_id", request.at("operation_id")},
                {"started_utc", utc_now()},
                {"cases", Json::array()},
                {"preflight", {{"status", "not-run"}}},
                {"cleanup", {{"status", "not-run"}}},
                {"scope",
                 {{"scene_assertions", false},
                  {"exported_frames", "only passed capture cases"},
                  {"ack_is_visible_effect", false}}}};
    Json context = request;
    bool created = false, failed = false;
    std::size_t index = 0;

    try {
        const auto budget = number(request, "artifact_budget_bytes", 128 * 1024 * 1024,
                                   16 * 1024 * 1024, 512 * 1024 * 1024);
        report["artifact_budget_bytes"] = budget;
        const auto &cases = request.at("cases");

        if (!cases.is_array() || cases.empty() || cases.size() > 64) {
            throw VoyemuError("TEST_ARGUMENT", "run requires 1..64 cases");
        }

        std::set<std::string> ids;

        for (const auto &item : cases) {
            const auto id = text(item, "id");

            if (!ids.insert(id).second) {
                throw VoyemuError("TEST_ARGUMENT", "duplicate case ID");
            }

            report["cases"].push_back({{"id", id}, {"status", "not-run"}});
        }

        const auto requested_cleanup = request.value(
                "cleanup", std::string(request.contains("start") ? "stop" : "leave_running"));

        if (requested_cleanup != "stop" && requested_cleanup != "leave_running") {
            throw VoyemuError("TEST_ARGUMENT", "cleanup must be stop or leave_running");
        }

        if (request.contains("start") && request.contains("session")) {
            throw VoyemuError("TEST_ARGUMENT", "select create or attach session, not both");
        }

        if (request.contains("start")) {
            auto create = request.at("start");
            create["op"] = "session.start";
            create["request_id"] = request.at("request_id");
            create["ownership_id"] = random_token();
            report["start_attempt"] = {{"profile", profile(create)},
                                       {"ownership_id", create.at("ownership_id")}};

            if (create.contains("machine")) {
                report["start_attempt"]["machine"] = create.at("machine");
            }

            Json running{{"schema", 1},
                         {"operation_id", request.at("operation_id")},
                         {"request_id", request.at("request_id")},
                         {"op", "test.run"},
                         {"status", "running"},
                         {"outcome", "unknown"},
                         {"profile", profile(create)},
                         {"ownership_id", create.at("ownership_id")}};

            if (create.contains("machine")) {
                running["machine"] = create.at("machine");
            }

            write_private_json(root_ / ".local/build/automation/operations" /
                                       (request.at("operation_id").get<std::string>() + ".json"),
                               running);
            auto session = dispatch(create);
            context["profile"] = session.at("profile");
            context["session"] = session.at("session");

            if (create.contains("machine")) {
                context["machine"] = create.at("machine");
            }

            created = true;
            report["session"] = session;
        }

        auto inspect = context;
        inspect["op"] = "profile.get";
        const auto prepared = dispatch(inspect);

        if (!prepared.at("android_prepared").get<bool>()) {
            throw VoyemuError("PROFILE_NOT_PREPARED", "run requires prepared Android profile");
        }

        const auto status = control(context, {{"op", "status"}});
        report["preflight"] = {{"status", "passed"}, {"profile", prepared}, {"session", status}};

        for (index = 0; index < cases.size(); ++index) {
            auto step = normalize_request(cases.at(index), true);
            const auto name = text(step, "op");

            if (name == "test.run" || name == "session.start" || name == "session.stop" ||
                name == "session.restart" || name == "session.attach" || name == "operation.get") {
                throw VoyemuError("TEST_ARGUMENT", "nested lifecycle or run case forbidden");
            }

            step["schema"] = 1;
            step["request_id"] = request.at("request_id").get<std::string>() + ":" +
                                 step.at("id").get<std::string>();
            step["profile"] = context.at("profile");
            step["session"] = context.at("session");

            if (name == "session.screenshot") {
                std::uint64_t used = 0;

                for (const auto &entry : std::filesystem::recursive_directory_iterator(output)) {
                    if (entry.is_regular_file()) {
                        used += entry.file_size();
                    }
                }

                if (used + 16 * 1024 * 1024 > budget) {
                    throw VoyemuError("TEST_ARTIFACT_BUDGET",
                                      "run capture retention bound reached");
                }

                step["artifact_dir"] = (output / ("case-" + std::to_string(index))).string();
            }

            try {
                auto result = dispatch(step);
                const bool timeout = result.is_object() && (result.value("timed_out", false) ||
                                                            !result.value("accepted", true));
                report["cases"][index].update({{"status", timeout ? "failed" : "passed"},
                                               {"request_id", step.at("request_id")},
                                               {"operation_id", request.at("operation_id")},
                                               {"result", result}});

                if (timeout) {
                    failed = true;
                    break;
                }
            } catch (const std::exception &exception) {
                auto failure = error(exception);
                report["cases"][index].update(
                        {{"status",
                          failure.at("code") == "TEST_UNSUPPORTED" ? "unavailable" : "failed"},
                         {"request_id", step.at("request_id")},
                         {"operation_id", request.at("operation_id")},
                         {"error", failure}});
                failed = true;
                break;
            }
        }
    } catch (const std::exception &exception) {
        failed = true;

        if (report.at("preflight").at("status") == "not-run") {
            report["preflight"] = {{"status", "failed"}, {"error", error(exception)}};
        } else if (index < report.at("cases").size()) {
            report["cases"][index].update({{"status", "failed"}, {"error", error(exception)}});
        } else {
            report["error"] = error(exception);
        }
    }

    if (!created && report.contains("start_attempt")) {
        try {
            const auto attempt = report.at("start_attempt");
            auto active = read_private_json(
                    session_active_directory(
                            root_, attempt.at("profile").get<std::string>(),
                            voyemu::machine_identity(attempt.at("profile").get<std::string>(),
                                                     attempt.value("machine", std::string{}))) /
                            (attempt.at("profile").get<std::string>() + ".json"),
                    4096);
            Json candidate{{"profile", attempt.at("profile")},
                           {"session",
                            {{"id", active.at("session_id")},
                             {"ownership_id", attempt.at("ownership_id")}}}};

            if (attempt.contains("machine")) {
                candidate["machine"] = attempt.at("machine");
            }

            session_record(candidate, true);
            context["profile"] = candidate.at("profile");
            context["session"] = candidate.at("session");
            created = true;
            report["reconciled_owned_session"] = candidate;
        } catch (const std::exception &) {
            report["start_reconciliation"] =
                    "No matching active ownership was observed; inspect operation.get before any retry";
        }
    }

    if (failed && context.contains("session")) {
        try {
            const auto record = session_record(context, false);
            const auto logs =
                    read_logs(std::filesystem::path(record.at("directory").get<std::string>()) /
                                      "supervisor.log",
                              Json::object());
            new_json(output / "logs.json", logs);
            report["logs"] = (output / "logs.json").string();
        } catch (const std::exception &exception) {
            report["logs_unavailable"] = error(exception);
        }
    }

    const auto cleanup = request.value("cleanup", std::string(created ? "stop" : "leave_running"));
    const bool create_rejected_before_launch =
            !created && request.contains("start") &&
            report.at("preflight").value("error", Json::object()).value("code", std::string{}) ==
                    "PROFILE_NOT_PREPARED";

    if (create_rejected_before_launch) {
        report["cleanup"] = {
                {"status", "not-required"},
                {"reason",
                 "Prepared-profile validation rejected create before launch; no owned guest was started"}};
    } else if (cleanup == "stop" &&
               (created || context.value("session", Json::object()).contains("ownership_id"))) {
        try {
            auto stop = context;
            stop["op"] = "session.stop";
            auto stopped = dispatch(stop);
            const bool clean = stopped.value("accepted", true);
            report["cleanup"] = {{"status", clean ? "passed" : "failed"}, {"result", stopped}};

            if (!clean) {
                failed = true;
            }
        } catch (const std::exception &exception) {
            failed = true;
            report["cleanup"] = {{"status", "failed"}, {"error", error(exception)}};
        }
    } else {
        report["cleanup"] = {
                {"status", cleanup == "leave_running" ? "not-requested" : "unavailable"},
                {"reason", created ? "explicit leave_running"
                                   : "read-only attach cannot stop another session"}};

        if (cleanup != "leave_running") {
            failed = true;
        }
    }

    report["status"] = failed ? "failed" : "passed";
    report["completed_utc"] = utc_now();
    report["report"] = (output / "report.json").string();
    new_json(output / "report.json", report);
    Json summary{{"status", report.at("status")},
                 {"report", report.at("report")},
                 {"cases", Json::array()},
                 {"preflight_status", report.at("preflight").at("status")},
                 {"cleanup_status", report.at("cleanup").at("status")}};

    for (const auto &item : report.at("cases")) {
        summary["cases"].push_back({{"id", item.at("id")}, {"status", item.at("status")}});
    }

    if (report.contains("session")) {
        summary["session"] = report.at("session");
    }

    if (report.contains("start_attempt")) {
        summary["start_attempt"] = report.at("start_attempt");
    }

    if (report.contains("reconciled_owned_session")) {
        summary["reconciled_owned_session"] = report.at("reconciled_owned_session");
    }

    return summary;
}
} // namespace voyemu::api
