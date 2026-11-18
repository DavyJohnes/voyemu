#include "can_model_observation.hpp"
#include "check.hpp"
#include "error.hpp"
#include <charconv>
#include <chrono>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
namespace voyemu {
namespace {
[[noreturn]] void invalid(const char *why) {
    throw VoyemuError("CAN_MODEL_ARGUMENT", why);
}

[[noreturn]] void protocol(const char *why) {
    throw VoyemuError("CAN_MODEL_PROTOCOL", why);
}

std::uint64_t decimal(const Json &v) {
    if (!v.is_string()) {
        invalid("decimal string required");
    }

    const auto &s = v.get_ref<const std::string &>();

    if (s.empty() || (s.size() > 1 && s[0] == '0')) {
        invalid("invalid decimal");
    }

    std::uint64_t n = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);

    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) {
        invalid("invalid decimal");
    }

    return n;
}

void fields(const Json &r, const std::set<std::string> &expected) {
    if (!r.is_object() || r.size() != expected.size()) {
        protocol("unexpected record fields");
    }

    for (const auto &[key, value] : r.items()) {
        (void)value;

        if (!expected.contains(key)) {
            protocol("unexpected record field");
        }
    }
}

void record(const Json &r) {
    if (!r.is_object() || !r.contains("type") || !r.at("type").is_string()) {
        protocol("invalid record");
    }
}

void packet(const Json &r, const char *type) {
    fields(r, {"type",
               "status",
               "boot_uuid",
               "first_open",
               "session",
               "epoch",
               "capture_boottime_ns",
               "earliest",
               "latest",
               "seq",
               "event_epoch",
               "event_boottime_ns",
               "kind",
               "state",
               "value",
               "acc",
               "warning",
               "forced_ev",
               "drive",
               "state_boottime_ns",
               "frame_hex",
               "flags"});
    record(r);

    if (r.at("type") != type || !r.contains("boot_uuid") || !r.at("boot_uuid").is_string()) {
        protocol("invalid packet");
    }

    const auto &boot = r.at("boot_uuid").get_ref<const std::string &>();

    if (boot.size() != 36 || boot[8] != '-' || boot[13] != '-' || boot[18] != '-' ||
        boot[23] != '-' || boot.find_first_not_of("0123456789abcdef-") != std::string::npos) {
        protocol("invalid boot");
    }

    for (const char *k : {"session", "epoch", "capture_boottime_ns", "earliest", "latest", "seq",
                          "event_epoch", "event_boottime_ns", "state_boottime_ns"}) {
        decimal(r.at(k));
    }

    auto unsigned32 = [&](const char *k) {
        const auto &v = r.at(k);

        if (!v.is_number_integer() ||
            (v.is_number_unsigned()
                     ? v.get<std::uint64_t>() > UINT32_MAX
                     : (v.get<std::int64_t>() < 0 || v.get<std::int64_t>() > UINT32_MAX))) {
            protocol("invalid unsigned packet number");
        }
    };
    auto signed32 = [&](const char *k) {
        const auto &v = r.at(k);

        if (!v.is_number_integer() ||
            (v.is_number_unsigned()
                     ? v.get<std::uint64_t>() > INT32_MAX
                     : (v.get<std::int64_t>() < INT32_MIN || v.get<std::int64_t>() > INT32_MAX))) {
            protocol("invalid signed packet number");
        }
    };

    for (const char *k : {"first_open", "kind", "flags"}) {
        unsigned32(k);
    }

    for (const char *k : {"status", "state", "value", "acc", "warning", "forced_ev", "drive"}) {
        signed32(k);
    }

    if (r.at("first_open") > 1) {
        protocol("invalid first_open");
    }

    if (!r.at("frame_hex").is_string() ||
        r.at("frame_hex").get_ref<const std::string &>().size() > 20 ||
        r.at("frame_hex").get_ref<const std::string &>().size() % 2 ||
        r.at("frame_hex").get_ref<const std::string &>().find_first_not_of("0123456789abcdef") !=
                std::string::npos) {
        protocol("invalid frame");
    }
}
} // namespace

