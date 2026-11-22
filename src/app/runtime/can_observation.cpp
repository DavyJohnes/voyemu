#include "can_observation.hpp"
#include "check.hpp"
#include "error.hpp"
#include "session.hpp"
#include <charconv>
#include <chrono>
#include <limits>
#include <set>
#include <stdexcept>
namespace voyemu {
namespace {
[[noreturn]] void invalid(const char *reason) {
    throw VoyemuError("CAN_ARGUMENT", reason);
}

std::uint64_t decimal(const Json &value) {
    if (!value.is_string()) {
        invalid("decimal string required");
    }

    const auto text = value.get<std::string>();

    if (text.empty() || (text.size() > 1 && text.front() == '0')) {
        invalid("invalid decimal");
    }

    std::uint64_t result = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);

    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
        result > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        invalid("invalid decimal");
    }

    return result;
}

void identity(const Json &value) {
    if (!value.is_string() || value.get_ref<const std::string &>().empty() ||
        value.get_ref<const std::string &>().size() > 128) {
        invalid("source instance required");
    }
}

void cursor(const Json &value) {
    if (!value.is_object() || value.size() != 2 || !value.contains("source_instance") ||
        !value.contains("after_seq")) {
        invalid("invalid cursor");
    }

    identity(value.at("source_instance"));
    decimal(value.at("after_seq"));
}

void raw(const Json &value) {
    if (!value.is_object() || value.value("status", std::string()) != "observed" ||
        !value.contains("raw") || !value.at("raw").is_number_integer()) {
        invalid("observed raw integer required");
    }

    const auto number = value.at("raw").get<std::int64_t>();

    if (number < std::numeric_limits<std::int32_t>::min() ||
        number > std::numeric_limits<std::int32_t>::max()) {
        invalid("raw integer range");
    }
}
} // namespace

std::string can_transport_diagnostic(const ProcessResult &result, const std::string &secret) {
    std::string output = result.output;

    if (!secret.empty()) {
        std::size_t at = 0;

        while ((at = output.find(secret, at)) != std::string::npos) {
            output.replace(at, secret.size(), "[redacted]");
            at += 10;
        }
    }

    if (output.size() > 2048) {
        output.resize(2048);
    }

    for (auto &ch : output) {
        const auto byte = static_cast<unsigned char>(ch);

        if (byte < 32 || byte >= 127) {
            ch = ' ';
        }
    }

    return "exit_code=" + std::to_string(result.exit_code) +
           " timed_out=" + (result.timed_out ? "true" : "false") + " output=" + output;
}

void validate_can_request(const Json &request) {
    if (!request.is_object() || request.dump().size() > 4096) {
        invalid("bounded object required");
    }

    for (const auto &[key, value] : request.items()) {
        (void)value;

        if (key != "schema" && key != "states" && key != "cursor" && key != "limit") {
            invalid("unknown request field");
        }
    }

    if (!request.contains("schema") || !request.at("schema").is_number_integer() ||
        request.at("schema") != 1) {
        invalid("schema 1 required");
    }

    if (request.contains("limit") && (!request.at("limit").is_number_integer() ||
                                      request.at("limit") < 1 || request.at("limit") > 32)) {
        invalid("limit 1..32 required");
    }

    if (request.contains("states")) {
        const auto &states = request.at("states");

        if (!states.is_array() || states.size() > 32) {
            invalid("bounded states required");
        }

        std::set<std::string> names;

        for (const auto &state : states) {
            if (!state.is_string()) {
                invalid("state name required");
            }

            const auto name = state.get<std::string>();

            if (name.empty() || name.size() > 128 ||
                name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") !=
                        std::string::npos ||
                !names.insert(name).second) {
                invalid("invalid or duplicate state");
            }
        }
    }

    if (request.contains("cursor")) {
        cursor(request.at("cursor"));
    }
}

