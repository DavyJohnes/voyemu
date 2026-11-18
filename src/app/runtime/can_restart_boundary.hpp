#pragma once
#include "android_reboot.hpp"
#include "can_observation.hpp"
#include "check.hpp"
#include "error.hpp"
#include <charconv>
#include <chrono>
#include <functional>
#include <set>
#include <vector>

namespace voyemu {
inline std::uint64_t can_boundary_decimal(const Json &value, bool positive = false) {
    if (!value.is_string()) {
        throw VoyemuError("CAN_BOUNDARY_ARGUMENT", "canonical decimal string required");
    }

    const auto &s = value.get_ref<const std::string &>();
    std::uint64_t n = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);

    if (s.empty() || (s.size() > 1 && s.front() == '0') || r.ec != std::errc{} ||
        r.ptr != s.data() + s.size() || (positive && !n)) {
        throw VoyemuError("CAN_BOUNDARY_ARGUMENT", "invalid decimal");
    }

    return n;
}

inline void validate_can_boundary_token(const Json &expected) {
    if (!expected.is_object() || expected.size() != 4 || !expected.contains("session") ||
        !expected.contains("epoch") || !expected.contains("nonce") ||
        !expected.contains("ttl_ms") || !expected.at("ttl_ms").is_number_integer() ||
        expected.at("ttl_ms") < 1 || expected.at("ttl_ms") > 180000) {
        throw VoyemuError("CAN_BOUNDARY_ARGUMENT", "exact source/session/epoch/nonce/ttl expected");
    }

    for (const char *k : {"session", "epoch", "nonce"}) {
        (void)can_boundary_decimal(expected.at(k), true);
    }
}

inline Json parse_can_boundary(const std::string &output, int exit_code) {
    if (output.empty() || output.size() > 8192 || output.back() != '\n' ||
        (exit_code != 0 && exit_code != 1)) {
        throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "bounded complete helper object required");
    }

    std::vector<std::set<std::string>> keys;
    auto callback = [&](int, Json::parse_event_t event, Json &value) {
        if (event == Json::parse_event_t::object_start) {
            keys.emplace_back();
        } else if (event == Json::parse_event_t::key) {
            if (keys.empty() || !keys.back().insert(value.get<std::string>()).second) {
                throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "duplicate key");
            }
        } else if (event == Json::parse_event_t::object_end) {
            keys.pop_back();
        }

        return true;
    };
    Json r;
    try {
        r = Json::parse(output, callback);
    } catch (...) {
        throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "malformed helper JSON");
    }

    const std::set<std::string> numeric = {"session",
                                           "epoch",
                                           "nonce",
                                           "kernel_open_seq",
                                           "commit_seq",
                                           "on_transition_count",
                                           "first_commit_boottime_ns",
                                           "fence_seq",
                                           "fence_boottime_ns",
                                           "expires_boottime_ns",
                                           "capture_boottime_ns",
                                           "raw_admission_seq",
                                           "raw_queued",
                                           "raw_inflight_seq",
                                           "loss_count",
                                           "generated_rx_seq",
                                           "fence_generated_rx_seq",
                                           "semantic_queue"};

    if (!r.is_object() || r.size() != numeric.size() + 10 || !r.contains("schema") ||
        !r.at("schema").is_number_integer() || r.value("schema", 0) != 1 ||
        r.value("producer", std::string{}) != "closed_ecu_boundary" || !r.contains("boot_uuid") ||
        !r.at("boot_uuid").is_string() ||
        !android_boot_id_valid(r.at("boot_uuid").get<std::string>())) {
        throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "wrong helper record");
    }

    for (const auto &key : numeric) {
        if (!r.contains(key)) {
            throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "missing counter");
        }

        (void)can_boundary_decimal(r.at(key));
    }

    for (const char *k : {"first_open", "valid", "all_writers_quiesced"}) {
        if (!r.contains(k) || !r.at(k).is_boolean()) {
            throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "boolean proof required");
        }
    }

    for (const char *k : {"status", "mode", "first_mode"}) {
        if (!r.contains(k) || !r.at(k).is_number_integer()) {
            throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "integer status required");
        }
    }

    if (r.at("status") < INT32_MIN || r.at("status") > INT32_MAX || r.at("mode") < 0 ||
        r.at("mode") > 5 || r.at("first_mode") < 0 || r.at("first_mode") > 5 ||
        !r.at("stage").is_string() ||
        !std::set<std::string>{"none", "quiescing", "frozen", "aborted", "expired"}.contains(
                r.at("stage").get<std::string>())) {
        throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "invalid helper stage");
    }

    if (r.at("valid") == true && (r.at("mode") == 0 || r.at("first_mode") == 0)) {
        throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "valid mode domain is 1..5");
    }

    if (exit_code == 0 && !(r.at("status") == 0 && r.at("valid") == true)) {
        throw VoyemuError("CAN_BOUNDARY_PROTOCOL", "contradictory helper exit");
    }

    return r;
}

