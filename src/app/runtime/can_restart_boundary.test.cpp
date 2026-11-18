#include "can_restart_boundary.hpp"
#include "full_restart.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
constexpr char old_boot[] = "11111111-1111-4111-8111-111111111111",
               new_boot[] = "22222222-2222-4222-8222-222222222222";

const Json &boundary_token() {
    static const Json token = {
            {"session", "11"}, {"epoch", "12"}, {"nonce", "13"}, {"ttl_ms", 180000}};

    return token;
}

Json frozen() {
    return {{"schema", 1},
            {"producer", "closed_ecu_boundary"},
            {"status", 0},
            {"boot_uuid", old_boot},
            {"first_open", true},
            {"valid", true},
            {"all_writers_quiesced", true},
            {"stage", "frozen"},
            {"mode", 2},
            {"first_mode", 2},
            {"session", "11"},
            {"epoch", "12"},
            {"nonce", "13"},
            {"kernel_open_seq", "1"},
            {"commit_seq", "4"},
            {"on_transition_count", "2"},
            {"first_commit_boottime_ns", "100"},
            {"fence_seq", "4"},
            {"fence_boottime_ns", "200"},
            {"expires_boottime_ns", "900"},
            {"capture_boottime_ns", "300"},
            {"raw_admission_seq", "5"},
            {"raw_queued", "0"},
            {"raw_inflight_seq", "0"},
            {"loss_count", "0"},
            {"generated_rx_seq", "6"},
            {"fence_generated_rx_seq", "6"},
            {"semantic_queue", "0"}};
}

template <class F> void reject(F f) {
    bool failed = false;

    try {
        f();
    } catch (const std::exception &) {
        failed = true;
    }

    assert(failed);
}