Json parse_can_observation(const Json &observation) {
    if (!observation.is_object() || observation.dump().size() > 128 * 1024 ||
        !observation.contains("schema") || !observation.at("schema").is_number_integer() ||
        observation.at("schema") != 1 ||
        observation.value("source", std::string()) != "android:qg.canbus") {
        invalid("invalid observation envelope");
    }

    const auto status = observation.value("status", std::string());

    if (status != "ready" && status != "not-ready" && status != "unavailable") {
        invalid("invalid observation status");
    }

    if (!observation.contains("source_instance")) {
        if (status == "ready") {
            invalid("missing identity");
        }

        return observation;
    }

    identity(observation.at("source_instance"));

    if (status == "ready" &&
        (!observation.contains("boot_id") || !observation.at("boot_id").is_string() ||
         observation.at("boot_id").get_ref<const std::string &>().empty())) {
        invalid("missing boot identity");
    }

    if (!observation.contains("pid") || !observation.at("pid").is_number_integer() ||
        observation.at("pid") <= 0) {
        invalid("invalid process identity");
    }

    const auto latest = decimal(observation.at("revision"));
    const auto &clock = observation.at("clock");

    if (clock.value("domain", std::string()) != "android.elapsedRealtimeNanos") {
        invalid("invalid clock domain");
    }

    decimal(clock.at("sampled_ns"));
    decimal(observation.at("service_started_elapsed_ns"));

    if (!observation.at("values").is_object() || observation.at("values").size() > 32) {
        invalid("invalid values");
    }

    for (const auto &[name, value] : observation.at("values").items()) {
        if (!name.starts_with("vehicle_state.")) {
            invalid("invalid state domain");
        }

        if ((name == "vehicle_state.IVI_SOC_MODESET" ||
             name == "vehicle_state.HUM_VSP_FUNCTION_SW") &&
            value.is_object() && value.value("status", std::string()) == "unavailable" &&
            value.value("basis", std::string()) == "ICanBusService.getVehicleState" &&
            value.contains("raw") && value.at("raw").is_number_integer() && value.at("raw") == -1) {
            continue;
        }

        raw(value);
    }

    raw(observation.at("acc"));
    const auto &transport = observation.at("transport");

    if (!transport.is_object() || !transport.at("binder_alive").is_boolean() ||
        transport.value("client_manager_cache", std::string()) != "not_observed") {
        invalid("invalid transport evidence");
    }

    const auto registration = transport.value("registration", std::string());

    if (registration != "pending" && registration != "registered" && registration != "failed") {
        invalid("invalid registration evidence");
    }

    if (status == "ready" &&
        (registration != "registered" || !transport.at("binder_alive").get<bool>())) {
        invalid("ready requires registered live binder");
    }

    const auto &history = observation.at("history");
    cursor(history.at("next_cursor"));

    if (history.at("next_cursor").at("source_instance") != observation.at("source_instance") ||
        decimal(history.at("next_cursor").at("after_seq")) > latest ||
        decimal(history.at("latest_seq")) != latest) {
        invalid("inconsistent cursor");
    }

    const auto oldest = decimal(history.at("oldest_seq"));

    if (oldest == 0 || oldest > latest + 1 || decimal(history.at("dropped_count")) != oldest - 1 ||
        !history.at("has_more").is_boolean()) {
        invalid("invalid history");
    }

    const auto &gap = history.at("gap");

    if (!gap.is_null() && gap != "overflow" && gap != "source_replaced" && gap != "cursor_ahead") {
        invalid("invalid history gap");
    }

    const auto &events = observation.at("events");

    if (!events.is_array() || events.size() > 32) {
        invalid("invalid event batch");
    }

    std::uint64_t previous = 0;

    for (const auto &event : events) {
        const auto seq = decimal(event.at("seq"));

        if (seq <= previous || seq < oldest || seq > latest ||
            event.at("source_instance") != observation.at("source_instance")) {
            invalid("invalid event sequence");
        }

        decimal(event.at("elapsed_ns"));
        decimal(event.at("utc_ms"));

        if (!event.at("kind").is_string()) {
            invalid("invalid event kind");
        }

        if (event.contains("write_id")) {
            decimal(event.at("write_id"));
        }

        previous = seq;
    }

    if (previous && previous != decimal(history.at("next_cursor").at("after_seq"))) {
        invalid("batch cursor mismatch");
    }

    return observation;
}

