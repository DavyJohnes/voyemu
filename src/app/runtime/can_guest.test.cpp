#include "can_observation.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace voyemu;

int main() try {
    const std::string boot = "aaaaaaaa-1111-2222-3333-444444444444";
    const CanGuestIdentity identity{std::string(32, 'a'), "owned-avd", "", false};
    const Json query{{"schema", 1},
                     {"states", Json::array({"IVI_SOC_MODESET", "HUM_VSP_FUNCTION_SW"})},
                     {"limit", 8}};
    unsigned calls = 0, queries = 0, boots = 0;
    bool foreign = false, changed = false, timed_out = false, bad_boot = false;
    CanGuestRunner runner = [&](const std::vector<std::string> &args, int left) {
        ++calls;
        assert(left > 0 && left <= 1000);

        if (timed_out) {
            return ProcessResult{-1, "", true};
        }

        if (args == std::vector<std::string>{"shell", "getprop", "qemu.voyemu.token"}) {
            return ProcessResult{0, (foreign ? std::string(32, 'b') : identity.session_id) + "\n"};
        }

        if (args == std::vector<std::string>{"emu", "avd", "name"}) {
            return ProcessResult{0, "owned-avd\nOK\n"};
        }

        if (args == std::vector<std::string>{"shell", "cat", "/proc/sys/kernel/random/boot_id"}) {
            ++boots;

            return ProcessResult{0, bad_boot ? "unknown\n"
                                             : (changed && boots % 2 == 0
                                                        ? "bbbbbbbb-1111-2222-3333-444444444444"
                                                        : boot) +
                                                       "\n"};
        }

        assert(args.size() == 2 && args[0] == "shell");
        assert(args[1] ==
               "content call --uri content://com.voyemu.output.vehicle --method observe --arg " +
                       android_shell_quote(query.dump()));
        ++queries;

        return ProcessResult{
                0,
                "Bundle[{json={\"schema\":1,\"source\":\"android:qg.canbus\",\"status\":\"unavailable\",\"reason\":\"service-not-ready\"}}]\n"};
    };
    {
        ProcessResult failure{127,
                              "Error: Could not find class com.android.commands.content.Content\n"};
        bool detailed = false;
        try {
            collect_can_observation(query, identity, 1000, [&](const auto &, int) {
                return failure;
            });
        } catch (const std::runtime_error &e) {
            const std::string message = e.what();
            detailed = message.find("CAN_TRANSPORT_UNKNOWN:") == 0 &&
                       message.find("exit_code=127 timed_out=false") != std::string::npos &&
                       message.find("com.android.commands.content.Content") != std::string::npos;
        }

        assert(detailed);
        const auto diagnostic = can_transport_diagnostic(
                {-1, std::string(2040, 'x') + "private-token\n\033secret", true}, "private-token");
        assert(diagnostic.find("private-token") == std::string::npos);
        assert(diagnostic.find("[redacte") != std::string::npos);
        assert(diagnostic.find("timed_out=true") != std::string::npos);
        assert(diagnostic.find('\n') == std::string::npos &&
               diagnostic.find('\033') == std::string::npos);
        assert(diagnostic.size() <= 2100);
    }
    const auto result = collect_can_observation(query, identity, 1000, runner);

    assert(calls == 5 && queries == 1 && boots == 2 && result.at("status") == "unavailable");
    assert(!result.contains("values") && result.at("guest_boot_id") == boot &&
           result.at("session_id") == identity.session_id);
    {
        unsigned ipk_reads = 0;
        const Json ipk_query = {{"schema", 1}, {"limit", 4}};
        const auto ipk_runner = [&](const std::vector<std::string> &args, int) {
            if (args.size() == 2 && args[1].starts_with("content call")) {
                assert(args[1] == "content call --uri content://com.voyemu.output.vehicle --method "
                                  "ipk-history --arg " +
                                          android_shell_quote(ipk_query.dump()));
                ++ipk_reads;

                return ProcessResult{
                        0, "Bundle[{json={\"schema\":1,\"source\":\"android:qg.canbus:ipk\","
                           "\"status\":\"not-ready\"}}]\n"};
            }

            return runner(args, 1000);
        };
        const auto history = collect_ipk_history(ipk_query, identity, 1000, ipk_runner);
        assert(ipk_reads == 1 && history.at("status") == "not-ready" &&
               history.at("guest_boot_id") == boot &&
               history.at("session_id") == identity.session_id);
        bool rejected = false;

        try {
            collect_ipk_history({{"schema", 1}, {"states", Json::array()}}, identity, 1000,
                                ipk_runner);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).starts_with("IPK_ARGUMENT: ");
        }

        assert(rejected && ipk_reads == 1);
    }
    assert(result.at("host_interval").at("clock_id") == "host.steady:" + identity.session_id &&
           result.at("atomic_guest_state") == false);
    auto rejects = [&](const Json &request, const CanGuestIdentity &guest, int timeout,
                       const char *code) {
        bool rejected = false;
        try {
            collect_can_observation(request, guest, timeout, runner);
        } catch (const std::runtime_error &e) {
            rejected = std::string(e.what()).starts_with(code);
        }

        assert(rejected);
    };
    foreign = true;
    const auto before = queries;
    rejects(query, identity, 1000, "CAN_STALE:");
    assert(queries == before);
    foreign = false;
    changed = true;
    boots = 0;
    rejects(query, identity, 1000, "CAN_STALE:");
    changed = false;
    bad_boot = true;
    const auto before_boot_query = queries;
    rejects(query, identity, 1000, "CAN_TRANSPORT_UNKNOWN:");
    assert(queries == before_boot_query);
    bad_boot = false;
    timed_out = true;
    rejects(query, identity, 1000, "CAN_TIMEOUT_UNKNOWN:");
    timed_out = false;
    auto invalid = query;
    invalid["shell"] = "untrusted";
    const auto before_invalid = calls;
    rejects(invalid, identity, 1000, "CAN_ARGUMENT:");
    assert(calls == before_invalid);
    auto incomplete = identity;
    incomplete.session_id = "";
    rejects(query, incomplete, 1000, "CAN_ARGUMENT:");
    assert(calls == before_invalid);
    rejects(query, identity, 0, "CAN_ARGUMENT:");
    assert(calls == before_invalid);
    auto cached = identity;
    cached.resumed = true;
    cached.cached_boot_nonce = std::string(32, 'b');
    foreign = true;
    boots = 0;
    assert(collect_can_observation(query, cached, 1000, runner).at("session_id") ==
           identity.session_id);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