inline void validate_can_boundary_source(const Json &r, const std::string &boot) {
    if (r.at("status") != 0 || r.at("valid") != true || r.at("boot_uuid") != boot ||
        !can_boundary_decimal(r.at("session"), true) ||
        !can_boundary_decimal(r.at("epoch"), true) ||
        !can_boundary_decimal(r.at("capture_boottime_ns"), true)) {
        throw VoyemuError("CAN_BOUNDARY_STALE", "source unavailable or Linux boot changed");
    }
}

inline void validate_can_boundary_frozen(const Json &r, const Json &expected,
                                         const std::string &boot) {
    validate_can_boundary_token(expected);
    validate_can_boundary_source(r, boot);

    if (r.at("session") != expected.at("session") || r.at("epoch") != expected.at("epoch") ||
        r.at("nonce") != expected.at("nonce") || r.at("stage") != "frozen" || r.at("mode") != 2 ||
        r.at("all_writers_quiesced") != true || r.at("first_open") != true ||
        can_boundary_decimal(r.at("raw_queued")) ||
        can_boundary_decimal(r.at("raw_inflight_seq")) ||
        can_boundary_decimal(r.at("semantic_queue")) || can_boundary_decimal(r.at("loss_count")) ||
        !can_boundary_decimal(r.at("fence_seq"), true) ||
        !can_boundary_decimal(r.at("fence_boottime_ns"), true) ||
        can_boundary_decimal(r.at("capture_boottime_ns")) <
                can_boundary_decimal(r.at("fence_boottime_ns")) ||
        can_boundary_decimal(r.at("capture_boottime_ns")) >=
                can_boundary_decimal(r.at("expires_boottime_ns")) ||
        can_boundary_decimal(r.at("generated_rx_seq")) <
                can_boundary_decimal(r.at("fence_generated_rx_seq")) ||
        r.at("commit_seq") != r.at("fence_seq")) {
        throw VoyemuError("CAN_BOUNDARY_UNVERIFIED", "current exact frozen OFF fence required");
    }
}

inline void validate_can_boundary_unchanged(const Json &r, const Json &retained,
                                            const Json &expected, const std::string &boot) {
    validate_can_boundary_frozen(r, expected, boot);
    validate_can_boundary_frozen(retained, expected, boot);

    for (const char *k :
         {"session", "epoch", "nonce", "kernel_open_seq", "commit_seq", "on_transition_count",
          "first_commit_boottime_ns", "fence_seq", "fence_boottime_ns", "expires_boottime_ns",
          "raw_admission_seq", "raw_queued", "raw_inflight_seq", "loss_count",
          "fence_generated_rx_seq", "semantic_queue", "mode", "first_mode"}) {
        if (r.at(k) != retained.at(k)) {
            throw VoyemuError("CAN_BOUNDARY_STALE", "frozen fence or counters changed");
        }
    }

    if (can_boundary_decimal(r.at("generated_rx_seq")) <
                can_boundary_decimal(retained.at("generated_rx_seq")) ||
        can_boundary_decimal(r.at("capture_boottime_ns")) <
                can_boundary_decimal(retained.at("capture_boottime_ns"))) {
        throw VoyemuError("CAN_BOUNDARY_STALE", "RX/time counter regressed");
    }
}

inline void validate_can_boundary_aborted(const Json &r, const Json &expected,
                                          const std::string &boot) {
    validate_can_boundary_token(expected);
    validate_can_boundary_source(r, boot);

    if (r.at("session") != expected.at("session") ||
        can_boundary_decimal(r.at("epoch")) <= can_boundary_decimal(expected.at("epoch")) ||
        r.at("nonce") != expected.at("nonce") || r.at("stage") != "aborted") {
        throw VoyemuError("CAN_BOUNDARY_UNVERIFIED", "original nonce abort not confirmed");
    }
}