void validate_can_model_request(const Json &q) {
    if (!q.is_object() || q.dump().size() > 1024) {
        invalid("bounded object required");
    }

    for (const auto &[k, v] : q.items()) {
        (void)v;

        if (k != "schema" && k != "mode" && k != "cursor" && k != "limit") {
            invalid("unknown field");
        }
    }

    if (!q.contains("schema") || !q.at("schema").is_number_integer() || q.at("schema") != 1) {
        invalid("schema 1 required");
    }

    const auto mode = q.value("mode", std::string("snapshot"));

    if (mode != "snapshot" && mode != "drain") {
        invalid("invalid mode");
    }

    if (q.contains("limit") &&
        (!q.at("limit").is_number_integer() || q.at("limit") < 1 || q.at("limit") > 32)) {
        invalid("limit 1..32 required");
    }

    if (mode == "snapshot" && (q.contains("cursor") || q.contains("limit"))) {
        invalid("snapshot has no cursor or limit");
    }

    if (q.contains("cursor")) {
        const auto &c = q.at("cursor");

        if (!c.is_object() || c.size() != 2 || !c.contains("session") ||
            !c.contains("next_cursor") || !decimal(c.at("session")) ||
            !decimal(c.at("next_cursor"))) {
            invalid("invalid cursor");
        }
    }
}