int main() try {
    for (const char *op :
         {"patch", "wheel", "wheel_pair", "android_reboot", "cluster_power", "cluster_restart",
          "media_power", "media_restart", "media_input", "test_app_launch"}) {
        assert(!can_boundary_operation_allowed({{"op", op}}));
    }

    for (const char *op : {"status", "ui_observer", "media_screen", "test_output_system",
                           "test_can_model_observation", "test_can_history"}) {
        assert(can_boundary_operation_allowed({{"op", op}}));
    }

    assert(!can_boundary_operation_allowed({{"op", "stop"}}));
    assert(can_boundary_operation_allowed(
            {{"op", "stop"}, {"session_id", "id"}, {"ownership_id", "owner"}}));
    assert(!can_boundary_operation_allowed({{"op", "phone_control"}, {"method", "incoming"}}));
    assert(can_boundary_operation_allowed({{"op", "phone_control"}, {"method", "snapshot"}}));
    auto r = frozen();
    assert(parse_can_boundary(r.dump() + "\n", 0) == r);

    for (int mode : {4, 5}) {
        auto power = r;
        power["mode"] = mode;
        power["first_mode"] = mode;
        assert(parse_can_boundary(power.dump() + "\n", 0) == power);
        validate_can_boundary_source(power, old_boot);
        reject([&] {
            validate_can_boundary_frozen(power, boundary_token(), old_boot);
        });
    }

    validate_can_boundary_frozen(r, boundary_token(), old_boot);
    auto newer = r;
    newer["capture_boottime_ns"] = "400";
    validate_can_boundary_unchanged(newer, r, boundary_token(), old_boot);
    newer["generated_rx_seq"] = "7";
    validate_can_boundary_unchanged(newer, r, boundary_token(), old_boot);

    for (const auto &patch : std::vector<Json>{{{"nonce", "14"}},
                                               {{"epoch", "13"}},
                                               {{"mode", 1}},
                                               {{"raw_queued", "1"}},
                                               {{"raw_inflight_seq", "1"}},
                                               {{"semantic_queue", "1"}},
                                               {{"loss_count", "1"}},
                                               {{"all_writers_quiesced", false}},
                                               {{"first_open", false}},
                                               {{"stage", "expired"}},
                                               {{"capture_boottime_ns", "900"}},
                                               {{"generated_rx_seq", "5"}},
                                               {{"fence_seq", "0"}}}) {
        auto bad = r;
        bad.update(patch);
        reject([&] {
            validate_can_boundary_frozen(bad, boundary_token(), old_boot);
        });
    }

    for (const char *key : {"commit_seq", "on_transition_count", "kernel_open_seq",
                            "raw_admission_seq", "first_commit_boottime_ns"}) {
        auto bad = newer;
        bad[key] = "99";
        reject([&] {
            validate_can_boundary_unchanged(bad, r, boundary_token(), old_boot);
        });
    }

    auto aborted = r;
    aborted.update({{"stage", "aborted"}, {"all_writers_quiesced", false}, {"epoch", "13"}});
    validate_can_boundary_aborted(aborted, boundary_token(), old_boot);
    auto old_epoch = aborted;
    old_epoch["epoch"] = "12";
    reject([&] {
        validate_can_boundary_aborted(old_epoch, boundary_token(), old_boot);
    });
    auto cold = r;
    cold.update({{"boot_uuid", new_boot},
                 {"session", "21"},
                 {"epoch", "1"},
                 {"nonce", "0"},
                 {"stage", "none"},
                 {"all_writers_quiesced", false},
                 {"on_transition_count", "0"},
                 {"fence_seq", "0"},
                 {"fence_boottime_ns", "0"},
                 {"expires_boottime_ns", "0"}});
    validate_can_boundary_new_boot(cold, new_boot);
    auto bad = cold;
    bad["on_transition_count"] = "1";
    reject([&] {
        validate_can_boundary_new_boot(bad, new_boot);
    });
    bad = cold;
    bad["first_mode"] = 1;
    reject([&] {
        validate_can_boundary_new_boot(bad, new_boot);
    });

    for (const std::string method : {"prepare-restart", "abort-restart"}) {
        std::vector<std::string> calls;
        const auto out = can_boundary_dispatch(method, [&](const std::string &op) -> Json {
            calls.push_back(op);

            if (op == method) {
                throw std::runtime_error("lost");
            }

            return r;
        });
        assert(out == r && (calls == std::vector<std::string>{method, "restart-status"}));
    }

    auto sealed = r;
    sealed.update({{"fence_boottime_ns", "200000000"},
                   {"capture_boottime_ns", "300000000"},
                   {"expires_boottime_ns", "900000000"}});
    const std::string id(32, 'a');
    auto anchor = sealed;
    anchor.update({{"capture_boottime_ns", "150000001"},
                   {"stage", "none"},
                   {"nonce", "0"},
                   {"all_writers_quiesced", false},
                   {"fence_seq", "0"},
                   {"fence_boottime_ns", "0"},
                   {"expires_boottime_ns", "0"},
                   {"fence_generated_rx_seq", "0"},
                   {"session_id", id},
                   {"owned_android_pid", 100},
                   {"guest_boot_id", old_boot},
                   {"capability", "closed_ecu_boundary_v1"}});
    Json expected = {{"anchor", anchor}, {"native_terminal_boottime_ms", "151"}};
    validate_can_boundary_expected(expected);
    validate_can_boundary_overlap(sealed, expected, old_boot);

    for (const auto &patch : std::vector<Json>{{{"raw_queued", "1"}},
                                               {{"raw_inflight_seq", "1"}},
                                               {{"semantic_queue", "1"}},
                                               {{"loss_count", "1"}},
                                               {{"mode", 5}},
                                               {{"first_open", false}},
                                               {{"kernel_open_seq", "2"}},
                                               {{"guest_boot_id", new_boot}},
                                               {{"stage", "quiescing"}}}) {
        auto invalid = expected;
        invalid["anchor"].update(patch);
        reject([&] {
            validate_can_boundary_expected(invalid);
        });
    }

    for (const char *time : {"0", "0151", "150", "201", "18446744073709551616"}) {
        auto invalid = expected;
        invalid["native_terminal_boottime_ms"] = time;
        reject([&] {
            validate_can_boundary_expected(invalid);
            validate_can_boundary_overlap(sealed, invalid, old_boot);
        });
    }

    for (const auto &patch : std::vector<Json>{{{"on_transition_count", "3"}},
                                               {{"epoch", "13"}},
                                               {{"raw_admission_seq", "6"}},
                                               {{"loss_count", "1"}},
                                               {{"generated_rx_seq", "5"}},
                                               {{"commit_seq", "3"}}}) {
        auto invalid = sealed;
        invalid.update(patch);
        reject([&] {
            validate_can_boundary_overlap(invalid, expected, old_boot);
        });
    }

    auto old_tail = sealed;
    old_tail["capture_boottime_ns"] = "400000000";
    const Json boot = {{"android", old_boot}};
    Json receipt = {{"session_id", id},
                    {"before", {{"android_pid", 100}}},
                    {"after", {{"android_boot_id", new_boot}}},
                    {"can_boundary",
                     {{"anchor", anchor},
                      {"native_terminal_boottime_ms", "151"},
                      {"prepared", sealed},
                      {"before_qnx_stop", sealed},
                      {"before_android_stop", old_tail},
                      {"old_kernel_stopped", true},
                      {"after", cold}}}};
    validate_can_restart_receipt(receipt, boot, expected);
    auto invalid = receipt;
    invalid["can_boundary"]["anchor"]["raw_admission_seq"] = "7";
    reject([&] {
        validate_can_restart_receipt(invalid, boot, expected);
    });
    invalid = receipt;
    invalid["can_boundary"]["after"]["kernel_open_seq"] = "2";
    reject([&] {
        validate_can_restart_receipt(invalid, boot, expected);
    });
    invalid = receipt;
    invalid["can_boundary"]["after"]["on_transition_count"] = "1";
    reject([&] {
        validate_can_restart_receipt(invalid, boot, expected);
    });
    invalid = receipt;
    invalid["can_boundary"]["old_kernel_stopped"] = false;
    reject([&] {
        validate_can_restart_receipt(invalid, boot, expected);
    });
    std::vector<std::string> methods;
    const CanGuestIdentity identity{std::string(32, 'a'), "owned-avd", {}, false};
    const auto hash = std::string(64, 'b');
    std::string avd_reply = "owned-avd\r\nOK\r\n";
    int avd_exit = 0;
    bool avd_timeout = false;
    auto runner = [&](const std::vector<std::string> &args, int timeout) -> ProcessResult {
        assert(timeout > 0 && timeout <= 15000);

        if (args == std::vector<std::string>{"shell", "getprop", "qemu.voyemu.token"}) {
            return {0, identity.session_id + "\n", false};
        }

        if (args == std::vector<std::string>{"emu", "avd", "name"}) {
            return {avd_exit, avd_reply, avd_timeout};
        }

        if (args == std::vector<std::string>{"shell", "cat", "/proc/sys/kernel/random/boot_id"}) {
            return {0, std::string(old_boot) + "\n", false};
        }

        if (args == std::vector<std::string>{"shell", "id", "-u"}) {
            return {0, "0\n", false};
        }

        if (args == std::vector<std::string>{"shell", "sha256sum",
                                             "/system/bin/voyemu-can-model-observer"}) {
            return {0, hash + "  /system/bin/voyemu-can-model-observer\n", false};
        }

        assert(args.size() >= 3 && args[0] == "shell" &&
               args[1] == "/system/bin/voyemu-can-model-observer");
        methods.push_back(args[2]);
        assert((args == std::vector<std::string>{"shell", "/system/bin/voyemu-can-model-observer",
                                                 "prepare-restart", "--session", "11", "--epoch",
                                                 "12", "--nonce", "13", "--ttl-ms", "180000"}) ||
               (args == std::vector<std::string>{"shell", "/system/bin/voyemu-can-model-observer",
                                                 "boundary"}));

        return {0, r.dump() + "\n", false};
    };
    assert(collect_can_boundary("prepare-restart", boundary_token(), identity, hash, old_boot,
                                15000, runner) == r);
    assert(methods == std::vector<std::string>{"prepare-restart"});
    methods.clear();
    assert(collect_can_boundary("boundary", Json(), identity, hash, old_boot, 15000, runner) == r);
    assert(methods == std::vector<std::string>{"boundary"});
    reject([&] {
        collect_can_boundary("shell", boundary_token(), identity, hash, old_boot, 15000, runner);
    });

    for (const std::string reply : {"owned-avd\r\nOK\r\n", "owned-avd\nOK\n"}) {
        avd_reply = reply;
        methods.clear();
        assert(collect_can_boundary("boundary", Json(), identity, hash, old_boot, 15000, runner) ==
               r);
        assert(methods == std::vector<std::string>{"boundary"});
    }

    for (const std::string reply :
         {"owned-avd\n", "owned-avd\nOK\nwarning\n", "warning\nowned-avd\nOK\n",
          "foreign-avd\nOK\n", "owned-avd\nFAIL\n", "owned-avd\nOK", "owned-avd\r\nOK\n"}) {
        avd_reply = reply;
        methods.clear();
        reject([&] {
            collect_can_boundary("boundary", Json(), identity, hash, old_boot, 15000, runner);
        });
        assert(methods.empty());
    }

    avd_reply = "owned-avd\r\nOK\r\n";
    avd_exit = 1;
    reject([&] {
        collect_can_boundary("boundary", Json(), identity, hash, old_boot, 15000, runner);
    });
    avd_exit = 0;
    avd_timeout = true;
    reject([&] {
        collect_can_boundary("boundary", Json(), identity, hash, old_boot, 15000, runner);
    });
    avd_timeout = false;
    reject([&] {
        parse_can_boundary(r.dump(), 0);
    });
    auto duplicate = r.dump();
    duplicate.insert(1, "\"status\":5,");
    reject([&] {
        parse_can_boundary(duplicate + "\n", 0);
    });
    bad = r;
    bad["commit_seq"] = 4;
    reject([&] {
        parse_can_boundary(bad.dump() + "\n", 0);
    });
    bad = r;
    bad["first_open"] = 1;
    reject([&] {
        parse_can_boundary(bad.dump() + "\n", 0);
    });
    bad = boundary_token();
    bad["nonce"] = "00";
    reject([&] {
        validate_can_boundary_token(bad);
    });
    bad = boundary_token();
    bad["shell"] = "unexpected";
    reject([&] {
        validate_can_boundary_token(bad);
    });
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