inline void validate_can_boundary_new_boot(const Json &r, const std::string &boot) {
    validate_can_boundary_source(r, boot);

    if (r.at("first_open") != true || r.at("first_mode") != 2 || r.at("mode") != 2 ||
        r.at("stage") != "none" || can_boundary_decimal(r.at("on_transition_count")) ||
        can_boundary_decimal(r.at("loss_count")) ||
        !can_boundary_decimal(r.at("kernel_open_seq"), true) ||
        !can_boundary_decimal(r.at("first_commit_boottime_ns"), true) ||
        can_boundary_decimal(r.at("first_commit_boottime_ns")) >
                can_boundary_decimal(r.at("capture_boottime_ns"))) {
        throw VoyemuError("CAN_BOUNDARY_UNVERIFIED",
                          "new boot first mode and global OFF history required");
    }
}

inline void validate_can_history(const Json &witness, const std::string &boot) {
    validate_can_boundary_source(witness, boot);
    const auto capture = can_boundary_decimal(witness.at("capture_boottime_ns"));
    const auto first = can_boundary_decimal(witness.at("first_commit_boottime_ns"), true);

    if (witness.at("first_open") != true ||
        can_boundary_decimal(witness.at("kernel_open_seq"), true) != 1 ||
        !can_boundary_decimal(witness.at("commit_seq"), true) || first > capture ||
        can_boundary_decimal(witness.at("loss_count")) || witness.at("stage") == "expired" ||
        witness.at("stage") == "quiescing") {
        throw VoyemuError("CAN_BOUNDARY_UNAVAILABLE",
                          "first-open lossless kernel history required");
    }
}

inline void validate_can_history_observation(Json anchor) {
    if (!anchor.is_object() ||
        anchor.value("capability", std::string{}) != "closed_ecu_boundary_v1" ||
        !anchor.contains("owned_android_pid") ||
        !anchor.at("owned_android_pid").is_number_integer() || anchor.at("owned_android_pid") < 2 ||
        !anchor.contains("session_id") || !anchor.at("session_id").is_string() ||
        !anchor.contains("guest_boot_id") || !anchor.at("guest_boot_id").is_string()) {
        throw VoyemuError("CAN_BOUNDARY_ARGUMENT", "exact available owned CAN history required");
    }

    const auto session = anchor.at("session_id").get<std::string>();
    const auto boot = anchor.at("guest_boot_id").get<std::string>();

    if (!hex_text(session, 32)) {
        throw VoyemuError("CAN_BOUNDARY_ARGUMENT", "exact available owned CAN history required");
    }

    for (const char *key : {"capability", "owned_android_pid", "session_id", "guest_boot_id"}) {
        anchor.erase(key);
    }

    validate_can_history(parse_can_boundary(anchor.dump() + "\n", 0), boot);
}

inline void validate_can_boundary_expected(const Json &expected) {
    if (!expected.is_object() || expected.size() != 2 || !expected.contains("anchor") ||
        !expected.contains("native_terminal_boottime_ms")) {
        throw VoyemuError("CAN_BOUNDARY_ARGUMENT",
                          "exact history anchor and native terminal time required");
    }

    const auto &anchor = expected.at("anchor");
    validate_can_history_observation(anchor);

    if (anchor.at("mode") != 2 ||
        (anchor.at("stage") != "none" && anchor.at("stage") != "aborted") ||
        can_boundary_decimal(anchor.at("raw_queued")) ||
        can_boundary_decimal(anchor.at("raw_inflight_seq")) ||
        can_boundary_decimal(anchor.at("semantic_queue")) ||
        can_boundary_decimal(expected.at("native_terminal_boottime_ms"), true) <=
                can_boundary_decimal(anchor.at("capture_boottime_ns")) / 1000000) {
        throw VoyemuError("CAN_BOUNDARY_UNVERIFIED",
                          "native terminal must follow the live OFF anchor");
    }
}