namespace {
[[noreturn]] void ipk_invalid(const char *reason) {
    throw VoyemuError("IPK_PROTOCOL", reason);
}

bool counter(const Json &object, const char *key) {
    return object.contains(key) && object.at(key).is_number_integer() && object.at(key) >= 0;
}

bool word(const Json &value, std::size_t limit) {
    return value.is_string() && !value.get_ref<const std::string &>().empty() &&
           value.get_ref<const std::string &>().size() <= limit &&
           value.get_ref<const std::string &>().find_first_not_of(
                   "abcdefghijklmnopqrstuvwxyz0123456789_") == std::string::npos;
}

std::uint64_t ipk_decimal(const Json &value) {
    try {
        return decimal(value);
    } catch (const VoyemuError &) {
        ipk_invalid("invalid decimal");
    }
}

// Reads one method of the fixed vehicle provider between two reads of the guest boot identity.
Json collect_provider(const char *method, const Json &query, const CanGuestIdentity &identity,
                      int timeout_ms, const CanGuestRunner &runner, Json (*parse)(const Json &)) {
    if (timeout_ms < 1 || timeout_ms > 10000 || !runner) {
        throw VoyemuError("CAN_ARGUMENT", "bounded read deadline and runner required");
    }

    auto token_valid = [](const std::string &value) {
        return hex_text(value, 32);
    };

    if (!token_valid(identity.session_id) ||
        (identity.resumed && !token_valid(identity.cached_boot_nonce)) ||
        identity.avd_name.empty() || identity.avd_name.size() > 128 ||
        identity.avd_name.find_first_of("\r\n") != std::string::npos) {
        throw VoyemuError("CAN_ARGUMENT", "complete owned guest identity required");
    }

    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now(), deadline = started + std::chrono::milliseconds(timeout_ms);
    auto run = [&](std::vector<std::string> args) {
        const auto now = Clock::now();

        if (now >= deadline) {
            throw VoyemuError("CAN_TIMEOUT_UNKNOWN", "source read deadline expired");
        }

        const auto left = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
        auto result = runner(args, static_cast<int>(left));

        if (result.timed_out) {
            throw VoyemuError("CAN_TIMEOUT_UNKNOWN",
                              "source read timed out; " + can_transport_diagnostic(result));
        }

        if (result.exit_code) {
            throw VoyemuError("CAN_TRANSPORT_UNKNOWN",
                              "fixed source read failed; " + can_transport_diagnostic(result));
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
    const auto token = line(run({"shell", "getprop", "qemu.voyemu.token"}));
    const auto avd = line(run({"emu", "avd", "name"}));

    if (avd != identity.avd_name || (token != identity.session_id &&
                                     (!identity.resumed || token != identity.cached_boot_nonce))) {
        throw VoyemuError("CAN_STALE", "SDK guest identity differs");
    }

    auto boot = [&] {
        auto value = line(run({"shell", "cat", "/proc/sys/kernel/random/boot_id"}));

        if (value.size() != 36 || value[8] != '-' || value[13] != '-' || value[18] != '-' ||
            value[23] != '-' || value.find_first_not_of("0123456789abcdef-") != std::string::npos) {
            throw VoyemuError("CAN_TRANSPORT_UNKNOWN", "complete guest boot identity unavailable");
        }

        return value;
    };
    const auto before_boot = boot();
    const auto command = std::string("content call --uri content://com.voyemu.output.vehicle "
                                     "--method ") +
                         method + " --arg " + android_shell_quote(query.dump());
    auto observed = parse(parse_android_provider_reply(run({"shell", command})));
    const auto after_boot = boot();

    if (before_boot != after_boot ||
        (observed.contains("boot_id") && !observed.at("boot_id").is_null() &&
         observed.at("boot_id") != before_boot)) {
        throw VoyemuError("CAN_STALE", "source read crossed a guest boot");
    }

    if (Clock::now() > deadline) {
        throw VoyemuError("CAN_TIMEOUT_UNKNOWN", "source read deadline expired");
    }

    auto ns = [](Clock::time_point point) {
        return std::to_string(
                std::chrono::duration_cast<std::chrono::nanoseconds>(point.time_since_epoch())
                        .count());
    };
    observed["guest_boot_id"] = before_boot;
    observed["session_id"] = identity.session_id;
    observed["host_interval"] = {{"domain", "host.steady"},
                                 {"clock_id", "host.steady:" + identity.session_id},
                                 {"start_ns", ns(started)},
                                 {"end_ns", ns(Clock::now())}};
    observed["atomic_guest_state"] = false;

    return observed;
}
} // namespace

Json collect_can_observation(const Json &query, const CanGuestIdentity &identity, int timeout_ms,
                             const CanGuestRunner &runner) {
    validate_can_request(query);

    return collect_provider("observe", query, identity, timeout_ms, runner, parse_can_observation);
}

void validate_ipk_request(const Json &request) {
    if (!request.is_object() || request.dump().size() > 1024) {
        throw VoyemuError("IPK_ARGUMENT", "bounded object required");
    }

    for (const auto &[key, value] : request.items()) {
        (void)value;

        if (key != "schema" && key != "cursor" && key != "limit") {
            throw VoyemuError("IPK_ARGUMENT", "IPK history accepts cursor and limit only");
        }
    }

    if (!request.contains("schema") || !request.at("schema").is_number_integer() ||
        request.at("schema") != 1) {
        throw VoyemuError("IPK_ARGUMENT", "schema 1 required");
    }

    if (request.contains("limit") && (!request.at("limit").is_number_integer() ||
                                      request.at("limit") < 1 || request.at("limit") > 256)) {
        throw VoyemuError("IPK_ARGUMENT", "limit 1..256 required");
    }

    if (request.contains("cursor")) {
        try {
            cursor(request.at("cursor"));
        } catch (const VoyemuError &) {
            throw VoyemuError("IPK_ARGUMENT", "invalid cursor");
        }
    }
}

Json parse_ipk_history(const Json &history) {
    if (!history.is_object() || history.dump().size() > 128 * 1024 || !history.contains("schema") ||
        !history.at("schema").is_number_integer() || history.at("schema") != 1 ||
        history.value("source", std::string()) != "android:qg.canbus:ipk") {
        ipk_invalid("invalid history envelope");
    }

    const auto status = history.value("status", std::string());

    if (status == "not-ready") {
        if (history.size() != 3) {
            ipk_invalid("invalid not-ready history");
        }

        return history;
    }

    if (status != "observed" || history.size() != 11) {
        ipk_invalid("invalid history status");
    }

    const auto &instance = history.at("source_instance");

    if (!instance.is_string() || instance.get_ref<const std::string &>().empty() ||
        instance.get_ref<const std::string &>().size() > 128 ||
        !(history.at("boot_id").is_null() || history.at("boot_id").is_string())) {
        ipk_invalid("invalid history identity");
    }

    const auto &link = history.at("link");

    if (!link.is_object() || link.size() != 8 || !word(link.value("state", Json()), 16) ||
        !counter(link, "generation") || !counter(link, "reconnects") ||
        !counter(link, "heartbeats_sent") || !counter(link, "heartbeat_replies") ||
        !counter(link, "malformed") || !word(link.value("last_failure", Json()), 32) ||
        !link.contains("last_reply_age_ms") ||
        !(link.at("last_reply_age_ms").is_null() || counter(link, "last_reply_age_ms"))) {
        ipk_invalid("invalid link state");
    }

    const auto oldest = ipk_decimal(history.at("oldest_seq"));
    const auto latest = ipk_decimal(history.at("latest_seq"));
    const auto &gap = history.at("gap");

    if (oldest > latest + 1 || !(gap.is_null() || gap == "source_replaced" ||
                                 gap == "cursor_ahead" || gap == "overflow")) {
        ipk_invalid("invalid history window");
    }

    const auto &events = history.at("events");

    if (!events.is_array() || events.size() > 256) {
        ipk_invalid("bounded events required");
    }

    std::uint64_t previous = 0;

    for (const auto &event : events) {
        if (!event.is_object() || event.size() != 11) {
            ipk_invalid("invalid event");
        }

        const auto seq = ipk_decimal(event.at("seq"));
        const auto direction = event.value("direction", std::string());
        const auto outcome = event.value("outcome", std::string());
        const auto cmd = event.value("cmd", std::string());

        if (seq <= previous || seq < oldest || seq > latest || !counter(event, "elapsed_ms") ||
            (direction != "sent" && direction != "received") ||
            (direction == "received") != (outcome == "received") ||
            (outcome != "submitted" && outcome != "failed" && outcome != "dropped" &&
             outcome != "received") ||
            cmd.size() != 4 || !cmd.starts_with("0x") ||
            cmd.find_first_not_of("0123456789abcdef", 2) != std::string::npos ||
            !word(event.at("kind"), 32) || !word(event.at("origin"), 32) ||
            !counter(event, "frames") || event.at("frames") < 1 || !counter(event, "bytes") ||
            event.at("bytes") < 9 || !word(event.at("reason"), 32) ||
            !counter(event, "generation")) {
            ipk_invalid("invalid event");
        }

        previous = seq;
    }

    const auto &next = history.at("next_cursor");

    if (!next.is_object() || next.size() != 2 ||
        next.value("source_instance", Json()) != instance ||
        (previous && ipk_decimal(next.at("after_seq")) != previous)) {
        ipk_invalid("invalid next cursor");
    }

    ipk_decimal(next.at("after_seq"));

    return history;
}

Json collect_ipk_history(const Json &query, const CanGuestIdentity &identity, int timeout_ms,
                         const CanGuestRunner &runner) {
    validate_ipk_request(query);

    return collect_provider("ipk-history", query, identity, timeout_ms, runner, parse_ipk_history);
}

namespace {
[[noreturn]] void mode_invalid(const char *reason) {
    throw VoyemuError("MODE_PROTOCOL", reason);
}

std::uint64_t mode_decimal(const Json &value) {
    try {
        return decimal(value);
    } catch (const VoyemuError &) {
        mode_invalid("invalid decimal");
    }
}

bool state_name(const std::string &value) {
    return !value.empty() && value.size() <= 64 &&
           value.find_first_not_of(
                   "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") ==
                   std::string::npos;
}
} // namespace

void validate_mode_request(const Json &request) {
    if (!request.is_object() || request.dump().size() > 1024) {
        throw VoyemuError("MODE_ARGUMENT", "bounded object required");
    }

    for (const auto &[key, value] : request.items()) {
        (void)value;

        if (key != "schema" && key != "cursor" && key != "limit") {
            throw VoyemuError("MODE_ARGUMENT", "mode history accepts cursor and limit only");
        }
    }

    if (!request.contains("schema") || !request.at("schema").is_number_integer() ||
        request.at("schema") != 1) {
        throw VoyemuError("MODE_ARGUMENT", "schema 1 required");
    }

    if (request.contains("limit") && (!request.at("limit").is_number_integer() ||
                                      request.at("limit") < 1 || request.at("limit") > 256)) {
        throw VoyemuError("MODE_ARGUMENT", "limit 1..256 required");
    }

    if (request.contains("cursor")) {
        try {
            cursor(request.at("cursor"));
        } catch (const VoyemuError &) {
            throw VoyemuError("MODE_ARGUMENT", "invalid cursor");
        }
    }
}

Json parse_mode_history(const Json &history) try {
    if (!history.is_object() || history.dump().size() > 128 * 1024 || !history.contains("schema") ||
        !history.at("schema").is_number_integer() || history.at("schema") != 1 ||
        history.value("source", std::string()) != "android:qg.canbus:mode") {
        mode_invalid("invalid history envelope");
    }

    const auto status = history.value("status", std::string());

    if (status == "not-ready") {
        if (history.size() != 3) {
            mode_invalid("invalid not-ready history");
        }

        return history;
    }

    if (status != "observed" || history.size() != 12) {
        mode_invalid("invalid history status");
    }

    const auto &instance = history.at("source_instance");

    if (!instance.is_string() || instance.get_ref<const std::string &>().empty() ||
        instance.get_ref<const std::string &>().size() > 128 ||
        !(history.at("boot_id").is_null() || history.at("boot_id").is_string())) {
        mode_invalid("invalid history identity");
    }

    const auto &complete = history.at("complete");
    const auto &reason = history.at("incomplete_reason");
    const auto no_boot = history.at("boot_id").is_null();

    if (!complete.is_boolean() || no_boot != (reason == "boot_unknown") ||
        (complete == true ? !reason.is_null()
                          : reason != "service_restarted" && reason != "overflow" &&
                                    reason != "boot_unknown" && reason != "boot_record_failed")) {
        mode_invalid("invalid history completeness");
    }

    const auto oldest = mode_decimal(history.at("oldest_seq"));
    const auto latest = mode_decimal(history.at("latest_seq"));
    const auto &gap = history.at("gap");

    if (oldest == 0 || oldest > latest + 1 || (oldest > 1 && complete == true) ||
        !(gap.is_null() || gap == "source_replaced" || gap == "cursor_ahead" ||
          gap == "overflow")) {
        mode_invalid("invalid history window");
    }

    const auto &events = history.at("events");

    if (!events.is_array() || events.size() > 256) {
        mode_invalid("bounded events required");
    }

    std::uint64_t previous = 0;

    for (const auto &event : events) {
        if (!event.is_object() || event.size() != 6) {
            mode_invalid("invalid event");
        }

        const auto seq = mode_decimal(event.at("seq"));
        const auto command = event.value("command", std::string());
        const auto origin = event.value("origin", std::string());
        const auto &value = event.at("value");
        const auto &packet = event.at("packet");

        if (seq <= previous || seq < oldest || seq > latest ||
            (command != "DRIVING_MODE_SET" && command != "IVI_SOC_MODESET") ||
            (origin != "acc_on" && origin != "client") || !value.is_number_integer() ||
            !packet.is_object() || packet.empty() || packet.size() > 32 ||
            !packet.contains(command) || packet.at(command) != value ||
            (command == "IVI_SOC_MODESET" && packet.size() != 1)) {
            mode_invalid("invalid event");
        }

        for (const auto &[name, state] : packet.items()) {
            if (!state_name(name) || !state.is_number_integer()) {
                mode_invalid("invalid event packet");
            }
        }

        mode_decimal(event.at("elapsed_ns"));
        previous = seq;
    }

    const auto &next = history.at("next_cursor");

    if (!next.is_object() || next.size() != 2 ||
        next.value("source_instance", Json()) != instance ||
        (previous && mode_decimal(next.at("after_seq")) != previous)) {
        mode_invalid("invalid next cursor");
    }

    if (mode_decimal(next.at("after_seq")) > latest) {
        mode_invalid("invalid next cursor sequence");
    }

    return history;
} catch (const Json::exception &) {
    mode_invalid("invalid history structure");
}

Json collect_mode_history(const Json &query, const CanGuestIdentity &identity, int timeout_ms,
                          const CanGuestRunner &runner) {
    validate_mode_request(query);

    return collect_provider("mode-history", query, identity, timeout_ms, runner,
                            parse_mode_history);
}
} // namespace voyemu