Json parse_can_model_observation(const std::string &output, int exit_code, const Json &q) {
    validate_can_model_request(q);

    if (output.empty() || output.size() > 128 * 1024 || output.back() != '\n') {
        protocol("truncated or oversized output");
    }

    std::istringstream stream(output);
    std::string line;
    Json records = Json::array();

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty() || line.size() > 4096 || records.size() >= 36) {
            protocol("invalid line");
        }

        try {
            std::vector<std::set<std::string>> keys;
            auto callback = [&](int, Json::parse_event_t event, Json &parsed) {
                if (event == Json::parse_event_t::object_start) {
                    keys.emplace_back();
                } else if (event == Json::parse_event_t::key) {
                    if (keys.empty() || !keys.back().insert(parsed.get<std::string>()).second) {
                        protocol("duplicate json key");
                    }
                } else if (event == Json::parse_event_t::object_end) {
                    if (keys.empty()) {
                        protocol("invalid json object");
                    }

                    keys.pop_back();
                }

                return true;
            };
            records.push_back(Json::parse(line, callback));
        } catch (...) {
            protocol("invalid json");
        }
    }

    if (records.empty()) {
        protocol("missing snapshot");
    }

    packet(records[0], "snapshot");
    const auto &snap = records[0];
    const auto session = decimal(snap.at("session")), latest = decimal(snap.at("latest")),
               earliest = decimal(snap.at("earliest"));

    if (!session || !decimal(snap.at("capture_boottime_ns")) || decimal(snap.at("seq")) ||
        snap.at("status") != 0 || ((!latest) != (!earliest)) || (earliest && earliest > latest)) {
        protocol("invalid snapshot");
    }

    const auto mode = q.value("mode", std::string("snapshot"));

    if (mode == "snapshot") {
        if (exit_code || records.size() != 1) {
            protocol("invalid snapshot termination");
        }

        return {{"schema", 1},
                {"source", "android:can_model"},
                {"status", "observed"},
                {"snapshot", snap},
                {"evidence", "snapshot_only"}};
    }

    if (records.size() < 2) {
        protocol("missing termination");
    }

    auto cursor = q.contains("cursor") ? decimal(q.at("cursor").at("next_cursor"))
                                       : (earliest ? earliest : 1);
    const bool wrong_session = q.contains("cursor") &&
                               decimal(q.at("cursor").at("session")) != session;

    if (wrong_session &&
        (records.size() != 2 || records.back().value("type", std::string()) != "error" ||
         records.back().value("reason", std::string()) != "restart_or_session_mismatch")) {
        protocol("session mismatch");
    }

    Json events = Json::array();
    bool gap = false, terminal_gap = false, loss = false;
    Json gap_detail = nullptr;
    const auto limit = q.value("limit", 32);

    for (std::size_t i = 1; i + 1 < records.size(); ++i) {
        const auto &r = records[i];
        record(r);
        const auto t = r.at("type").get<std::string>();

        if (t == "gap" && r.value("reason", std::string()) == "beginning_not_retained" && i == 1 &&
            !q.contains("cursor") && earliest > 1) {
            fields(r, {"type", "reason", "evidence"});

            if (r.at("evidence") != "partial") {
                protocol("invalid gap evidence");
            }

            gap = true;
            gap_detail = r;
            continue;
        }

        if (t == "gap" || t == "unknown") {
            packet(r, t.c_str());

            if ((t == "gap" && (r.at("status") != -2 || decimal(r.at("earliest")) <= cursor)) ||
                (t == "unknown" && (r.at("status") != -3 || decimal(r.at("latest")) >= cursor)) ||
                records.back().value("reason", std::string()) != t || i + 2 != records.size() ||
                r.at("session") != snap.at("session") ||
                r.at("boot_uuid") != snap.at("boot_uuid")) {
                protocol("invalid terminal packet");
            }

            gap = true;
            terminal_gap = true;
            gap_detail = r;
            continue;
        }

        packet(r, "event");

        if (events.size() >= static_cast<std::size_t>(limit) ||
            r.at("session") != snap.at("session") || r.at("boot_uuid") != snap.at("boot_uuid") ||
            decimal(r.at("seq")) != cursor || cursor > latest || r.at("status") != 0 ||
            !decimal(r.at("event_epoch")) || !decimal(r.at("event_boottime_ns")) ||
            r.at("kind") < 1 || r.at("kind") > 8) {
            protocol("invalid event");
        }

        if (cursor == std::numeric_limits<std::uint64_t>::max()) {
            throw VoyemuError("CAN_MODEL_CURSOR", "cursor overflow");
        }

        const auto kind = r.at("kind").get<int>();

        if (r.at("frame_hex").get_ref<const std::string &>().size() !=
            ((kind == 3 || kind == 5) ? 20u : 0u)) {
            protocol("invalid event frame");
        }

        if (kind == 6 || kind == 7) {
            loss = true;
        }

        ++cursor;
        events.push_back(r);
    }

    const auto &end = records.back();
    record(end);
    const auto type = end.at("type").get<std::string>();
    std::string evidence;

    if (type == "end") {
        fields(end, {"type", "session", "next_cursor", "events", "evidence"});

        if ((exit_code != 0 && exit_code != 1) || end.at("session") != snap.at("session") ||
            decimal(end.at("next_cursor")) != cursor ||
            decimal(end.at("events")) != events.size()) {
            protocol("invalid end");
        }

        evidence = end.value("evidence", std::string());

        if (evidence != "partial" && evidence != "bounded_range_only") {
            protocol("invalid evidence");
        }

        if ((exit_code == 0) != (evidence == "bounded_range_only") ||
            ((gap || loss) && exit_code == 0) || (cursor <= latest && exit_code == 0)) {
            protocol("inconsistent evidence");
        }
    } else if (type == "error") {
        fields(end, {"type", "reason", "evidence"});
        const auto why = end.value("reason", std::string());

        if (exit_code != 1 || end.value("evidence", std::string()) != "partial" ||
            (why != "gap" && why != "unknown" && why != "restart" &&
             why != "restart_or_session_mismatch" && why != "endpoint_failure" &&
             why != "time_budget_pending_event" && why != "cursor_overflow")) {
            protocol("invalid error termination");
        }

        if ((why == "gap" || why == "unknown") && !terminal_gap) {
            protocol("missing terminal evidence");
        }

        if (why == "unknown") {
            throw VoyemuError("CAN_MODEL_CURSOR", "unknown cursor");
        }

        if (why == "restart_or_session_mismatch") {
            throw VoyemuError("CAN_MODEL_CURSOR", "model session changed");
        }

        evidence = "partial";
    } else {
        protocol("invalid termination");
    }

    return {{"schema", 1},          {"source", "android:can_model"},
            {"status", "observed"}, {"snapshot", snap},
            {"events", events},     {"terminal", end},
            {"gap", gap_detail},    {"evidence", evidence}};
}
} // namespace voyemu
namespace voyemu {
Json collect_can_model_observation(const Json &request, const CanGuestIdentity &id,
                                   const std::string &sha, int timeout_ms,
                                   const CanGuestRunner &runner) {
    validate_can_model_request(request);

    if (!hex_text(sha, 64) || !hex_text(id.session_id, 32) ||
        (id.resumed && !hex_text(id.cached_boot_nonce, 32)) || id.avd_name.empty() ||
        timeout_ms < 1 || timeout_ms > 10000 || !runner) {
        throw VoyemuError("CAN_MODEL_ARGUMENT", "owned identity and installed hash required");
    }

    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    auto run = [&](const std::vector<std::string> &args, bool partial = false) {
        const auto left =
                std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();

        if (left <= 0) {
            throw VoyemuError("CAN_MODEL_TIMEOUT", "deadline expired");
        }

        auto r = runner(args, static_cast<int>(left));

        if (r.timed_out) {
            throw VoyemuError("CAN_MODEL_TIMEOUT", "source timed out");
        }

        if (r.exit_code != 0 && !(partial && r.exit_code == 1)) {
            throw VoyemuError("CAN_MODEL_TRANSPORT", "fixed source unavailable");
        }

        return r;
    };
    auto line = [](const std::string &s) {
        return s.substr(0, s.find_first_of("\r\n"));
    };
    auto nonce = [&] {
        return line(run({"shell", "getprop", "qemu.voyemu.token"}).output);
    };
    auto boot = [&] {
        auto s = line(run({"shell", "cat", "/proc/sys/kernel/random/boot_id"}).output);

        if (s.size() != 36 || s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-' ||
            s.find_first_not_of("0123456789abcdef-") != std::string::npos) {
            throw VoyemuError("CAN_MODEL_TRANSPORT", "invalid boot");
        }

        return s;
    };
    auto check_nonce = [&](const std::string &n) {
        return n == id.session_id || (id.resumed && n == id.cached_boot_nonce);
    };
    const auto first_nonce = nonce();

    if (!check_nonce(first_nonce) || line(run({"emu", "avd", "name"}).output) != id.avd_name) {
        throw VoyemuError("CAN_MODEL_STALE", "foreign guest");
    }

    const auto first_boot = boot();

    if (line(run({"shell", "id", "-u"}).output) != "0") {
        throw VoyemuError("CAN_MODEL_UNAVAILABLE", "owned root transport required");
    }

    auto hash = [&] {
        const auto out =
                run({"shell", "sha256sum", "/system/bin/voyemu-can-model-observer"}).output;

        if (out != sha + "  /system/bin/voyemu-can-model-observer\n" &&
            out != sha + "  /system/bin/voyemu-can-model-observer\r\n") {
            throw VoyemuError("CAN_MODEL_UNAVAILABLE", "installed helper differs");
        }
    };
    hash();
    const std::vector<std::string> snapshot_args = {
            "shell", "/system/bin/voyemu-can-model-observer", "snapshot"};
    auto before = parse_can_model_observation(run(snapshot_args).output, 0, {{"schema", 1}});
    std::vector<std::string> args = {"shell", "/system/bin/voyemu-can-model-observer"};
    const auto mode = request.value("mode", std::string("snapshot"));
    args.push_back(mode);

    if (mode == "drain") {
        args.push_back("--limit");
        args.push_back(std::to_string(request.value("limit", 32)));

        if (request.contains("cursor")) {
            args.push_back("--session");
            args.push_back(request.at("cursor").at("session").get<std::string>());
            args.push_back("--cursor");
            args.push_back(request.at("cursor").at("next_cursor").get<std::string>());
        }
    }

    auto result = run(args, mode == "drain");
    auto observed = parse_can_model_observation(result.output, result.exit_code, request);
    auto after = parse_can_model_observation(run(snapshot_args).output, 0, {{"schema", 1}});
    hash();
    const auto last_boot = boot(), last_nonce = nonce();
    const auto &a = before.at("snapshot"), &b = observed.at("snapshot"), &c = after.at("snapshot");

    if (first_boot != last_boot || first_nonce != last_nonce || !check_nonce(last_nonce) ||
        a.at("boot_uuid") != first_boot || b.at("boot_uuid") != first_boot ||
        c.at("boot_uuid") != first_boot || a.at("session") != b.at("session") ||
        b.at("session") != c.at("session")) {
        throw VoyemuError("CAN_MODEL_STALE", "guest or model restarted");
    }

    observed["guest_boot_id"] = first_boot;
    observed["session_id"] = id.session_id;
    observed["atomic_guest_state"] = false;
    observed["evidence_scope"] = {{"raw_read", "model_read_not_ioctl_admission"},
                                  {"emit", "rx_attempt_not_receiver_delivery"}};

    return observed;
}
} // namespace voyemu