inline void validate_can_boundary_overlap(const Json &witness, const Json &expected,
                                          const std::string &boot) {
    validate_can_history(witness, boot);
    const auto &anchor = expected.at("anchor");

    if (witness.at("mode") != 2 || anchor.at("boot_uuid") != boot ||
        can_boundary_decimal(expected.at("native_terminal_boottime_ms")) >
                can_boundary_decimal(witness.at("capture_boottime_ns")) / 1000000) {
        throw VoyemuError("CAN_BOUNDARY_UNVERIFIED",
                          "OFF history does not overlap the native terminal");
    }

    for (const char *key :
         {"session", "epoch", "kernel_open_seq", "first_mode", "first_commit_boottime_ns",
          "on_transition_count", "loss_count", "raw_admission_seq"}) {
        if (witness.at(key) != anchor.at(key)) {
            throw VoyemuError("CAN_BOUNDARY_STALE",
                              "kernel identity or ON/loss history changed after the anchor");
        }
    }

    for (const char *key : {"capture_boottime_ns", "commit_seq", "generated_rx_seq"}) {
        if (can_boundary_decimal(witness.at(key)) < can_boundary_decimal(anchor.at(key))) {
            throw VoyemuError("CAN_BOUNDARY_STALE", "kernel counter regressed after the anchor");
        }
    }

    if (witness.at("stage") == "frozen" &&
        can_boundary_decimal(expected.at("native_terminal_boottime_ms")) >
                can_boundary_decimal(witness.at("fence_boottime_ns")) / 1000000) {
        throw VoyemuError("CAN_BOUNDARY_UNVERIFIED",
                          "native terminal is later than the kernel fence");
    }
}

inline void validate_can_restart_receipt(const Json &receipt, const Json &boot,
                                         const Json &expected) {
    validate_can_boundary_expected(expected);
    const auto &proof = receipt.at("can_boundary");
    const auto old = boot.at("android").get<std::string>(),
               fresh = receipt.at("after").at("android_boot_id").get<std::string>();
    const auto &sealed = proof.at("prepared");
    const auto &anchor = expected.at("anchor");

    if (proof.at("anchor") != anchor ||
        proof.at("native_terminal_boottime_ms") != expected.at("native_terminal_boottime_ms") ||
        anchor.at("session_id") != receipt.at("session_id") ||
        anchor.at("owned_android_pid") != receipt.at("before").at("android_pid")) {
        throw VoyemuError("CAN_BOUNDARY_STALE",
                          "restart receipt differs from the owned CAN history anchor");
    }

    const Json token = {{"session", anchor.at("session")},
                        {"epoch", anchor.at("epoch")},
                        {"nonce", sealed.at("nonce")},
                        {"ttl_ms", 180000}};
    validate_can_boundary_frozen(sealed, token, old);
    validate_can_boundary_overlap(sealed, expected, old);
    validate_can_boundary_unchanged(proof.at("before_qnx_stop"), sealed, token, old);
    validate_can_boundary_unchanged(proof.at("before_android_stop"), proof.at("before_qnx_stop"),
                                    token, old);
    validate_can_history(proof.at("after"), fresh);
    validate_can_boundary_new_boot(proof.at("after"), fresh);

    if (proof.at("old_kernel_stopped") != true || old == fresh) {
        throw VoyemuError("CAN_BOUNDARY_UNKNOWN", "kernel stop/cold source history not verified");
    }
}

inline bool can_boundary_operation_allowed(const Json &operation) {
    const auto name = operation.at("op").get<std::string>();
    const std::set<std::string> reads = {"status",
                                         "ui_observer",
                                         "ui_observer_exchange",
                                         "media_screen",
                                         "test_output_state",
                                         "test_output_system",
                                         "test_output_provider",
                                         "test_watch_start",
                                         "test_watch_read",
                                         "test_watch_stop",
                                         "test_output_logs",
                                         "test_cluster_ui",
                                         "test_cluster_system",
                                         "test_can_observation",
                                         "test_can_model_observation",
                                         "test_can_history",
                                         "test_boot_identity"};

    if (reads.contains(name) ||
        (name == "phone_control" && operation.value("method", std::string{}) == "snapshot")) {
        return true;
    }

    return operation.contains("ownership_id") && operation.contains("session_id") && name == "stop";
}

