#include "qnx_prepare.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_guest_build.hpp"
#include "qnx_guest_shell.hpp"
#include "qnx_imagefs.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) {
    try {
        const auto qinggan09_required = qnx_qinggan09_disk_floor + qnx_qinggan09_guest_workspace +
                                        qnx_qinggan09_image_budget;

        if (qnx_qinggan09_source_space_available(qinggan09_required - 1) ||
            !qnx_qinggan09_source_space_available(qinggan09_required) ||
            !qnx_qinggan09_source_space_available(qinggan09_required + 1) ||
            qinggan09_required != (10752ULL << 20)) {
            throw std::runtime_error("Free A9 source graph disk boundary");
        }

        const auto qinggan11_required = qnx_qinggan11_disk_floor + qnx_qinggan11_guest_workspace +
                                        qnx_qinggan11_image_budget;

        if (qnx_qinggan11_source_space_available(qinggan11_required - 1) ||
            !qnx_qinggan11_source_space_available(qinggan11_required) ||
            !qnx_qinggan11_source_space_available(qinggan11_required + 1) ||
            qinggan11_required != (10616ULL << 20)) {
            throw std::runtime_error("Free A11 source graph disk boundary");
        }

        {
            // The incomplete Passion definition publishes its adapter; the standard preparation
            // reaches its guest modules.
            const std::string passion = "passion-11-russian-20260403-2.1.4.80";
            const Json incomplete = {{"schema", 1},
                                     {"status", "incomplete"},
                                     {"hardware_adapter", "sa8155-passion-21480"}};

            if (qnx_profile_adapter(incomplete) != "sa8155-passion-21480" ||
                qnx_profile_adapter(Json{{"hardware_adapter", "sa8155-qinggan09"}}) !=
                        "sa8155-qinggan09") {
                throw std::runtime_error("published QNX adapter differs");
            }

            for (const auto &definition :
                 {Json{{"status", "incomplete"}, {"hardware_adapter", nullptr}},
                  Json{{"status", "incomplete"}, {"hardware_adapter", 7}}}) {
                std::string code;
                try {
                    qnx_profile_adapter(definition);
                } catch (const std::runtime_error &error) {
                    code = message_code(error.what());
                }

                if (code != "QNX_PREPARE_ADAPTER") {
                    throw std::runtime_error("unpublished QNX adapter accepted: " +
                                             definition.dump());
                }
            }

            const auto scratch = std::filesystem::temp_directory_path() /
                                 ("qnx-prepare-adapter-" + random_token());
            std::filesystem::create_directories(scratch / "profiles" / passion);
            std::filesystem::create_directories(scratch / ".local/build");
            const auto root = std::filesystem::canonical(scratch);
            write_private_json(root / "profiles" / passion / "profile.json", Json::object());
            write_private_json(root / "profiles" / passion / "profile-qnx.json", incomplete);
            std::string error;
            try {
                prepare_qnx_images(root, passion, root / ".local/build/output", true);
            } catch (const std::exception &failure) {
                error = failure.what();
            }

            std::filesystem::remove_all(root);

            if (!error.starts_with("DOWNLOAD_OFFLINE")) {
                throw std::runtime_error("Passion preparation stopped before its guest modules: " +
                                         error);
            }
        }

        if (argc == 5 && std::string_view(argv[1]) == "--developer-passion") {
            const auto root = std::filesystem::canonical(argv[2]);
            std::cout << prepare_qnx_passion_developer_images(root,
                                                              std::filesystem::absolute(argv[3]),
                                                              std::filesystem::absolute(argv[4]))
                                 .dump()
                      << '\n';

            return 0;
        }

        if (argc == 5 && (std::string_view(argv[1]) == "--developer-qinggan11" ||
                          std::string_view(argv[1]) == "--source-qinggan11")) {
            const auto root = std::filesystem::canonical(argv[2]);
            std::cout << prepare_qnx_images(root, argv[3], std::filesystem::absolute(argv[4]), true)
                                 .dump()
                      << '\n';

            return 0;
        }

        if (argc == 5 && std::string(argv[1]) == "--run") {
            const auto root = std::filesystem::canonical(argv[2]);
            std::cout << prepare_qnx_images(root, argv[3], std::filesystem::absolute(argv[4]), true)
                                 .dump()
                      << '\n';

            return 0;
        }

        const std::string launch =
                "QGFX_NATIVE_MCU=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/rpcd dummy &\n";
        const std::string mount =
                "qm_bounded logmount /bin/mount -t qnx6 -o sync=optional /dev/qip/hd1 /log || exit 1\n";
        const std::string monitor =
                "cat > /log/clsboot/unboot.list <<'QIP_MONITOR_ONLY'\n#0_hmimgr\n#23_io-pkt-v6-hc\nQIP_MONITOR_ONLY\n";
        const std::string fixture = "original sysmgr and watchdog\n" + mount + launch + monitor +
                                    "original readiness and services\n";
        const auto actual = qnx_qinggan09_runtime(fixture);
        const auto entropy_begin = actual.find("/usr/sbin/random -t >&2 &\n"),
                   entropy_end = actual.find("echo 'QNET entropy-ready=1'\n", entropy_begin);

        if (entropy_begin == actual.npos || entropy_end == actual.npos ||
            entropy_begin < actual.find(mount) + mount.size() ||
            entropy_end > actual.find("/usr/sbin/qconn")) {
            throw std::runtime_error("Free A9 OEM entropy must be ready before qconn");
        }

#ifndef _WIN32
        const auto entropy_script = actual.substr(
                entropy_begin,
                entropy_end + std::string("echo 'QNET entropy-ready=1'\n").size() - entropy_begin);
        const std::string entropy_mock = R"SH(qm_test_steps=0
qm_bounded() { case $1 in random-ready) return $RAND_RC;; *) return 125;; esac; }
function /usr/sbin/random { return 0; }
function kill { builtin [ "$START_RC" -eq 0 ]; }
function [ { if builtin [ "$#" -eq 4 ] && builtin [ "$1" = ! ] && builtin [ "$2" = -c ]; then ! builtin [ "$qm_test_steps" -ge "$READY_AT" ]; else builtin [ "$@"; fi; }
function /bin/sleep { qm_test_steps=$((qm_test_steps+1)); }
)SH";

        for (const auto &values : std::vector<std::vector<int>>{
                     {0, 2, 0, 0}, {9, 2, 0, 1}, {0, 999, 0, 1}, {0, 0, 8, 1}}) {
            ProcessSpec command;
            command.executable = "/bin/bash";
            command.arguments = {"-c", "START_RC=" + std::to_string(values[0]) +
                                               "\nREADY_AT=" + std::to_string(values[1]) +
                                               "\nRAND_RC=" + std::to_string(values[2]) + "\n" +
                                               entropy_mock + entropy_script +
                                               "echo ENTROPY_ADMITTED\n"};
            const auto result = execute(command, 5000, 4096);

            if (result.timed_out || result.exit_code != values[3] ||
                (result.output.find("ENTROPY_ADMITTED") != std::string::npos) != (values[3] == 0)) {
                throw std::runtime_error("Free A9 entropy daemon/device/RAND readiness gate");
            }
        }
#endif

        if (actual.find("QGFX_NATIVE_MCU=1 QGFX_NATIVE_MCU_ASYNC=1 LD_PRELOAD=") == actual.npos ||
            actual.find(monitor) == actual.npos ||
            !actual.ends_with("original readiness and services\n")) {
            throw std::runtime_error("Free A9 MCU/monitor preservation");
        }

        // The Free A11 runtime starts rpcd with the same asynchronous MCU transport.
        const auto async = qnx_native_mcu_async_runtime(fixture);

        if (async.find("QGFX_NATIVE_MCU=1 QGFX_NATIVE_MCU_ASYNC=1 LD_PRELOAD=") == async.npos ||
            async.find(launch) != async.npos || async.size() != fixture.size() + 24) {
            throw std::runtime_error("asynchronous MCU launch of the rpcd runtime");
        }

        for (const auto &invalid : {std::string("missing"), fixture + launch, async}) {
            bool rejected = false;

            try {
                qnx_native_mcu_async_runtime(invalid);
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("missing/duplicate/already asynchronous rpcd launch");
            }
        }

        const auto
                relay = actual.find(
                        "VOYEMU_QNET_RELAY=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /bin/ls >&2 &\n"),
                qconn = actual.find("/usr/sbin/qconn >&2 &\n");

        if (relay == actual.npos || qconn == actual.npos ||
            relay < actual.find(mount) + mount.size() || qconn < relay ||
            actual.find("/bin/ifconfig tap0 create || exit 1") == actual.npos ||
            actual.find("172.16.100.22 netmask 255.255.255.0 up || exit 1") == actual.npos ||
            actual.find("[ -e /dev/socket ] ||") == actual.npos ||
            actual.find("/bin/io-pkt-v6-hc") != actual.npos) {
            throw std::runtime_error("Free A9 genuine existing-stack TAP/qconn bootstrap");
        }

        for (const auto &invalid :
             {std::string("missing"), fixture + launch, fixture + mount, launch, actual}) {
            bool rejected = false;
            try {
                qnx_qinggan09_runtime(invalid);
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error(
                        "Free A9 missing/duplicate/already-patched anchors accepted");
            }
        }

        // The A9 and A11 direct Gauge launch opts in to the Kanzi observer; point, light and other
        // owners keep their environment.
        const std::string gauge =
                "QGFX_WHEEL_RELAY=0 QGFX_MCU_RELAY=0 QGFX_DLT_TAIL=1 QGFX_THEME_REQUEST_ENTRY=1 "
                "LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/gauge &\n";
        const std::string owners =
                "QGFX_MCD_TRACE=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/point &\n"
                "QGFX_MCD_TRACE=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/light &\n";
        const auto observed_gauge = qnx_kanzi_gauge_runtime(owners + gauge);

        if (observed_gauge !=
                    owners + "QGFX_WHEEL_RELAY=0 QGFX_MCU_RELAY=0 QGFX_DLT_TAIL=1 "
                             "QGFX_THEME_REQUEST_ENTRY=1 QGFX_CLUSTER_UI=1 "
                             "LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /usr/bin/gauge &\n" ||
            qnx_qinggan09_runtime(fixture + gauge, {}, true) !=
                    qnx_kanzi_gauge_runtime(qnx_qinggan09_runtime(fixture + gauge))) {
            throw std::runtime_error("Kanzi observer opt-in must be scoped to the Gauge launch");
        }

        for (const auto &invalid : {owners, owners + gauge + gauge, observed_gauge}) {
            bool rejected = false;
            try {
                (void)qnx_kanzi_gauge_runtime(invalid);
            } catch (const std::runtime_error &error) {
                rejected = std::string_view(error.what()) ==
                           "QNX_KANZI_GAUGE: QNX preparation check failed";
            }

            if (!rejected) {
                throw std::runtime_error("Kanzi Gauge missing, duplicate or repeated opt-in");
            }
        }

        const std::string persistent_fixture =
                "automount=hd0:/:qnx6:ro,after &\n" + mount + monitor +
                "qm_bounded vardirs old-volatile-alias\n/bin/pps -m /var/pps -p /var/pps_persist\n" +
                launch +
                "LD_PRELOAD=/proc/boot/bin/qditest /usr/bin/hmimgr\nqm_background=$!\nQGFX_MCD_TRACE=1 /usr/bin/point &\n"
                "QGFX_MCD_TRACE=1 /usr/bin/light &\nQGFX_MCD_TRACE=1 /usr/bin/gauge &\n# Stream the real Screen composition\nqm_export_screen &\nqm_export=$!\n"
                "kill \"$qm_gauge\" 2>/dev/null\nwait \"$qm_gauge\"\necho \"QMC GAUGE exit=$?\"\n"
                "for qm_service in \"$qm_background\" \"$qm_point\" \"$qm_light\" \"$qm_ivcd\"; do wait \"$qm_service\"; done\n";
        const auto persistent = qnx_qinggan09_runtime(
                persistent_fixture,
                "echo persistent-stage\nqm_render_preload=extensions:render\nqm_observer_preload=extensions:observer\n");

        if (persistent.find("ro,after qnx6 sync=optional &") == persistent.npos ||
            persistent.find("old-volatile-alias") != persistent.npos ||
            persistent.find("echo persistent-stage") > persistent.find("/bin/pps -m") ||
            persistent.find("LD_PRELOAD=\"$qm_render_preload\" /usr/bin/rpcd") == persistent.npos ||
            persistent.find("LD_PRELOAD=\"$qm_observer_preload\"") == persistent.npos) {
            throw std::runtime_error(
                    "persistent root options, stage order, and preload composition");
        }

        const auto dispatch = persistent.find("/usr/bin/sysmgrtool start \"$qm_owner\"");
        const auto exporter = persistent.find("qm_export_screen &");

        if (dispatch == persistent.npos || exporter == persistent.npos || dispatch >= exporter ||
            persistent.find("[ ! -e /log/run/$qm_owner.pid ]") == persistent.npos) {
            throw std::runtime_error("A9 graphics start must dispatch before observing the Screen");
        }

        for (const auto &owner : {"point", "light", "gauge"}) {
            const auto pid_check = persistent.find(std::string("$(qm_owner_pid ") + owner + ")");

            if (pid_check == persistent.npos || pid_check <= exporter ||
                persistent.find(std::string(" /usr/bin/") + owner + " ") != persistent.npos) {
                throw std::runtime_error(
                        "A9 graphics owner bypasses native supervisor or PID observation");
            }
        }

        if (persistent.find("/usr/bin/sysmgrtool stop \"$qm_owner\"") == persistent.npos ||
            persistent.find("wait \"$qm_gauge\"") != persistent.npos) {
            throw std::runtime_error("A9 supervised owners must stop through sysmgr");
        }

        if (persistent.find("VOYEMU_QNET_RELAY=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb") ==
                    persistent.npos ||
            persistent.find("${LD_PRELOAD:+") != persistent.npos) {
            throw std::runtime_error(
                    "infrastructure preload changed or observer reads ambient preload");
        }

#ifndef _WIN32
        const auto source_root = std::filesystem::path(__FILE__)
                                         .parent_path()
                                         .parent_path()
                                         .parent_path()
                                         .parent_path();
        ProcessSpec preload_test;
        preload_test.executable = "/bin/bash";
        preload_test.arguments = {
                (source_root / "src/guest/qnx/persistent-runtime.test.sh").string()};
        const auto preload_result = execute(preload_test, 5000, 8192);

        if (preload_result.timed_out || preload_result.exit_code != 0) {
            throw std::runtime_error("persistent preload shell regression: " +
                                     preload_result.output);
        }

        auto source_text = [](const std::filesystem::path &file) {
            std::ifstream stream(file);

            if (!stream) {
                throw std::runtime_error("runtime source unavailable");
            }

            return std::string(std::istreambuf_iterator<char>(stream), {});
        };
        const auto owner_stage = source_text(source_root / "src/guest/qnx/persistent-runtime.sh");
        const auto assembled = qnx_qinggan09_runtime(
                source_text(source_root / "src/guest/qnx/qnx-runtime.sh"), owner_stage);

        if (assembled.find("qm_sc ensure\n") > assembled.find("/usr/bin/sysmgr &")) {
            throw std::runtime_error("A9 supervised owner config order");
        }

        const auto observed = qnx_qinggan09_runtime(
                source_text(source_root / "src/guest/qnx/qnx-runtime.sh"), owner_stage, true);

        if (observed.find("if(o==\"gauge\")f=\"CLUSTER_UI=1 ") == observed.npos ||
            assembled.find("CLUSTER_UI=1") != assembled.npos) {
            throw std::runtime_error("A9 Kanzi observer must be scoped to Gauge");
        }

        const auto original_ifs = source_root /
                                  ".local/profiles/free-09-chinese-20260106-6.0.3/qnx/ifs2_la.img";

        if (std::filesystem::is_regular_file(original_ifs)) {
            const auto display = read_qnx_imagefs_file(source_text(original_ifs),
                                                       "ifs_display.img");
            const auto before = read_qnx_imagefs_file(display, "bin/ssplash");
            (void)replace_qnx_imagefs_file_in_zero_padding(display, "bin/ssplash",
                                                           sha256_bytes(before),
                                                           prepare_qnx_guest_shell(observed), 4096);
        }

        preload_test.arguments = {
                (source_root / "src/guest/qnx/persistent-supervisor.test.sh").string()};
        const auto supervisor_result = execute(preload_test, 5000, 8192);

        if (supervisor_result.timed_out || supervisor_result.exit_code != 0) {
            throw std::runtime_error("persistent supervisor lifecycle regression: " +
                                     supervisor_result.output);
        }

        std::cout << "A9 persistent=" << owner_stage.size() << " assembled=" << assembled.size()
                  << " bytes\n";
        preload_test.arguments = {
                (source_root / "src/guest/qnx/persistent-owner-restart.test.sh").string()};
        const auto owner_result = execute(preload_test, 5000, 8192);

        if (owner_result.timed_out || owner_result.exit_code != 0) {
            throw std::runtime_error("persistent supervised owner regression: " +
                                     owner_result.output);
        }
#endif

        for (const auto &invalid :
             {fixture, persistent_fixture + "automount=hd0:/:qnx6:ro,after &\n"}) {
            bool rejected = false;
            try {
                qnx_qinggan09_runtime(invalid, "echo persistent-stage\n");
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error(
                        "persistent runtime accepted missing or duplicate root driver");
            }
        }

        {
            const auto test_source_root = std::filesystem::path(__FILE__)
                                                  .parent_path()
                                                  .parent_path()
                                                  .parent_path()
                                                  .parent_path();
            ensure_private_directory(test_source_root / ".local/build");
            const auto fixture_root =
                    std::filesystem::canonical(test_source_root / ".local/build") /
                    ("qnx-qinggan11-alias-test-" + random_token());
            struct Cleanup {
                std::filesystem::path root;

                ~Cleanup() {
                    std::error_code ec;
                    std::filesystem::remove_all(root, ec);
                }
            } cleanup{fixture_root};
            const std::string base = "free-11-chinese-20260731-7.3.1",
                              alias = "qinggan11-test-copy";
            ensure_private_directory(fixture_root / "profiles" / base);
            ensure_private_directory(fixture_root / "profiles" / alias);
            ensure_private_directory(fixture_root / ".local/build");
            const auto config = read_private_json(
                    test_source_root / "profiles" / base / "profile-qnx.json", 128 * 1024);
            write_private_json(fixture_root / "profiles" / base / "profile.json", {{"id", base}});
            write_private_json(fixture_root / "profiles" / base / "profile-qnx.json", config);
            write_private_json(fixture_root / "profiles" / alias / "profile-qnx.json", config);
            const Json alias_definition = {
                    {"id", alias}, {"capture", {{"profile", alias}, {"referenceProfile", base}}}};
            const auto destination = fixture_root / ".local/build/output";
            auto refused = [&](const Json &definition, const std::string &expected) {
                write_private_json(fixture_root / "profiles" / alias / "profile.json", definition);
                bool matched = false;
                try {
                    (void)prepare_qnx_qinggan11_images(fixture_root, alias, destination);
                } catch (const std::exception &e) {
                    matched = std::string(e.what()).starts_with(expected);

                    if (!matched) {
                        throw std::runtime_error("Free A11 alias expected " + expected + ", got " +
                                                 e.what());
                    }
                }

                if (!matched || std::filesystem::exists(destination)) {
                    throw std::runtime_error(
                            "Free A11 alias mutated output or admitted absent inputs");
                }
            };
            refused(alias_definition, "QNX_ORIGINALS_REFERENCE_MISSING");
            auto bad = alias_definition;
            bad["capture"]["referenceProfile"] = "free-09-chinese-20260106-6.0.3";
            refused(bad, "QNX_QINGGAN11_DEVELOPER_IDENTITY");
            bad = alias_definition;
            bad["capture"]["profile"] = "foreign";
            refused(bad, "QNX_QINGGAN11_DEVELOPER_IDENTITY");
            auto changed = config;
            changed["unexpected_alias_drift"] = true;
            write_private_json(fixture_root / "profiles" / alias / "profile-qnx.json", changed);
            refused(alias_definition, "QNX_ORIGINALS_REFERENCE");
        }
        bool dev_failed = false;
        try {
            prepare_qnx_qinggan11_developer_images("relative", "invalid", {});
        } catch (...) {
            dev_failed = true;
        }

        if (!dev_failed) {
            throw std::runtime_error("developer relative root accepted");
        }

        bool passion_failed = false;
        try {
            prepare_qnx_passion_developer_images("relative", {}, {});
        } catch (...) {
            passion_failed = true;
        }

        if (!passion_failed) {
            throw std::runtime_error("Passion developer relative root accepted");
        }

        bool failed = false;
        try {
            prepare_qnx_images("relative", "invalid", {});
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("relative project accepted");
        }

        std::cout << "raw QNX preparation preflight guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
