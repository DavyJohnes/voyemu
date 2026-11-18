#include "qnx_qinggan11.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_guest_shell.hpp"
#include "qnx_imagefs.hpp"
#include "qnx_prepare.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
std::string read(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);

    if (!input) {
        throw std::runtime_error("fixture open");
    }

    return std::string((std::istreambuf_iterator<char>(input)), {});
}

template <class Function> void rejects(Function function) {
    bool failed = false;

    try {
        function();
    } catch (...) {
        failed = true;
    }

    if (!failed) {
        throw std::runtime_error("unguarded A11 display accepted");
    }
}

template <class Function> void rejects_code(Function function, std::string_view code) {
    try {
        function();
    } catch (const std::runtime_error &error) {
        if (message_code(error.what()) == code) {
            return;
        }

        throw;
    }

    throw std::runtime_error("expected Free runtime guard");
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-611-display") {
            const auto image = read(argv[2]), configuration = read(argv[3]);
            const auto changed = prepare_qnx_qinggan11_611_display_config(image, configuration);
            const auto actual = read_qnx_imagefs_file(changed, "lib64/graphics.conf");

            if (actual.substr(0, configuration.size()) != configuration || actual.size() != 1119) {
                throw std::runtime_error("6.11 display configuration differs");
            }

            rejects([&] {
                prepare_qnx_qinggan11_611_display_config(changed, configuration);
            });
            std::cout << "6.11 display exact guard passed\n";

            return 0;
        }

        if (argc == 5 && std::string_view(argv[1]) == "--qinggan11-611") {
            const auto manager = read(argv[2]), scene = read(argv[3]), driver = read(argv[4]);
            const auto fixed_manager = prepare_qnx_qinggan11_611_theme_manager(manager),
                       fixed_scene = prepare_qnx_qinggan11_611_theme_scene(scene);

            for (const auto at : {0x5844, 0x587c, 0x58ec, 0x5924, 0x59b0}) {
                if (static_cast<unsigned char>(fixed_manager[at + 1]) != 0xcc) {
                    throw std::runtime_error("6.11 manager comparator");
                }
            }

            for (const auto at : {0x9de4, 0x9ed4, 0xa2cc, 0xa3b4}) {
                if (static_cast<unsigned char>(fixed_scene[at + 1]) != 0xcc) {
                    throw std::runtime_error("6.11 scene comparator");
                }
            }

            validate_qnx_qinggan11_611_ram_driver(driver);
            rejects([&] {
                prepare_qnx_qinggan11_theme_manager(manager);
            });
            rejects([&] {
                prepare_qnx_qinggan11_theme_scene(scene);
            });
            rejects([&] {
                prepare_qnx_qinggan11_611_theme_manager(fixed_manager);
            });
            rejects([&] {
                prepare_qnx_qinggan11_611_theme_scene(fixed_scene);
            });
            auto bad = driver;
            bad[201] ^= 1;
            rejects([&] {
                validate_qnx_qinggan11_611_ram_driver(bad);
            });
            std::cout << "6.11 theme and OEM RAM driver guards passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--theme-scene") {
            const auto original = read(argv[2]);
            const auto fixed = prepare_qnx_qinggan11_theme_scene(original);
            auto expected = original;

            for (const auto at : {0x9de4, 0x9ed4, 0xa2cc, 0xa3b4}) {
                expected[at + 1] = static_cast<char>(0xcc);
            }

            if (fixed == original || fixed != expected) {
                throw std::runtime_error(
                        "HMI must require four genuine APP_READY and APP_RET responses");
            }

            rejects([&] {
                prepare_qnx_qinggan11_theme_scene(fixed);
            });
            rejects([] {
                prepare_qnx_qinggan11_theme_scene({});
            });
            std::cout << "A11 OEM HMI readiness regression passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--theme-manager") {
            const auto original = read(argv[2]);
            const auto fixed = prepare_qnx_qinggan11_theme_manager(original);

            if (fixed.size() != original.size()) {
                throw std::runtime_error("theme manager extent");
            }

            unsigned changed = 0;

            for (std::size_t i = 0; i < fixed.size(); ++i) {
                if (fixed[i] != original[i]) {
                    ++changed;
                }
            }

            if (changed != 5) {
                throw std::runtime_error("four OEM consumers must require all four real responses");
            }

            for (const auto at : {0x5844, 0x587c, 0x58ec, 0x5924, 0x59b0}) {
                if (static_cast<unsigned char>(fixed[at + 1]) != 0xcc) {
                    throw std::runtime_error("theme readiness comparator");
                }
            }

            rejects([&] {
                prepare_qnx_qinggan11_theme_manager(fixed);
            });
            std::cout << "A11 OEM theme consumer readiness regression passed\n";

            return 0;
        }

        rejects([] {
            prepare_qnx_qinggan11_theme_manager({});
        });
        rejects([] {
            prepare_qnx_qinggan11_theme_scene({});
        });
        rejects([] {
            prepare_qnx_qinggan11_display_config({}, {});
        });
        const std::string
                initial_function = R"QNX_INITIAL(qm_initial_native_delay() (
    [ "$#" -eq 0 ] || return 125
    unset VOYEMU_QBW_ARG_00 VOYEMU_QBW_ARG_01 VOYEMU_QBW_ARG_02 VOYEMU_QBW_ARG_03 VOYEMU_QBW_ARG_04 VOYEMU_QBW_ARG_05 VOYEMU_QBW_ARG_06 VOYEMU_QBW_ARG_07 VOYEMU_QBW_ARG_08 VOYEMU_QBW_ARG_09 VOYEMU_QBW_ARG_10 VOYEMU_QBW_ARG_11 VOYEMU_QBW_ARG_12 VOYEMU_QBW_ARG_13 VOYEMU_QBW_ARG_14 VOYEMU_QBW_ARG_15
    export VOYEMU_QBW_ENABLE=initial-delay10ms-v1 VOYEMU_QBW_ARGC=0
    export LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb
    exec /proc/boot/bin/ksh -c 'exit 125'
)
)QNX_INITIAL",
                initial_case = R"QNX_INITIAL(case "${VOYEMU_QNX_INITIAL_NATIVE_DELAY-}" in
    '') if ! /bin/sleep 0.01; then echo 'QMC FAIL fractional-sleep-required'; exit 1; fi ;;
    1) if ! qm_initial_native_delay; then echo 'QMC FAIL initial-native-delay'; exit 125; fi ;;
    *) echo 'QMC FAIL initial-delay-mode'; exit 125 ;;
