#include "can_model_observation.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace voyemu;

int main() try {
    const Json snapshot = {{"type", "snapshot"},
                           {"status", 0},
                           {"boot_uuid", "aaaaaaaa-1111-2222-3333-444444444444"},
                           {"first_open", 1},
                           {"session", "18446744073709551615"},
                           {"epoch", "3"},
                           {"capture_boottime_ns", "12"},
                           {"earliest", "1"},
                           {"latest", "1"},
                           {"seq", "0"},
                           {"event_epoch", "0"},
                           {"event_boottime_ns", "0"},
                           {"kind", 0},
                           {"state", 0},
                           {"value", 0},
                           {"acc", 0},
                           {"warning", 0},
                           {"forced_ev", 0},
                           {"drive", 0},
                           {"state_boottime_ns", "0"},
                           {"frame_hex", ""},
                           {"flags", 0}};
    Json event = snapshot;

    event["type"] = "event";
    event["seq"] = "1";
    event["event_epoch"] = "3";
    event["event_boottime_ns"] = "13";
    event["kind"] = 1;
    Json end = {{"type", "end"},
                {"session", snapshot.at("session")},
                {"next_cursor", "2"},
                {"events", "1"},
                {"evidence", "bounded_range_only"}};
    const Json query = {{"schema", 1}, {"mode", "drain"}, {"limit", 1}};
    const auto output = snapshot.dump() + "\n" + event.dump() + "\n" + end.dump() + "\n";
    assert(parse_can_model_observation(output, 0, query).at("events").size() == 1);
    auto reject = [&](const auto &raw, int code, const Json &q) {
        bool bad = false;
        try {
            parse_can_model_observation(raw, code, q);
        } catch (const std::exception &) {
            bad = true;
        }

        assert(bad);
    };
    auto out_of_range = snapshot;
    out_of_range["flags"] = std::numeric_limits<std::uint64_t>::max();
    reject(out_of_range.dump() + "\n" + event.dump() + "\n" + end.dump() + "\n", 0, query);
    out_of_range = snapshot;
    out_of_range["state"] = -2147483649LL;
    reject(out_of_range.dump() + "\n" + event.dump() + "\n" + end.dump() + "\n", 0, query);
    auto extra = snapshot;
    extra["unknown"] = 1;
    reject(extra.dump() + "\n" + event.dump() + "\n" + end.dump() + "\n", 0, query);
    auto duplicate = snapshot.dump();
    duplicate.insert(1, "\"session\":\"1\",");
    reject(duplicate + "\n" + event.dump() + "\n" + end.dump() + "\n", 0, query);
    auto extra_end = end;
    extra_end["unknown"] = 1;
    reject(snapshot.dump() + "\n" + event.dump() + "\n" + extra_end.dump() + "\n", 0, query);
    reject(output.substr(0, output.size() - 1), 0, query);
    reject(output, 1, query);
    reject(output + "garbage\n", 0, query);
    auto stale = query;
    stale["cursor"] = {{"session", "10"}, {"next_cursor", "1"}};
    Json stale_error = {
            {"type", "error"}, {"reason", "restart_or_session_mismatch"}, {"evidence", "partial"}};
    reject(snapshot.dump() + "\n" + stale_error.dump() + "\n", 1, stale);
    auto retained = snapshot;
    retained["earliest"] = "2";
    retained["latest"] = "2";
    Json initial_gap = {
            {"type", "gap"}, {"reason", "beginning_not_retained"}, {"evidence", "partial"}};
    Json missing_terminal = {{"type", "error"}, {"reason", "gap"}, {"evidence", "partial"}};
    reject(retained.dump() + "\n" + initial_gap.dump() + "\n" + missing_terminal.dump() + "\n", 1,
           query);
    Json gap_ok = snapshot;
    gap_ok["type"] = "gap";
    gap_ok["status"] = -2;
    gap_ok["earliest"] = "2";
    gap_ok["latest"] = "2";
    Json err_gap = {{"type", "error"}, {"reason", "gap"}, {"evidence", "partial"}};
    assert(parse_can_model_observation(
                   snapshot.dump() + "\n" + gap_ok.dump() + "\n" + err_gap.dump() + "\n", 1, query)
                   .at("gap")
                   .at("earliest") == "2");
    Json unknown = snapshot;
    unknown["type"] = "unknown";
    unknown["status"] = -3;
    Json err_unknown = {{"type", "error"}, {"reason", "unknown"}, {"evidence", "partial"}};
    auto ahead = query;
    ahead["cursor"] = {{"session", snapshot.at("session")}, {"next_cursor", "3"}};
    reject(snapshot.dump() + "\n" + unknown.dump() + "\n" + err_unknown.dump() + "\n", 1, ahead);
    Json bad_gap = gap_ok;
    bad_gap["earliest"] = "1";
    reject(snapshot.dump() + "\n" + bad_gap.dump() + "\n" + err_gap.dump() + "\n", 1, query);
    Json gap = snapshot;
    gap["type"] = "gap";
    gap["status"] = -3;
    Json gap_error = {{"type", "error"}, {"reason", "gap"}, {"evidence", "partial"}};
    reject(snapshot.dump() + "\n" + gap.dump() + "\n" + gap_error.dump() + "\n", 1, query);
    auto missing_frame = event;
    missing_frame["kind"] = 3;
    reject(snapshot.dump() + "\n" + missing_frame.dump() + "\n" + end.dump() + "\n", 0, query);
    auto odd = event;
    odd["frame_hex"] = "a";
    reject(snapshot.dump() + "\n" + odd.dump() + "\n" + end.dump() + "\n", 0, query);
    auto bad = event;
    bad["seq"] = "2";
    reject(snapshot.dump() + "\n" + bad.dump() + "\n" + end.dump() + "\n", 0, query);
    bad = event;
    bad["session"] = "1";
    reject(snapshot.dump() + "\n" + bad.dump() + "\n" + end.dump() + "\n", 0, query);
    auto maximum = snapshot;
    maximum["earliest"] = "18446744073709551615";
    maximum["latest"] = "18446744073709551615";
    auto maximum_event = event;
    maximum_event["earliest"] = maximum["earliest"];
    maximum_event["latest"] = maximum["latest"];
    maximum_event["seq"] = maximum["latest"];
    Json overflow = {{"type", "error"}, {"reason", "cursor_overflow"}, {"evidence", "partial"}};
    auto maximum_query = query;
    maximum_query["cursor"] = {{"session", snapshot.at("session")},
                               {"next_cursor", maximum["latest"]}};
    bool overflow_rejected = false;
    try {
        parse_can_model_observation(maximum.dump() + "\n" + maximum_event.dump() + "\n" +
                                            overflow.dump() + "\n",
                                    1, maximum_query);
    } catch (const std::runtime_error &e) {
        overflow_rejected = std::string(e.what()).starts_with("CAN_MODEL_CURSOR: cursor overflow");
    }

    assert(overflow_rejected);
    auto loss = event;
    loss["kind"] = 6;
    reject(snapshot.dump() + "\n" + loss.dump() + "\n" + end.dump() + "\n", 0, query);
    auto partial = end;
    partial["evidence"] = "partial";
    assert(parse_can_model_observation(
                   snapshot.dump() + "\n" + loss.dump() + "\n" + partial.dump() + "\n", 1, query)
                   .at("evidence") == "partial");
    auto invalid = query;
    invalid["cursor"] = {{"session", "18446744073709551616"}, {"next_cursor", "1"}};
    reject(output, 0, invalid);
    invalid = query;
    invalid["limit"] = 33;
    reject(output, 0, invalid);
    invalid = query;
    invalid["command"] = "id";
    reject(output, 0, invalid);
    assert(parse_can_model_observation(snapshot.dump() + "\n", 0, {{"schema", 1}}).at("evidence") ==
           "snapshot_only");
    const CanGuestIdentity identity{std::string(32, 'a'), "test-avd", "", false};
    const std::string sha(64, 'b');
    int calls = 0;
    bool tamper = false, restart = false, invalid_hash_line = false, root_uid = true;
    CanGuestRunner runner = [&](const std::vector<std::string> &args, int) {
        ++calls;

        if (args == std::vector<std::string>{"emu", "avd", "name"}) {
            return ProcessResult{0, "test-avd\nOK\n"};
        }

        if (args == std::vector<std::string>{"shell", "getprop", "qemu.voyemu.token"}) {
            return ProcessResult{0, identity.session_id + "\n"};
        }

        if (args == std::vector<std::string>{"shell", "cat", "/proc/sys/kernel/random/boot_id"}) {
            return ProcessResult{0, snapshot.at("boot_uuid").get<std::string>() + "\n"};
        }

        if (args.size() > 1 && args[1] == "sha256sum") {
            return ProcessResult{
                    0, (tamper ? std::string(64, 'c') : sha) +
                               (invalid_hash_line ? "  /unexpected\n"
                                                  : "  /system/bin/voyemu-can-model-observer\n")};
        }

        if (args == std::vector<std::string>{"shell", "id", "-u"}) {
            return ProcessResult{0, root_uid ? "0\n" : "2000\n"};
        }

        // SDK adbd is already root. Its SELinux policy need not permit executing su again.
        if (args.size() > 1 && args[1] == "su") {
            return ProcessResult{1, "su: Permission denied\n"};
        }

        assert(args.size() >= 3 && args[1] == "/system/bin/voyemu-can-model-observer");

        if (args[2] == "drain") {
            return ProcessResult{0, output};
        }

        auto value = snapshot;

        if (restart && calls > 6) {
            value["session"] = "99";
        }

        return ProcessResult{0, value.dump() + "\n"};
    };
    assert(collect_can_model_observation(query, identity, sha, 10000, runner).at("events").size() ==
           1);
    auto collect_reject = [&] {
        bool denied = false;
        try {
            collect_can_model_observation(query, identity, sha, 10000, runner);
        } catch (const std::exception &) {
            denied = true;
        }

        assert(denied);
    };
    root_uid = false;
    collect_reject();
    root_uid = true;
    tamper = true;
    collect_reject();
    tamper = false;
    invalid_hash_line = true;
    collect_reject();
    invalid_hash_line = false;
    restart = true;
    calls = 0;
    collect_reject();
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
