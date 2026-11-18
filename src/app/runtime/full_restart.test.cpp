#include "full_restart.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;

int main() try {
    const std::string old = "11111111-1111-4111-8111-111111111111",
                      next = "22222222-2222-4222-8222-222222222222";
    InterguestLink::Status handshake;

    handshake.ready = true;
    handshake.generation = 2;
    handshake.utc_expected = 1700000000;
    handshake.utc_actual = 1700000001;
    // ready only means the host wrote utc_request; until utc_ack it is not a guest acknowledgement.
    assert(!restart_qnx_ack(handshake, 1, 5, 5));
    handshake.utc_complete = true;
    assert(restart_qnx_ack(handshake, 1, 5, 5));
    handshake.utc_status = 1;
    assert(!restart_qnx_ack(handshake, 1, 5, 5));
    handshake.utc_status = 0;
    handshake.utc_actual = handshake.utc_expected - 1;
    assert(!restart_qnx_ack(handshake, 1, 5, 5));
    handshake.utc_actual = handshake.utc_expected + 3;
    assert(!restart_qnx_ack(handshake, 1, 5, 5));
    handshake.utc_actual = handshake.utc_expected;
    assert(!restart_qnx_ack(handshake, 2, 5, 5));
    assert(!restart_qnx_ack(handshake, 1, 5, 4));
    const Json expected{{"android", old}, {"qnx_generation", "4"}, {"qnx_pid", 200}};
    const auto before = restart_boot_identity(old, 4, 200, true),
               after = restart_boot_identity(next, 5, 201, true);

    for (const std::string scenario :
         {"success", "qnx_stop_failure", "android_stop_failure", "partial_android", "partial_qnx",
          "restore", "no_ack", "late_ack"}) {
        std::int64_t now = 0;
        std::vector<std::string> calls;
        std::vector<Json> receipts;
        unsigned reads = 0;
        FullRestartActions actions;
        actions.now_ms = [&] {
            return now;
        };
        actions.wait = [&] {
            now += 10;
        };
        actions.publish = [&](const Json &r) {
            receipts.push_back(r);
        };
        actions.stop_qnx = [&] {
            calls.push_back("stop_qnx");

            if (scenario == "qnx_stop_failure") {
                throw std::runtime_error("flush failed");
            }
        };
        actions.stop_android = [&] {
            calls.push_back("stop_android");

            if (scenario == "android_stop_failure") {
                throw std::runtime_error("Android stop failed");
            }
        };
        actions.start_android = [&] {
            calls.push_back("start_android");
        };
        actions.start_qnx = [&] {
            calls.push_back("start_qnx");
        };
        actions.observe = [&] {
            ++reads;
            auto b = after;

            if (scenario == "partial_android") {
                b["qnx"] = before.at("qnx");
            }

            if (scenario == "partial_qnx") {
                b["android_boot_id"] = old;
            }

            if (scenario == "restore") {
                b["qnx"]["kind"] = "owned_qemu_incarnation";
            }

            if (scenario == "late_ack") {
                now = 101;
            }

            return Json{{"boot", b}, {"guest_acknowledged", scenario != "no_ack"}};
        };
        bool failed = false;
        try {
            const auto result = run_full_restart(expected, before, {{"operation_id", "action"}},
                                                 100, actions);
            validate_restart_complete(result, expected);
        } catch (const std::exception &e) {
            failed = true;
            assert(std::string(e.what()).starts_with("SESSION_RESTART_UNKNOWN:"));
        }

        if (scenario == "success") {
            assert(!failed && calls == std::vector<std::string>({"stop_qnx", "stop_android",
                                                                 "start_android", "start_qnx"}));
            assert(receipts.back().at("status") == "completed");
        } else {
            assert(failed && receipts.back().at("status") == "unknown" &&
                   receipts.back().at("accepted") == false);

            if (scenario == "qnx_stop_failure") {
                assert(calls.size() == 1 && receipts.back().at("stopped").at("qnx") == false);
            }

            if (scenario == "android_stop_failure") {
                assert(calls.size() == 2 && receipts.back().at("stopped").at("qnx") == true &&
                       receipts.back().at("stopped").at("android") == false);
            }
        }

        auto stale = expected;
        stale["android"] = next;
        const auto count = calls.size();
        bool denied = false;
        try {
            (void)run_full_restart(stale, before, Json::object(), 100, actions);
        } catch (...) {
            denied = true;
        }

        assert(denied && calls.size() == count);
    }

    for (const auto invalid : std::vector<Json>{1, "0", "04", "-1", "18446744073709551616"}) {
        auto e = expected;
        e["qnx_generation"] = invalid;
        bool denied = false;
        try {
            validate_restart_expected(e);
        } catch (...) {
            denied = true;
        }

        assert(denied);
    }

    for (const Json result : std::vector<Json>{
                 Json(), Json::object(),
                 Json{{"ok", true}, {"before", before}, {"after", after}, {"status", "completed"}},
                 Json{{"ok", true}, {"before", "invalid"}, {"after", after}}}) {
        bool denied = false;
        try {
            validate_restart_complete(result, expected);
        } catch (const std::exception &e) {
            denied = std::string(e.what()).starts_with("SESSION_RESTART_UNKNOWN:");
        }

        assert(denied);
    }

    std::cout << "full restart guards and phase failure controls passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
