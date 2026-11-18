#include "error.hpp"
#include "session_internal.hpp"

namespace voyemu {
using namespace session_detail;

void Session::set_media_input(std::function<Json(const Json &)> handler) {
    std::lock_guard lock(mutex_);

    media_input_ = std::move(handler);
}

void Session::set_test_providers(const Json &manifests, const std::string &profile) {
    std::lock_guard lock(mutex_);

    if (test_sources_ || test_providers_ || !manifests.is_array() || manifests.size() > 4 ||
        profile.empty()) {
        throw VoyemuError("PROVIDER_REGISTRATION", "invalid provider registration");
    }

    const auto jobs = directory_ / "provider-jobs";
    voyemu::runtime::ensure_provider_jobs_directory(jobs);
    auto registry = std::make_shared<voyemu::runtime::ProviderRegistry>(jobs);

    for (const auto &manifest : manifests) {
        registry->load(manifest);
    }

    test_provider_profile_ = profile;
    test_providers_ = std::move(registry);
}

std::shared_ptr<UiObserver> Session::test_ui_observer_locked() {
    if (!ui_observer_) {
        const auto specification = android_spec_;
        const auto pid = android_.pid();
        const auto generation = android_generation_;
        ui_observer_ = std::make_shared<UiObserver>(
                directory_, session_id_, specification,
                [this, specification, pid, generation](const auto &args, int timeout) {
                    return execute_android_adb(specification, args, timeout, 128 * 1024, pid,
                                               generation);
                },
                [this, specification, pid, generation](const auto &args) {
                    std::lock_guard lock(mutex_);

                    if (stopped_ || test_services_closed_ || !android_powered_ ||
                        deliveries_["android"].value("status", std::string{}) == "stopping") {
                        throw VoyemuError("TREE_STALE", "Android changed before observer launch");
                    }

                    auto stream =
                            owned_android_adb(specification, pid, generation)->open_shell(args, 0);
                    stream.close_stdin();

                    return UiObserverStream(
                            std::make_shared<AndroidEmbeddedAdb::Stream>(std::move(stream)));
                },
                [this, specification, pid, generation](std::string_view id, int timeout) {
                    std::lock_guard lock(mutex_);
                    auto stream = owned_android_adb(specification, pid, generation)
                                          ->open_observer(id, timeout);

                    return UiObserverStream(
                            std::make_shared<AndroidEmbeddedAdb::Stream>(std::move(stream)));
                });
    }

    return ui_observer_;
}

voyemu::runtime::SourceSample
Session::collect_test_ui(const voyemu::runtime::SourceContext &context) {
    voyemu::runtime::SourceSample unavailable;
    Json specification;
    std::shared_ptr<UiObserver> observer;
    std::int64_t pid;
    std::uint64_t generation;
    unsigned port;
    std::string nonce;
    bool resumed;
    {
        std::lock_guard lock(mutex_);

        if (context.cancelled || stopped_ || test_services_closed_ || !android_powered_ ||
            android_spec_.empty() ||
            deliveries_["android"].value("status", std::string{}) == "stopping" ||
            !android_spec_.contains("ui_observer") || !test_sources_->demanded("ui")) {
            return unavailable;
        }

        specification = android_spec_;
        port = specification.at("port").get<unsigned>();
        pid = android_.pid();
        generation = android_generation_;

        if (!android_.owns_tcp_listener(port) || !android_.owns_tcp_listener(port + 1)) {
            return unavailable;
        }

        nonce = android_boot_nonce_;
        resumed = android_resume_;
        observer = test_ui_observer_locked();
    }
    auto remaining = [&] {
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(
                                  context.deadline - std::chrono::steady_clock::now())
                                  .count();

        if (context.cancelled || left <= 0) {
            throw VoyemuError("TREE_TIMEOUT_UNKNOWN", "UI source cancelled or expired");
        }

        return static_cast<int>(left);
    };
    auto read = [&](std::vector<std::string> args) {
        auto reply = execute_android_adb(specification, args, remaining(), 4096, pid, generation);

        if (reply.exit_code || reply.timed_out) {
            throw VoyemuError("TREE_TRANSPORT_UNKNOWN", "UI identity read failed");
        }

        const auto end = reply.output.find_first_of("\r\n");

        return reply.output.substr(0, end);
    };
    auto owned = [&] {
        std::lock_guard lock(mutex_);

        return !context.cancelled && !stopped_ && !test_services_closed_ && android_powered_ &&
               generation == android_generation_ && pid == android_.pid() &&
               observer == ui_observer_ &&
               deliveries_["android"].value("status", std::string{}) != "stopping" &&
               android_.owns_tcp_listener(port) && android_.owns_tcp_listener(port + 1);
    };
    const auto token = read({"shell", "getprop", "qemu.voyemu.token"});

    if (token != session_id_ && (!resumed || token != nonce)) {
        return unavailable;
    }

    if (read({"emu", "avd", "name"}) != specification.at("avd_name").get<std::string>()) {
        return unavailable;
    }

    const auto boot = read({"shell", "cat", "/proc/sys/kernel/random/boot_id"});

    if (!android_boot_id_valid(boot) || !owned()) {
        return unavailable;
    }

    auto connection = observer->connection(std::min(10000, remaining()), true);

    if (connection.at("boot_id") != boot) {
        return unavailable;
    }

    Json frame, events = Json::array();
    bool gap = false;
    auto exchange = [&](Json request) {
        auto response = observer->exchange(connection, std::move(request), remaining());

        if (!response.value("ok", false)) {
            throw VoyemuError("TREE_PROTOCOL", "observer subscription rejected");
        }

        return response;
    };
    try {
        if (connection != test_ui_connection_ || test_ui_subscription_.empty()) {
            test_ui_connection_ = connection;
            test_ui_subscription_ = Json();
            test_ui_sample_ = unavailable;
            auto subscription =
                    exchange({{"op", "events.subscribe"},
                              {"surfaces", Json::array({"media_primary", "media_secondary"})},
                              {"scope", {{"windows", "all_attached"}}},
                              {"timeout_ms", remaining()}});

            if (!subscription.value("ready", false)) {
                throw VoyemuError("TREE_PROTOCOL", "UI subscription baseline not ready");
            }

            test_ui_subscription_ = {{"id", subscription.at("subscription_id")},
                                     {"cursor", subscription.at("cursor")}};
            frame = subscription.at("baseline");
            gap = subscription.value("gap", false);
        } else {
            auto update = exchange({{"op", "events.read"},
                                    {"subscription_id", test_ui_subscription_.at("id")},
                                    {"cursor", test_ui_subscription_.at("cursor")},
                                    {"timeout_ms", std::min(50, remaining())}});
            events = update.at("events");

            if (!events.is_array() || events.size() > 256) {
                throw VoyemuError("TREE_LIMIT", "UI journal exceeds bound");
            }

            gap = update.value("gap", false);
            test_ui_subscription_["cursor"] = update.at("cursor");

            if (!events.empty() || gap ||
                voyemu::runtime::SourceCoordinator::now_ms() - test_ui_sampled_ms_ >= 250) {
                frame = exchange({{"op", "ui"},
                                  {"surfaces", Json::array({"media_primary", "media_secondary"})},
                                  {"scope", {{"windows", "all_attached"}}},
                                  {"timeout_ms", remaining()}});
            }
        }

        auto hello = exchange({{"op", "hello"}});

        if (hello.at("boot_id") != boot ||
            hello.at("observer_pid") != connection.at("observer_pid")) {
            throw VoyemuError("TREE_STALE", "UI observer changed");
        }

        auto sample = test_ui_sample_;

        if (!frame.is_null()) {
            if (frame.at("boot_id") != boot ||
                frame.at("observer_pid") != connection.at("observer_pid") ||
                frame.at("observer_epoch") != hello.at("observer_epoch")) {
                throw VoyemuError("TREE_STALE", "UI frame incarnation changed");
            }

            sample.value = voyemu::runtime::normalize_ui_observation(std::move(frame));
            sample.value["session_id"] = session_id_;
            sample.value["android_pid"] = pid;
            sample.instance = hello.at("observer_epoch").get<std::string>();
            sample.boot_id = boot;
            sample.revision = sample.value.at("revision").get<std::string>();
            sample.status = "ready";
            test_ui_sampled_ms_ = voyemu::runtime::SourceCoordinator::now_ms();
        }

        if (read({"shell", "cat", "/proc/sys/kernel/random/boot_id"}) != boot || !owned()) {
            throw VoyemuError("TREE_STALE", "Android launch changed during UI read");
        }

        sample.events.clear();

        for (const auto &event : events) {
            sample.events.push_back(event);
        }

        sample.gap = gap;
        sample.continuous = false;
        sample.covered_through_ms = 0;
        sample.value["coverage"] = {{"continuous", false},
                                    {"mode", "sampled_state_with_accessibility_notifications"},
                                    {"gap", gap}};
        sample.value["cursor"] = test_ui_subscription_.at("cursor");
        test_ui_sample_ = sample;

        return sample;
    } catch (...) {
        // Expired/restarted subscriptions are rearmed next collection; no negative assertion
        // survives a gap.
        test_ui_subscription_ = Json();
        test_ui_sample_ = unavailable;
        throw;
    }
}

void Session::ensure_test_services() {
    if (test_services_closed_ || stopped_) {
        throw VoyemuError("TEST_SERVICES_CLOSED", "session is stopping");
    }

    if (test_sources_) {
        return;
    }

    test_watch_ = std::make_shared<voyemu::runtime::WatchHub>();
    test_can_watch_ = std::make_shared<voyemu::runtime::WatchHub>();
    test_sources_ = std::make_shared<voyemu::runtime::SourceCoordinator>(*test_watch_);

    for (const auto &id : {std::string("can"), std::string("system")}) {
        voyemu::runtime::SourceDefinition definition;
        definition.id = id;
        definition.interval = std::chrono::milliseconds(100);
        definition.budget = source_collector_budget(id);
        definition.collect = [this,
                              can = id == "can"](const voyemu::runtime::SourceContext &context) {
            return collect_test_source(can, context);
        };
        test_sources_->add(std::move(definition));
    }

    {
        voyemu::runtime::SourceDefinition definition;
        definition.id = "ui";
        definition.interval = std::chrono::milliseconds(50);
        definition.budget = std::chrono::milliseconds(10000);
        definition.collect = [this](const voyemu::runtime::SourceContext &context) {
            return collect_test_ui(context);
        };
        test_sources_->add(std::move(definition));
    }

    if (test_providers_) {
        for (const auto &provider : test_providers_->capabilities()) {
            const auto id = provider.at("id").get<std::string>();
            voyemu::runtime::SourceDefinition definition;
            definition.id = id;
            definition.interval = std::chrono::milliseconds(250);
            definition.budget = std::chrono::milliseconds(10000);
            definition.collect = [this, id](const voyemu::runtime::SourceContext &context) {
                return collect_test_provider(id, context);
            };
            test_sources_->add(std::move(definition));
        }
    }

    test_sources_->publish_to(*test_can_watch_, {"can"});
    test_source_watches_[{"can"}] = test_can_watch_;
    test_sources_->start();
}

voyemu::runtime::SourceSample
Session::collect_test_provider(const std::string &id,
                               const voyemu::runtime::SourceContext &context) {
    voyemu::runtime::SourceSample unavailable;

    if (context.cancelled || !test_sources_->demanded(id)) {
        return unavailable;
    }

    Json specification;
    std::int64_t pid;
    std::uint64_t generation;
    unsigned port;
    std::string nonce;
    bool resumed;
    {
        std::lock_guard lock(mutex_);

        if (stopped_ || test_services_closed_ || !android_powered_ || android_spec_.empty()) {
            return unavailable;
        }

        specification = android_spec_;
        port = specification.at("port").get<unsigned>();
        pid = android_.pid();
        generation = android_generation_;
        nonce = android_boot_nonce_;
        resumed = android_resume_;
    }
    auto read = [&](const std::vector<std::string> &arguments) {
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(
                                  context.deadline - std::chrono::steady_clock::now())
                                  .count();

        if (context.cancelled || left <= 0) {
            throw VoyemuError("PROVIDER_DEADLINE", "provider deadline exceeded");
        }

        auto r = execute_android_adb(specification, arguments, static_cast<int>(left), 4096, pid,
                                     generation);

        if (r.exit_code || r.timed_out) {
            throw VoyemuError("PROVIDER_GUEST_READ", "guest read failed");
        }

        auto end = r.output.find_first_of("\r\n");

        return r.output.substr(0, end);
    };
    auto owned = [&] {
        std::lock_guard lock(mutex_);

        return !context.cancelled && !stopped_ && !test_services_closed_ && android_powered_ &&
               generation == android_generation_ && pid == android_.pid() &&
               android_.owns_tcp_listener(port) && android_.owns_tcp_listener(port + 1);
    };

    if (!owned()) {
        return unavailable;
    }

    auto token = read({"shell", "getprop", "qemu.voyemu.token"});

    if (token != session_id_ && (!resumed || token != nonce)) {
        return unavailable;
    }

    if (read({"emu", "avd", "name"}) != specification.at("avd_name").get<std::string>()) {
        return unavailable;
    }

    const auto boot = read({"shell", "cat", "/proc/sys/kernel/random/boot_id"});

    if (!std::regex_match(
                boot, std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))) {
        return unavailable;
    }