esac
)QNX_INITIAL",
                initial_default =
                        R"QNX_INITIAL(if ! /bin/sleep 0.01; then echo 'QMC FAIL fractional-sleep-required'; exit 1; fi
)QNX_INITIAL";
        const std::string old = "2e13e9b8299f882414400969309c501c639314a92f5d0e603806e0f13d37426c",
                          copy = "of=/log/mcd-observer-padded.so bs=16928 count=1",
                          base_runtime = "#!/proc/boot/bin/ksh\nqm_source_capture=0\n" + old +
                                         "\n" + old + "\n" + copy + "\n",
                          hash(64, 'a');
        const std::array<std::pair<std::string_view, unsigned>, 13> utilities{
                {{"/bin/sleep", 15},
                 {"/bin/cat", 9},
                 {"/bin/dd", 2},
                 {"/bin/grep", 10},
                 {"/bin/ls", 8},
                 {"/bin/openssl", 3},
                 {"/bin/chmod", 1},
                 {"/sbin/mkqnx6fs", 1},
                 {"/bin/mount", 1},
                 {"/bin/mkdir", 4},
                 {"/bin/touch", 4},
                 {"/bin/ln", 4},
                 {"/bin/pps", 1}}};
        auto runtime = base_runtime + initial_function + "qm_wait_ram_driver() {\n: \n}\n" +
                       initial_case;

        for (const auto &[path, count] : utilities) {
            for (unsigned i = 0; i < count - (path == "/bin/sleep" ? 1 : 0); ++i) {
                runtime += std::string(path) + " arg\n";
            }
        }

        const std::string animation_ready =
                "qm_bounded animation-ready /usr/bin/test_powFakeMsg test_evt 58 1 power\n";
        runtime +=
                "/usr/bin/dlt-daemon -c /log/dlt-owned.conf > /log/dlt-ascii.log 2>&1 &\nqm_bounded dlt-ready qm_wait_dlt || exit 1\n" +
                animation_ready;
        const auto selected = prepare_qnx_qinggan11_runtime(runtime, hash);

        if (selected.find(
                    "qm_bounded animation-ready /usr/bin/test_powFakeMsg test_evt 60 1 power\n") ==
                    std::string::npos ||
            selected.find("test_evt 58") != std::string::npos) {
            throw std::runtime_error("A11 IVI animation-ready event");
        }

        rejects([&] {
            auto missing = runtime;
            missing.erase(missing.find(animation_ready), animation_ready.size());
            prepare_qnx_qinggan11_runtime(missing, hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + animation_ready, hash);
        });

        if (selected.find("qm_source_capture=1") == std::string::npos ||
            selected.find(old) != std::string::npos ||
            selected.find("bs=21312 count=1") == std::string::npos ||
            selected.find("bs=16928 count=1") != std::string::npos) {
            throw std::runtime_error("source capture selection and complete observer copy");
        }

        rejects([&] {
            prepare_qnx_qinggan11_runtime(selected, hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime, "bad");
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(std::string(21211, 'x'), hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime("#!/proc/boot/bin/ksh\n" + old + old, hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + old, hash);
        });
        rejects([&] {
            auto missing = runtime;
            missing.erase(missing.find(copy), copy.size());
            prepare_qnx_qinggan11_runtime(missing, hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + copy, hash);
        });

        if (selected.find(
                    "VOYEMU_QNET_RELAY=1 LD_PRELOAD=/proc/boot/bin/Snapdragon.rgb /proc/boot/bin/ls") ==
                    std::string::npos ||
            selected.find(
                    "qm_bounded net-up /bin/ifconfig tap0 172.16.100.22 netmask 255.255.255.0 up") ==
                    std::string::npos ||
            selected.find(
                    "qm_bounded random-ready /proc/boot/bin/openssl rand -out /dev/null 32") ==
                    std::string::npos) {
            throw std::runtime_error("Free network providers absent");
        }

        if (selected.find("qm_bounded socket-provider") > selected.find("qm_bounded net-create") ||
            selected.find("qm_bounded net-create") > selected.find("/usr/bin/dlt-daemon")) {
            throw std::runtime_error("Free network startup order");
        }

        rejects_code(
                [&] {
                    prepare_qnx_qinggan11_runtime(runtime + "VOYEMU_QNET_RELAY=1\n", hash);
                },
                "QNX_QINGGAN11_RUNTIME_NETWORK");
#ifndef _WIN32
        {
            ProcessSpec syntax;
            syntax.executable = "/bin/sh";
            syntax.directory = std::filesystem::current_path();
            syntax.arguments = {"-n", "-c", selected};

            if (execute(syntax, 5000, 4096).exit_code != 0) {
                throw std::runtime_error("Free network shell syntax");
            }
        }
#endif
        const std::string wait_trace =
                "echo 'QMC WAIT initial_sleep before'\necho 'QMC WAIT initial_sleep after'\necho 'QMC WAIT ram_driver before'\necho 'QMC WAIT ram_driver after'\n";

        if (prepare_qnx_qinggan11_runtime(runtime + wait_trace, hash) != selected) {
            throw std::runtime_error("Free runtime changed for A9 wait tracing");
        }

        rejects_code(
                [&] {
                    prepare_qnx_qinggan11_runtime(
                            runtime + wait_trace + "echo 'QMC WAIT ram_driver after'\n", hash);
                },
                "QNX_QINGGAN11_RUNTIME_WAIT_TRACE");
        rejects_code(
                [&] {
                    prepare_qnx_qinggan11_runtime(runtime + "echo 'QMC WAIT ram_driver after'\n",
                                                  hash);
                },
                "QNX_QINGGAN11_RUNTIME_WAIT_TRACE");

        if (selected.find("qm_initial_native_delay") != std::string::npos ||
            selected.find("VOYEMU_QNX_INITIAL_NATIVE_DELAY") != std::string::npos ||
            selected.find("initial-delay10ms-v1") != std::string::npos ||
            selected.find(initial_default) == std::string::npos) {
            throw std::runtime_error("Free projection retained Windows-only initial delay");
        }

        for (const auto &block : {initial_function, initial_case}) {
            rejects_code(
                    [&] {
                        auto missing = runtime;
                        missing.erase(missing.find(block), block.size());
                        prepare_qnx_qinggan11_runtime(missing, hash);
                    },
                    "QNX_QINGGAN11_RUNTIME_INITIAL_DELAY");
            rejects_code(
                    [&] {
                        prepare_qnx_qinggan11_runtime(runtime + block, hash);
                    },
                    "QNX_QINGGAN11_RUNTIME_INITIAL_DELAY");
        }

        rejects_code(
                [&] {
                    auto changed = runtime;
                    changed[changed.find(initial_case) + initial_case.size() - 2] = ' ';
                    prepare_qnx_qinggan11_runtime(changed, hash);
                },
                "QNX_QINGGAN11_RUNTIME_INITIAL_DELAY");
        rejects_code(
                [&] {
                    prepare_qnx_qinggan11_runtime(
                            runtime + "# VOYEMU_QNX_INITIAL_NATIVE_DELAY unexpected residual\n",
                            hash);
                },
                "QNX_QINGGAN11_RUNTIME_INITIAL_DELAY");

        const std::string
                dlt_original = "qm_bounded dlt-ready qm_wait_dlt || exit 1",
                dlt_report =
                        "qm_bounded dlt-ready qm_wait_dlt || { qm_bounded dlt-stderr /proc/boot/bin/dd if=/log/dlt-ascii.log bs=1024 count=8; exit 1; }";

        if (selected.find(dlt_report) == std::string::npos) {
            throw std::runtime_error("bounded genuine DLT stderr export");
        }

        rejects([&] {
            auto missing = runtime;
            missing.erase(missing.find(dlt_original), dlt_original.size());
            prepare_qnx_qinggan11_runtime(missing, hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + dlt_original, hash);
        });
        const std::string
                dlt_launch =
                        "/usr/bin/dlt-daemon -c /log/dlt-owned.conf > /log/dlt-ascii.log 2>&1 &",
                socket_provider =
                        "if [ ! -e /dev/socket ]; then\n    /proc/boot/bin/io-pkt-v6-hc -I -t2 > /log/io-pkt-local.log 2>&1 &\n    qm_socket_provider=$!\n    qm_bounded socket-provider /proc/boot/bin/waitfor /dev/socket 5 || { qm_bounded socket-stderr /proc/boot/bin/dd if=/log/io-pkt-local.log bs=1024 count=8; exit 1; }\nfi\n";

        if (selected.find(socket_provider + "/usr/sbin/random -t >&2 &\n") == std::string::npos ||
            selected.find(socket_provider, selected.find(socket_provider) + 1) !=
                    std::string::npos) {
            throw std::runtime_error("genuine socket prerequisite before original DLT launch");
        }

        rejects([&] {
            auto missing = runtime;
            missing.erase(missing.find(dlt_launch), dlt_launch.size());
            prepare_qnx_qinggan11_runtime(missing, hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + dlt_launch, hash);
        });
#ifndef _WIN32
        auto mocked_socket = socket_provider;
        const auto replace_mock = [&](const std::string &old, const std::string &replacement) {
            const auto at = mocked_socket.find(old);

            if (at == std::string::npos || mocked_socket.find(old, at + 1) != std::string::npos) {
                throw std::runtime_error("socket mock substitution");
            }

            mocked_socket.replace(at, old.size(), replacement);
        };
        replace_mock("[ ! -e /dev/socket ]", "[ \"$node_present\" = 0 ]");
        replace_mock("/proc/boot/bin/io-pkt-v6-hc -I -t2 > /log/io-pkt-local.log 2>&1 &",
                     "qm_io_pkt -I -t2 &");

        for (const auto present : {0, 1}) {
            for (const auto code : {0, 7}) {
                ProcessSpec spec;
                spec.executable = "/bin/sh";
                spec.directory = std::filesystem::current_path();
                spec.arguments = {
                        "-c",
                        "node_present=" + std::to_string(present) +
                                "; qm_io_pkt(){ [ \"$#\" -eq 2 ] && [ \"$1\" = -I ] && [ \"$2\" = -t2 ] || exit 92; echo SOCKET_START; }; qm_bounded(){ if [ \"$1\" = socket-provider ]; then [ \"$#\" -eq 4 ] && [ \"$2\" = /proc/boot/bin/waitfor ] && [ \"$3\" = /dev/socket ] && [ \"$4\" = 5 ] || exit 93; echo SOCKET_WAIT; return " +
                                std::to_string(code) +
                                "; fi; [ \"$#\" -eq 5 ] && [ \"$1\" = socket-stderr ] && [ \"$2\" = /proc/boot/bin/dd ] && [ \"$3\" = if=/log/io-pkt-local.log ] && [ \"$4\" = bs=1024 ] && [ \"$5\" = count=8 ] || exit 94; echo SOCKET_REPORT; return 127; }; " +
                                mocked_socket +
                                "if [ \"$node_present\" = 0 ]; then wait \"$qm_socket_provider\" || exit 95; fi; echo DLT_FOLLOW"};
                const auto result = execute(spec, 5000, 4096);
                const auto failed = present == 0 && code != 0;
                auto occurrences = [&](const std::string &value) {
                    unsigned count = 0;

                    for (std::size_t p = 0; (p = result.output.find(value, p)) != std::string::npos;
                         p += value.size()) {
                        ++count;
                    }

                    return count;
                };

                if (result.timed_out || result.exit_code != (failed ? 1 : 0) ||
                    occurrences("SOCKET_START") != (present ? 0 : 1) ||
                    occurrences("SOCKET_WAIT") != (present ? 0 : 1) ||
                    occurrences("SOCKET_REPORT") != (failed ? 1 : 0) ||
                    occurrences("DLT_FOLLOW") != (failed ? 0 : 1)) {
                    throw std::runtime_error(
                            "genuine local socket prerequisite lifecycle/error route");
                }
            }
        }

        for (const auto code : {0, 7}) {
            ProcessSpec spec;
            spec.executable = "/bin/sh";
            spec.directory = std::filesystem::current_path();
            spec.arguments = {
                    "-c",
                    "qm_bounded(){ if [ \"$1\" = dlt-ready ]; then return " + std::to_string(code) +
                            "; fi; [ \"$#\" -eq 5 ] && [ \"$1\" = dlt-stderr ] && [ \"$2\" = /proc/boot/bin/dd ] && [ \"$3\" = if=/log/dlt-ascii.log ] && [ \"$4\" = bs=1024 ] && [ \"$5\" = count=8 ] || exit 93; echo DLT_REPORT; return 127; }; " +
                            dlt_report};
            const auto result = execute(spec, 5000, 4096);

            if (result.timed_out || result.exit_code != (code == 0 ? 0 : 1) ||
                (result.output.find("DLT_REPORT") != std::string::npos) != (code != 0)) {
                throw std::runtime_error("DLT genuine failure status/export route changed");
            }
        }
#endif

        if (argc == 3 && std::string_view(argv[1]) == "--runtime") {
            const auto original = read(std::filesystem::path(argv[2]) /
                                       "src/guest/qnx/qnx-runtime.sh"),
                       adapted = prepare_qnx_qinggan11_runtime(original, hash),
                       installed = prepare_qnx_guest_shell(qnx_kanzi_gauge_runtime(adapted));
            // Preparation replaces the 21210-byte OEM bin/ssplash of both A11 displays in place.
            constexpr std::size_t ssplash_slot = 21210;

            if (installed.size() > ssplash_slot) {
                throw std::runtime_error("A11 runtime needs " + std::to_string(installed.size()) +
                                         " bytes of the " + std::to_string(ssplash_slot) +
                                         "-byte ssplash slot");
            }

            if (original.find("qm_bounded config /bin/cat ") == std::string::npos ||
                adapted.find("qm_bounded config /proc/boot/bin/cat ") == std::string::npos ||
                adapted.find(dlt_report) == std::string::npos ||
                adapted.find(socket_provider + "/usr/sbin/random -t >&2 &\n") ==
                        std::string::npos ||
                adapted.find("test_powFakeMsg get_info") != std::string::npos ||
                adapted.find("test_powFakeMsg test_evt 0 ") != std::string::npos ||
                adapted.find("test_powFakeMsg test_evt 60 1 power") == std::string::npos ||
                adapted.find("VOYEMU_QNX_INITIAL_NATIVE_DELAY") != std::string::npos) {
                throw std::runtime_error("actual Free namespace and network runtime snapshot");
            }

            for (const auto &[path, count] : utilities) {
                const auto full = path == "/bin/sleep" ? std::string(path)
                                                       : "/proc/boot" + std::string(path);
                unsigned occurrences = 0;

                for (std::size_t p = 0; (p = adapted.find(full, p)) != std::string::npos;
                     p += full.size()) {
                    ++occurrences;
                }

                if (occurrences < count) {
                    throw std::runtime_error("actual utility namespace count");
                }

                if (adapted.find("/proc/boot/bin/sleep") != std::string::npos) {
                    throw std::runtime_error("host root sleep incorrectly relocated");
                }
            }

            std::cout
                    << "Free A11 7.3.1 exact own-runtime utility namespace source passed; no OEM execution\n";

            return 0;
        }

        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + "/proc/boot/bin/sleep arg\n", hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + "/bin/cat arg\n", hash);
        });
        rejects([&] {
            auto missing = runtime;
            missing.erase(missing.find("/bin/cat"), 8);
            prepare_qnx_qinggan11_runtime(missing, hash);
        });
        rejects([&] {
            prepare_qnx_qinggan11_runtime(runtime + "/bin/unknown arg\n", hash);
        });
        const auto prefixed = prepare_qnx_qinggan11_runtime(
                runtime + "/proc/boot/bin/cat arg\n/usr/bin/gauge arg\n", hash);

        if (prefixed.find("/proc/boot/proc/boot") != std::string::npos ||
            prefixed.find("/usr/bin/gauge arg") == std::string::npos) {
            throw std::runtime_error("utility path boundary");
        }

        if (argc == 3) {
            const auto ifs = read(argv[1]), configuration = read(argv[2]);

            if (sha256_bytes(ifs) !=
                "0e5598e15589c7816af65e2ee7257e240f62651fc142bf94129e08a1ce27c1c2") {
                throw std::runtime_error("A11 ifs fixture pin");
            }

            const auto display = read_qnx_imagefs_file(ifs, "ifs_display.img");
            const auto output = prepare_qnx_qinggan11_display_config(display, configuration);
            const auto actual = read_qnx_imagefs_file(output, "lib64/graphics.conf");

            if (actual.substr(0, configuration.size()) != configuration || actual.size() != 1119 ||
                sha256_bytes(read_qnx_imagefs_file(output, "lib64/graphics.old")) !=
                        "c02066b2f5d7e0dc14d21ad1911baf53fa289d93f2c4a2c60644b3428d81d178") {
                throw std::runtime_error("A11 duplicate graphics config replacement");
            }

            rejects([&] {
                prepare_qnx_qinggan11_display_config(output, configuration);
            });
            rejects([&] {
                prepare_qnx_qinggan11_display_config(display, std::string(1120, 'x'));
            });
            auto wrong = display;
            wrong[100] ^= 1;
            rejects([&] {
                prepare_qnx_qinggan11_display_config(wrong, configuration);
            });
            std::cout
                    << "Exact A11 duplicate graphics slots resolved and owned config applied; SHA256 "
                    << sha256_bytes(output) << '\n';

            return 0;
        }

        std::cout << "A11 display adapter guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
