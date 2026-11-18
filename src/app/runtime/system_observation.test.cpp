#include "system_observation.hpp"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace voyemu;

int main() try {
    const auto awake = parse_system_power(
            "POWER MANAGER (dumpsys power)\n  mWakefulness=Awake\n  mDisplayReady=true\nDisplay Power: state=ON\n  private-address=secret\n");

    assert(awake["status"] == "ready" && awake["wakefulness"] == "Awake" &&
           awake["display_state"] == "ON" && awake["display_ready"] == true &&
           awake.dump().find("secret") == std::string::npos);
    const auto asleep = parse_system_power(
            "POWER MANAGER (dumpsys power)\n  mWakefulness=Asleep\n  mDisplayReady=false\nDisplay Power: state=OFF\n");
    assert(asleep["complete"] == true && asleep["display_ready"] == false &&
           asleep["wakefulness"] == "Asleep");
    assert(parse_system_power("POWER MANAGER (dumpsys power)\n  mWakefulness=Dozing\n")["status"] ==
           "partial");

    for (const auto text :
         {"Permission Denial\n",
          "POWER MANAGER (dumpsys power)\n  mWakefulness=Awake\n  mWakefulness=Asleep\n",
          "POWER MANAGER (dumpsys power)\n  mDisplayReady=yes\n",
          "POWER MANAGER (dumpsys power)\n  mWakefulness=unknown\n"}) {
        assert(parse_system_power(text)["status"] == "unavailable");
    }

    assert(parse_system_power(std::string(128 * 1024, 'x'))["status"] == "unavailable");
    auto rows = parse_system_processes(" PID PPID UID NAME\n1 0 0 init\n21 1 1000 vehicled\n");
    assert(rows.size() == 2 && rows[1]["uid"] == 1000 && rows[1]["name"] == "vehicled");

    for (auto text :
         {"PID NAME\n1 init\n", "PID PPID UID NAME\n", "PID PPID UID NAME\n1 0 root init\n",
          "PID PPID UID NAME\n1 0 0 init\n1 0 0 other\n"}) {
        bool failed = false;
        try {
            parse_system_processes(text);
        } catch (const std::runtime_error &) {
            failed = true;
        }

        assert(failed);
    }

    assert(parse_system_anr(
                   "ACTIVITY MANAGER LAST ANR (dumpsys activity lastanr)\n  <no ANR has occurred>\n")
                   ["status"] == "absent");
    auto anr = parse_system_anr(
            "ACTIVITY MANAGER LAST ANR\nANR in com.module-widget\nPID: 42\nReason: Input dispatching timed out\nTime: 2026-10-15\nSECRET RAW STACK\n");
    assert(anr["status"] == "present" && anr["record"]["pid"] == 42 &&
           anr.dump().find("SECRET") == std::string::npos);
    assert(parse_system_anr("Permission Denial")["status"] == "unknown");
    assert(parse_system_anr("ACTIVITY MANAGER LAST ANR\nANR in partial\n")["status"] == "unknown");
    auto logs = parse_android_logs(
            "--------- beginning of system\n1760000000.123 42 43 W ActivityManager: ANR secret address\n");
    assert(logs["events"].size() == 1 && logs["events"][0]["pid"] == 42 &&
           logs.dump().find("secret") == std::string::npos);
    assert(logs["cursor"].is_null() && logs["gap"].is_null() && !logs["continuous"].get<bool>());
    assert(parse_android_logs("bad line\n")["unparsed_lines"] == 1);

    const auto fatal = parse_android_logs(
            "1760000000.124 84 84 F art: runtime.cc: abort message: Failed to create runtime\n1760000000.125 84 84 F DEBUG: Abort message: JNI FatalError called\n1760000000.126 84 84 W ActivityManager: personal address\n",
            true);
    assert(fatal["events"][0]["message"].get<std::string>().find("Failed to create runtime") !=
           std::string::npos);
    assert(fatal["events"][1]["message"].get<std::string>().find("JNI FatalError") !=
           std::string::npos);
    assert(!fatal["events"][2].contains("message") &&
           fatal.dump().find("personal address") == std::string::npos);
    const auto private_logs = parse_android_logs(
            "1760000000.124 84 84 F art: token=abc password=\"two words\" secret:xyz authorization=Bearer active-private-value\n",
            true, {"active-private-value"});
    const auto sanitized = private_logs.dump();

    for (const auto &secret : {"abc", "two words", "xyz", "Bearer", "active-private-value"}) {
        assert(sanitized.find(secret) == std::string::npos);
    }

    const auto boundary =
            parse_android_logs("1760000000.124 84 84 F art: " + std::string(2040, 'x') +
                                       "active-private-value\t\033\xff\n",
                               true, {"active-private-value"});
    assert(boundary["events"][0]["message"].get_ref<const std::string &>().size() == 2048);
    assert(boundary["events"][0]["message_truncated"] == true &&
           boundary.dump().find("active-private-value") == std::string::npos);
    assert(parse_android_logs("1760000000.124 84 84 F art: hidden abort reason\n")
                   .dump()
                   .find("hidden abort reason") == std::string::npos);

    assert(native_handler_diagnostics_command() ==
           "value=$(logcat -d -b main -b system -b crash -v epoch -m 128 libc:V DEBUG:V crash_dump32:V crash_dump64:V tombstoned:V libdebuggerd_client:V debuggerd:V IPCThreadState:W ProcessState:W '*:S' 2>/dev/null); if [ $? -eq 0 ]; then printf '%s' \"$value\" | head -c 32769; else echo VOYEMU_NATIVE_UNAVAILABLE; fi");
    const auto handler = parse_native_handler_diagnostics(
            "1792247251.123 84 84 W libc: debuggerd handler failed token=private\n");
    assert(handler["source_complete"] == false && handler["selected_text_complete"] == true &&
           handler["clock"] == "android.realtime");
    assert(handler["boot_association"].get<std::string>().starts_with("unknown") &&
           handler["captured_sha256"].get<std::string>().size() == 64 &&
           handler.dump().find("private") == std::string::npos);
    const auto teardown = parse_native_handler_diagnostics(
            "1792247251.122 84 85 W IPCThreadState: Calling IPCThreadState::self() during shutdown is dangerous, expect a crash.\n");
    assert(teardown["status"] == "ready" && teardown["source_complete"] == false);
    assert(teardown["excerpt"].get<std::string>().find("during shutdown is dangerous") !=
           std::string::npos);
    assert(parse_native_handler_diagnostics(std::string(32769, 'x'))["excerpt_truncated"] == true);
    assert(parse_native_handler_diagnostics("VOYEMU_NATIVE_UNAVAILABLE\n")["status"] ==
           "unavailable");
    const auto native = parse_native_prefix(
            "Timestamp: 2026-10-17 10:20:30+0300\npid: 16957, tid: 16958, name: Binder\nbacktrace:\n #00 pc 123 libc.so\ntoken=private\n");
    assert(native["pid"] == 16957 && native["timestamp"] == "2026-10-17 10:20:30+0300" &&
           native["source_complete"] == true);
    assert(native["captured_sha256"].get<std::string>().size() == 64 &&
           native.dump().find("private") == std::string::npos);
    const auto native_long = parse_native_prefix(std::string(32769, 'x'));
    assert(native_long["source_complete"] == false && native_long["excerpt_truncated"] == true &&
           native_long["captured_bytes"] == 32768);
    assert(parse_native_prefix(std::string(32768, 'x'))["source_complete"] == true);
    const auto native_files = parse_native_inventory(
            "-rw------- 1 root root 123 2026-10-17 10:00 tombstone_00\nlrwxrwxrwx 1 root root 1 now tombstone_01 -> /private\n-rw------- 1 root root 1 now ../../secret\n");
    assert(native_files["files"].size() == 1 &&
           native_files["files"][0]["basename"] == "tombstone_00");
    bool native_large = false;
    try {
        parse_native_prefix(std::string(32770, 'x'));
    } catch (const std::runtime_error &) {
        native_large = true;
    }

    assert(native_large);
    const auto diagnostic = parse_anr_diagnostics(
            "ACTIVITY MANAGER LAST ANR\nANR in partial token=abc password=\"two words\"\n", {});
    assert(diagnostic["summary"]["status"] == "unknown" && diagnostic["source"] == "anr");
    assert(diagnostic.dump().find("abc") == std::string::npos &&
           diagnostic.dump().find("two words") == std::string::npos);
    assert(diagnostic["source_sha256"].get<std::string>().size() == 64 &&
           !diagnostic["excerpt_truncated"].get<bool>());
    const auto long_anr = parse_anr_diagnostics(std::string(16380, 'x') + "active-private-value\n",
                                                {"active-private-value"});
    assert(long_anr["excerpt"].get_ref<const std::string &>().size() == 16384 &&
           long_anr["excerpt_truncated"] == true);
    assert(long_anr.dump().find("active-private-value") == std::string::npos);
    assert(parse_anr_diagnostics(
                   "ACTIVITY MANAGER LAST ANR\n<no ANR has occurred>\n")["summary"]["status"] ==
           "absent");
    assert(parse_anr_diagnostics(
                   "ACTIVITY MANAGER LAST ANR\nANR in package\nReason: timeout\n")["summary"]
                                                                                  ["status"] ==
           "present");
    bool oversized = false;
    try {
        parse_anr_diagnostics(std::string(128 * 1024 + 1, 'x'));
    } catch (const std::runtime_error &) {
        oversized = true;
    }

    assert(oversized);

    assert(parse_anr_diagnostics("Authorization: Bearer private-bearer-value\n")
                   .dump()
                   .find("private-bearer-value") == std::string::npos);

    assert(parse_system_anr(
                   "ACTIVITY MANAGER LAST ANR (dumpsys activity lastanr)\n  <no ANR has occurred since boot>\n")
                   ["status"] == "absent");
    const auto inventory = parse_anr_trace_inventory(
            "-rw------- 1 system system 20 2026-10-15 01:00 anr_2026-10-15-01-00-00-001\n-rw------- 1 system system 20 2026-10-15 02:00 anr_2026-10-15-02-00-00-001\nlrwxrwxrwx 1 root root 2 2026-10-15 03:00 evil -> /secret\n-rw------- 1 root root 2 2026-10-15 03:00 ../../secret\n-rw------- 1 root root 2 2026-10-15 03:00 traces.txt\n");
    assert(inventory["files"].size() == 3 &&
           inventory["selected"] == "anr_2026-10-15-02-00-00-001" &&
           inventory["boot_association"] == "unknown");
    assert(parse_anr_trace_inventory(
                   "-rw------- 1 root root 1 2026-10-15 00:00 traces.txt\n")["selected"] ==
           "traces.txt");
    assert(parse_anr_trace_inventory("total 0\n")["selected"].is_null());
    {
        const std::string bearer = "private-sdk-bearer";
        unsigned reads = 0;
        const auto snapshot = collect_retained_android_diagnostics(
                "anr", "", {bearer}, [&](std::vector<std::string> args) {
                    switch (++reads) {
                    case 1:
                        assert((args == std::vector<std::string>{"shell", "dumpsys", "activity",
                                                                 "lastanr"}));

                        return "ACTIVITY MANAGER LAST ANR\n<no ANR has occurred>\n";
                    case 2:
                        assert((args ==
                                std::vector<std::string>{"shell", "ls", "-l", "/data/anr"}));

                        return "-rw------- 1 root root 10 now traces.txt\n";
                    default:
                        assert((args == std::vector<std::string>{"shell", "head", "-c", "131071",
                                                                 "/data/anr/traces.txt"}));

                        return "private-sdk-bearer\n";
                    }
                });
        assert(reads == 3 && snapshot.at("traces").at("prefix").at("source_complete") == false &&
               snapshot.dump().find(bearer) == std::string::npos);
        const auto tombstone = diagnostic_tombstone({{"tombstone", "tombstone_09"}}, "native");
        reads = 0;
        const auto crashes = collect_retained_android_diagnostics(
                "native", tombstone, {bearer}, [&](std::vector<std::string> args) {
                    assert(args.size() == 2 && args[0] == "shell");

                    switch (++reads) {
                    case 1:
                        assert(args[1].starts_with("value=$(logcat -d -b crash "));

                        return "private-sdk-bearer\n";
                    case 2:
                        assert(args[1] == native_handler_diagnostics_command());

                        return "VOYEMU_NATIVE_UNAVAILABLE\n";
                    case 3:
                        assert(args[1].starts_with("value=$(ls -l /data/tombstones "));

                        return "VOYEMU_NATIVE_UNAVAILABLE\n";
                    default:
                        assert(args[1].find("head -c 32769 /data/tombstones/tombstone_09") !=
                               std::string::npos);

                        return "VOYEMU_NATIVE_UNAVAILABLE\n";
                    }
                });
        assert(reads == 4 && crashes.at("tombstone").at("basename") == tombstone &&
               crashes.dump().find(bearer) == std::string::npos);
    }
    const auto activities = parse_system_components(
            "ACTIVITY MANAGER ACTIVITIES (dumpsys activity activities)\n  * Hist #0: ActivityRecord{aabb u0 com.qinggan.setting/.MainActivity t12}\n    Intent { private extras }\n  mResumedActivity: ActivityRecord{aabb u0 com.qinggan.setting/.MainActivity t12}\n",
            false);
    assert(activities["status"] == "ready" && activities["complete"] == true &&
           activities["items"].size() == 1);
    assert(activities["items"][0]["component"] == "com.qinggan.setting/.MainActivity" &&
           activities.dump().find("private") == std::string::npos);
    const auto services = parse_system_components(
            "ACTIVITY MANAGER SERVICES (dumpsys activity services)\n  * ServiceRecord{12ab u10 com.example/.Service$Inner}\n",
            true);
    assert(services["status"] == "ready" && services["items"][0]["user"] == 10);
    assert(parse_system_components("ACTIVITY MANAGER SERVICES\n  (nothing)\n", true)["complete"] ==
           true);

    for (const auto &text :
         {"Permission Denial\n", "ACTIVITY MANAGER SERVICES\n",
          "ACTIVITY MANAGER SERVICES\n ServiceRecord{bad u0 private}\n (nothing)\n"}) {
        assert(parse_system_components(text, true)["status"] == "unavailable");
    }

    const auto partial = parse_system_components(
            "ACTIVITY MANAGER ACTIVITIES\n ActivityRecord{aa u0 com.example/.Main t1}\n ActivityRecord{bad}\n",
            false);
    assert(partial["status"] == "partial" && partial["complete"] == false &&
           partial["items"].size() == 1);
    assert(parse_system_components(std::string(128 * 1024, 'x'), true)["status"] == "unavailable");
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