    auto fence = [&] {
        return owned() && read({"shell", "cat", "/proc/sys/kernel/random/boot_id"}) == boot;
    };
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(context.deadline -
                                                                   std::chrono::steady_clock::now())
                              .count();
    voyemu::runtime::ProviderContext request{random_token(), session_id_, test_provider_profile_,
                                             android_adb_serial(specification), boot};

    return voyemu::runtime::provider_sample(
            test_providers_->collect(id, request, static_cast<int>(std::max<std::int64_t>(0, left)),
                                     voyemu::runtime::run_provider, fence));
}

voyemu::runtime::SourceSample
Session::collect_test_source(bool can, const voyemu::runtime::SourceContext &context) {
    voyemu::runtime::SourceSample sample;

    if (context.cancelled || !test_sources_->demanded(can ? "can" : "system")) {
        return sample;
    }

    Json specification, can_cursor = nullptr;
    std::int64_t pid;
    std::uint64_t generation;
    CanGuestIdentity identity;
    unsigned port;
    {
        // Only copy/fence under state lock; no SDK calls or waiting while holding it.
        std::lock_guard lock(mutex_);

        if (stopped_ || test_services_closed_ || !android_powered_ || android_spec_.empty()) {
            return sample;
        }

        specification = android_spec_;
        port = specification.at("port").get<unsigned>();

        if (!android_.owns_tcp_listener(port) || !android_.owns_tcp_listener(port + 1)) {
            return sample;
        }

        pid = android_.pid();
        generation = android_generation_;

        if (can && test_can_generation_ == generation) {
            can_cursor = test_can_cursor_;
        }

        identity = {session_id_, specification.at("avd_name").get<std::string>(),
                    android_boot_nonce_, android_resume_};
    }
    auto runner = [&](const std::vector<std::string> &arguments, int budget) {
        if (context.cancelled || std::chrono::steady_clock::now() >= context.deadline) {
            throw VoyemuError("TEST_SOURCE_CANCELLED", "source read cancelled");
        }

        auto left = std::chrono::ceil<std::chrono::milliseconds>(context.deadline -
                                                                 std::chrono::steady_clock::now())
                            .count();

        if (left <= 0) {
            throw VoyemuError("TEST_SOURCE_TIMEOUT", "source deadline expired");
        }

        return execute_android_adb(specification, arguments,
                                   std::min<int>(budget, static_cast<int>(left)), 128 * 1024, pid,
                                   generation);
    };
    const auto left = std::chrono::ceil<std::chrono::milliseconds>(context.deadline -
                                                                   std::chrono::steady_clock::now())
                              .count();

    if (left <= 0) {
        return sample;
    }

    Json observation;

    if (can) {
        Json query = {{"schema", 1}, {"limit", 32}};

        if (!can_cursor.is_null()) {
            query["cursor"] = can_cursor;
        }

        observation = collect_can_observation(
                query, identity, static_cast<int>(std::min<std::int64_t>(left, 10000)), runner);

        if (observation.value("status", std::string{}) != "ready") {
            return sample;
        }

        sample.instance = observation.at("source_instance").get<std::string>();
        sample.boot_id = observation.at("guest_boot_id").get<std::string>();
        sample.revision = observation.at("revision").get<std::string>();
        const auto history = observation.at("history");
        sample.gap = !history.at("gap").is_null();
        sample.continuous = !can_cursor.is_null() && !sample.gap &&
                            !history.value("has_more", true);
        sample.covered_through_ms =
                std::stoll(observation.at("host_interval").at("start_ns").get<std::string>()) /
                1000000;

        if (!can_cursor.is_null()) {
            for (const auto &event : observation.at("events")) {
                sample.events.push_back(event);
            }
        }

        sample.value = observation;
    } else {
        // The framed command's minimum budget must respect the collector's absolute deadline.
        const Restore collector_deadline(android_request_deadline_, context.deadline);
        auto read = [&](std::vector<std::string> args) {
            auto result = runner(args, static_cast<int>(left));

            if (result.exit_code || result.timed_out) {
                throw VoyemuError("TEST_SYSTEM_TRANSPORT", "fixed SDK read failed");
            }

            return result.output;
        };
        auto line = [](std::string value) {
            auto end = value.find_first_of("\r\n");

            if (end != std::string::npos) {
                value.resize(end);
            }

            return value;
        };
        const auto token = line(read({"shell", "getprop", "qemu.voyemu.token"}));

        if (token != identity.session_id &&
            (!identity.resumed || token != identity.cached_boot_nonce)) {
            throw VoyemuError("TEST_SYSTEM_STALE", "foreign SDK guest");
        }

        if (line(read({"emu", "avd", "name"})) != identity.avd_name) {
            throw VoyemuError("TEST_SYSTEM_STALE", "foreign AVD");
        }

        const auto boot = line(read({"shell", "cat", "/proc/sys/kernel/random/boot_id"}));

        if (boot.size() != 36 || boot[8] != 45 || boot[13] != 45 || boot[18] != 45 ||
            boot[23] != 45 || boot.find_first_not_of("0123456789abcdef-") != std::string::npos) {
            throw VoyemuError("TEST_SYSTEM_BOOT", "complete boot identity required");
        }

        const auto processes = read({"shell", "ps", "-A", "-o", "PID,PPID,UID,NAME"});
        Json anr;
        try {
            anr = parse_system_anr(read({"shell", "dumpsys", "activity", "lastanr"}));
        } catch (const std::exception &) {
            anr = {{"status", "unavailable"}, {"reason", "fixed Android lastanr read failed"}};
        }

        auto inventory = [&](bool services) {
            try {
                return parse_system_components(read({"shell", "dumpsys", "activity",
                                                     services ? "services" : "activities"}),
                                               services);
            } catch (const std::exception &) {
                return Json{{"status", "unavailable"},
                            {"items", Json::array()},
                            {"complete", false},
                            {"reason", "fixed Android inventory read failed"},
                            {"coverage", "point sample; not continuous"}};
            }
        };
        const auto activities = inventory(false), services = inventory(true);
        Json power;
        try {
            power = parse_system_power(read({"shell", "dumpsys", "power"}));
        } catch (const std::exception &) {
            power = {{"status", "unavailable"},
                     {"complete", false},
                     {"reason", "fixed Android power read failed"}};
        }

        if (line(read({"shell", "cat", "/proc/sys/kernel/random/boot_id"})) != boot) {
            throw VoyemuError("TEST_SYSTEM_STALE", "guest boot changed");
        }

        sample.instance = session_id_ + ":" + std::to_string(generation);
        sample.boot_id = boot;
        sample.revision = sha256_bytes(processes + anr.dump() + activities.dump() +
                                       services.dump() + power.dump());
        sample.continuous = false;
        sample.value = {
                {"boot_id", boot},
                {"owned_android_pid", pid},
                {"processes", parse_system_processes(processes)},
                {"anr", anr},
                {"activities", activities},
                {"services", services},
                {"power", power},
                {"complete", activities.at("complete") == true && services.at("complete") == true &&
                                     anr.at("status") != "unavailable" &&
                                     power.at("complete") == true},
                {"coverage", "point sample; absence is not continuous"},
                {"source",
                 "Android ps -A -o PID,PPID,UID,NAME; dumpsys activity lastanr/activities/services; dumpsys power"}};
    }

    {
        std::lock_guard lock(mutex_);

        if (context.cancelled || stopped_ || test_services_closed_ || !android_powered_ ||
            generation != android_generation_ || pid != android_.pid() ||
            !android_.owns_tcp_listener(port) || !android_.owns_tcp_listener(port + 1)) {
            throw VoyemuError("TEST_SOURCE_STALE", "owned Android launch changed");
        }

        if (can) {
            test_can_cursor_ = observation.at("history").at("next_cursor");
            test_can_generation_ = generation;
        }
    }
    sample.status = can || sample.value.value("complete", false) ? "ready" : "partial";

    return sample;
}
} // namespace voyemu