inline Json can_boundary_dispatch(const std::string &method,
                                  const std::function<Json(const std::string &)> &collect) {
    try {
        return collect(method);
    } catch (const std::exception &) {
        return collect("restart-status");
    } // Never repeat BEGIN or ABORT after uncertainty.
}

inline Json collect_can_boundary(const std::string &method, const Json &expected,
                                 const CanGuestIdentity &id, const std::string &sha,
                                 const std::string &expected_boot, int timeout_ms,
                                 const CanGuestRunner &runner) {
    if (!std::set<std::string>{"boundary", "prepare-restart", "restart-status", "abort-restart"}
                 .contains(method) ||
        !hex_text(sha, 64) || !hex_text(id.session_id, 32) ||
        (id.resumed && !hex_text(id.cached_boot_nonce, 32)) ||
        !android_boot_id_valid(expected_boot) || id.avd_name.empty() || timeout_ms < 1 ||
        timeout_ms > 15000 || !runner) {
        throw VoyemuError("CAN_BOUNDARY_ARGUMENT", "fixed helper and owned boot required");
    }

    if (method != "boundary") {
        validate_can_boundary_token(expected);
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    auto run = [&](const std::vector<std::string> &args, bool partial = false) {
        const auto left = std::chrono::ceil<std::chrono::milliseconds>(
                                  deadline - std::chrono::steady_clock::now())
                                  .count();

        if (left < 1) {
            throw VoyemuError("CAN_BOUNDARY_TIMEOUT", "source deadline reached");
        }

        auto r = runner(args, static_cast<int>(left));

        if (r.timed_out) {
            throw VoyemuError("CAN_BOUNDARY_TIMEOUT", "fixed helper timed out");
        }

        if (r.exit_code != 0 && !(partial && r.exit_code == 1)) {
            throw VoyemuError("CAN_BOUNDARY_TRANSPORT", "fixed source unavailable");
        }

        return r;
    };
    auto line = [](const std::string &s) {
        auto value = s;

        while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) {
            value.pop_back();
        }

        return value;
    };
    auto nonce = [&] {
        auto s = line(run({"shell", "getprop", "qemu.voyemu.token"}).output);

        if (s != id.session_id && (!id.resumed || s != id.cached_boot_nonce)) {
            throw VoyemuError("CAN_BOUNDARY_STALE", "guest nonce differs");
        }

        return s;
    };
    auto boot = [&] {
        auto b = line(run({"shell", "cat", "/proc/sys/kernel/random/boot_id"}).output);

        if (b != expected_boot) {
            throw VoyemuError("CAN_BOUNDARY_STALE", "Linux boot differs");
        }
    };
    const auto first_nonce = nonce();
    const auto avd = run({"emu", "avd", "name"}).output;

    if (avd != id.avd_name + "\r\nOK\r\n" && avd != id.avd_name + "\nOK\n") {
        throw VoyemuError("CAN_BOUNDARY_STALE", "AVD differs");
    }

    boot();

    if (line(run({"shell", "id", "-u"}).output) != "0") {
        throw VoyemuError("CAN_BOUNDARY_UNAVAILABLE", "owned root required");
    }

    auto hash = [&] {
        const auto out =
                line(run({"shell", "sha256sum", "/system/bin/voyemu-can-model-observer"}).output);

        if (out != sha + "  /system/bin/voyemu-can-model-observer") {
            throw VoyemuError("CAN_BOUNDARY_UNAVAILABLE", "sealed helper differs");
        }
    };
    hash();
    std::vector<std::string> args = {"shell", "/system/bin/voyemu-can-model-observer", method};

    if (method != "boundary") {
        for (const char *key : {"session", "epoch", "nonce"}) {
            args.emplace_back(std::string("--") + key);
            args.emplace_back(expected.at(key).get<std::string>());
        }

        if (method == "prepare-restart") {
            args.emplace_back("--ttl-ms");
            args.emplace_back(std::to_string(expected.at("ttl_ms").get<int>()));
        }
    }

    auto reply = run(args, true);
    auto observed = parse_can_boundary(reply.output, reply.exit_code);
    hash();
    boot();

    if (nonce() != first_nonce || observed.at("boot_uuid") != expected_boot) {
        throw VoyemuError("CAN_BOUNDARY_STALE", "guest changed during fixed helper");
    }

    return observed;
}
} // namespace voyemu
