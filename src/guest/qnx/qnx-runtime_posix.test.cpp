#include <cassert>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
namespace fs = std::filesystem;

static std::string read(const fs::path &p) {
    std::ifstream f(p);

    return {std::istreambuf_iterator<char>(f), {}};
}

static void write(const fs::path &p, const std::string &s) {
    std::ofstream f(p);

    f << s;
    assert(f.good());
}

struct Result {
    int code;
    std::string output;
};

static Result run(const fs::path &script, const std::vector<std::string> &args,
                  const std::string &mode, const fs::path &self, const fs::path &recorder,
                  bool error = false) {
    int pipefd[2];

    assert(pipe(pipefd) == 0);
    const pid_t child = fork();
    assert(child >= 0);

    if (!child) {
        if (setpgid(0, 0) != 0) {
            _exit(126);
        }

        close(pipefd[0]);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[1]);
        unsetenv("VOYEMU_QNX_MCD_DIAGNOSTIC");
        unsetenv("VOYEMU_QNX_INITIAL_NATIVE_DELAY");
        unsetenv("VOYEMU_QBW_EXTERNAL");

        if (!mode.empty()) {
            setenv("VOYEMU_QBW_EXTERNAL", mode.c_str(), 1);
        }

        setenv("VOYEMU_TEST_SELF", self.c_str(), 1);
        setenv("VOYEMU_TEST_RECORD", recorder.c_str(), 1);
        setenv("VOYEMU_TEST_SCRIPT", script.c_str(), 1);
        setenv("VOYEMU_TEST_ERROR", error ? "1" : "0", 1);
        std::vector<std::string> values{"/bin/bash", script.string()};
        values.insert(values.end(), args.begin(), args.end());
        std::vector<char *> argv;

        for (auto &s : values) {
            argv.push_back(s.data());
        }

        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(126);
    }

    int status = 0;
    bool done = false;
    const int grouped = setpgid(child, child);

    // A fast fixture may already be terminal; ESRCH is accepted only with an exact child reap.
    if (grouped < 0 && errno == ESRCH) {
        assert(waitpid(child, &status, WNOHANG) == child);
        done = true;
    } else {
        assert(grouped == 0 || errno == EACCES);
    }

    close(pipefd[1]);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    std::string out;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);

    while (!done && std::chrono::steady_clock::now() < end) {
        char buffer[4096];
        ssize_t n;

        while ((n = ::read(pipefd[0], buffer, sizeof buffer)) > 0) {
            out.append(buffer, n);
        }

        if (waitpid(child, &status, WNOHANG) == child) {
            done = true;
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    if (!done) {
        kill(-child, SIGKILL);
        waitpid(child, &status, 0);
    }

    char buffer[4096];
    ssize_t n;

    while ((n = ::read(pipefd[0], buffer, sizeof buffer)) > 0) {
        out.append(buffer, n);
    }

    close(pipefd[0]);
    assert(done);

    return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), out};
}

static std::string mock() {
    return R"SH(
function [ { if builtin [ "$#" -eq 3 ] && builtin [ "$1" = -r ]; then
 case "$2" in /dev/hd0) return 1;; /usr/bin/gauge|/usr/share/theme/application_gauge.cfg|/lib64/libkzcoreui.so) return 0;; *) return 125;; esac
 fi; builtin [ "$@"; }
function echo { builtin echo "$@"; if builtin [ "$VOYEMU_TEST_ERROR" = 1 ] && builtin [ "$*" = 'QMC FILE present=/lib64/libkzcoreui.so' ]; then return 7; fi; }
)SH";
}

static std::string native_mock() {
    return R"SH(
qm_native_external() {
 "$VOYEMU_TEST_SELF" --record "$VOYEMU_TEST_RECORD" "$@" || return 125
 /bin/bash "$VOYEMU_TEST_SCRIPT" --voyemu-files-only &
 qm_test_child=$!
 wait "$qm_test_child"; qm_test_rc=$?
 if kill -0 "$qm_test_child" 2>/dev/null; then echo CHILD_LEFT; return 125; fi
 echo "CHILD_REAPED rc=$qm_test_rc"
 return "$qm_test_rc"
}
)SH";
}

static void mcd_tests(const std::string &original, const fs::path &temp, const fs::path &self,
                      const fs::path &record, bool defaults_only) {
    const auto start = original.find("qm_wait_core_ipc() {"),
               end = original.find("qm_wait_window() {", start);

    assert(start != std::string::npos && end != std::string::npos);
    const auto helper = original.find("qm_mcd_predicate() {");
    const auto at = helper != std::string::npos && helper < start ? helper : start;
    const auto functions = original.substr(at, end - at);
    const auto sock = temp / "mcd.sock", missing = temp / "missing.sock",
               regular = temp / "regular.sock", pidfile = temp / "mcd.pid",
               script = temp / "mcd-test.sh";
    const int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    assert(fd >= 0);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    assert(sock.string().size() < sizeof address.sun_path);
    std::strcpy(address.sun_path, sock.c_str());
    assert(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    write(regular, "regular");
    auto quoted = [](const fs::path &value) {
        assert(value.string().find(char(39)) == std::string::npos);

        return "'" + value.string() + "'";
    };
    auto invoke = [&](const fs::path &path, bool nonempty, const std::string &diagnostic,
                      const std::string &extra = "", bool dead = false,
                      const std::string &tag = "mcd") {
        write(pidfile, nonempty ? "1" : "");
        const std::string env = diagnostic.empty()
                                        ? "unset VOYEMU_QNX_MCD_DIAGNOSTIC\n"
                                        : "export VOYEMU_QNX_MCD_DIAGNOSTIC=" + diagnostic + "\n";
        write(script,
              functions + env +
                      "qm_test_sleeps=0\nfunction /bin/sleep { qm_test_sleeps=$((qm_test_sleeps+1)); " +
                      (extra.empty() ? std::string(":") : extra) + "; }\n" +
                      (dead ? "function kill { return 1; }\n" : "") + "qm_wait_core_ipc $$ " +
                      quoted(path) + " " + quoted(pidfile) + " " + tag +
                      "\nqm_test_rc=$?\necho SLEEPS=$qm_test_sleeps\nexit \"$qm_test_rc\"\n");

        return run(script, {}, "", self, record);
    };

    for (const auto &path : {missing, regular, sock}) {
        for (bool nonempty : {false, true}) {
            const auto actual = invoke(path, nonempty, "");
            const bool ready = path == sock && nonempty;
            assert(actual.code == (ready ? 0 : 1));
            assert(actual.output == (ready ? "QIP IPC service-ready=mcd\nSLEEPS=0\n"
                                           : "QIP FAIL IPC-readiness-timeout=mcd\nSLEEPS=160\n"));
        }
    }

    const auto default_dead = invoke(sock, true, "", "", true);
    assert(default_dead.code == 1 &&
           default_dead.output == "QIP FAIL IPC-parent-exited=mcd\nSLEEPS=0\n");

    if (defaults_only) {
        close(fd);
        std::cout << "MCD default transcript: socket/type/size/owner and160-poll parity PASS\n";

        return;
    }

    auto count = [](const std::string &value, const std::string &token) {
        unsigned n = 0;

        for (std::size_t at = 0; (at = value.find(token, at)) != std::string::npos;
             at += token.size()) {
            ++n;
        }

        return n;
    };

    for (const auto &path : {missing, regular, sock}) {
        for (bool nonempty : {false, true}) {
            const auto actual = invoke(path, nonempty, "1");
            const bool ready = path == sock && nonempty;
            assert(actual.code == (ready ? 0 : 1));
            const auto flags = path == sock ? (nonempty ? "socket_S=1 pidfile_s=1"
                                                        : "socket_S=1 pidfile_s=0")
                                            : "socket_S=0 pidfile_s=not_tested";
            assert(actual.output.find(std::string("owner_alive=1 ") + flags +
                                      " attempt=0 phase=snapshot") != std::string::npos);
            assert(count(actual.output, "QIP IPC predicate=mcd") == unsigned(ready ? 1 : 2));

            if (!ready) {
                assert(actual.output.find("attempt=159 phase=final") != std::string::npos &&
                       actual.output.find("SLEEPS=160") != std::string::npos);
            }
        }
    }

    const auto dead = invoke(sock, true, "1", "", true);
    assert(dead.code == 1 &&
           dead.output.find(
                   "owner_alive=0 socket_S=not_tested pidfile_s=not_tested attempt=0 phase=final") !=
                   std::string::npos &&
           dead.output.find("SLEEPS=0") != std::string::npos);
    const auto invalid = invoke(sock, true, "bad");
    assert(invalid.code == 125 && invalid.output == "QIP FAIL IPC-diagnostic-mode=mcd\nSLEEPS=0\n");
    const auto rpcd = invoke(sock, true, "bad", "", false, "rpcd");
    assert(rpcd.code == 0 && rpcd.output == "QIP IPC service-ready=rpcd\nSLEEPS=0\n");
    const auto transition =
            invoke(missing, false, "1",
                   "qm_core_socket=" + quoted(sock) + "; builtin printf 1 > " + quoted(pidfile));
    assert(transition.code == 0 && count(transition.output, "QIP IPC predicate=mcd") == 2 &&
           transition.output.find("socket_S=1 pidfile_s=1 attempt=1") != std::string::npos &&
           transition.output.find("SLEEPS=1") != std::string::npos);
    const auto cycle = invoke(sock, false, "1",
                              "if builtin [ \"$qm_core_socket\" = " + quoted(sock) +
                                      " ]; then qm_core_socket=" + quoted(missing) +
                                      "; else qm_core_socket=" + quoted(sock) + "; fi");
    assert(cycle.code == 1 && count(cycle.output, "QIP IPC predicate=mcd") == 9 &&
           cycle.output.find("attempt=159 phase=final") != std::string::npos);
    close(fd);
    std::cout
            << "MCD actual Bash/socket predicates, mocked owner/sleep, coalesced8+final, default parity PASS; guest evidence unverified\n";
}

static void driver_tests(const std::string &original, const fs::path &temp, const fs::path &self,
                         const fs::path &record, bool default_only) {
    const auto begin = original.find("qm_wait_ram_driver() {"),
               end = original.find("qm_wait_oem_mount() {", begin),
               helper = original.find("qm_driver_native_delay() (");

    assert(begin != std::string::npos && end != std::string::npos);
    const auto at = helper != std::string::npos && helper < begin ? helper : begin;
    const auto functions = original.substr(at, end - at);
    const auto script = temp / "driver.sh";
    auto invoke = [&](const std::string &mode, unsigned ready_at, bool fail = false) {
        const auto env = mode.empty() ? "unset VOYEMU_QNX_DRIVER_NATIVE_DELAY\n"
                                      : "export VOYEMU_QNX_DRIVER_NATIVE_DELAY=" + mode + "\n";
        write(script,
              functions + env +
                      "qm_test_steps=0\nqm_test_native=0\nqm_test_legacy=0\nqm_test_ready=" +
                      std::to_string(ready_at) + R"SH(
function [ { if builtin [ "$#" -eq 3 ] && builtin [ "$1" = -x ] && builtin [ "$2" = /proc/boot/bin/devb-ram ]; then builtin [ "$qm_test_steps" -ge "$qm_test_ready" ]; else builtin [ "$@"; fi; }
function /bin/sleep { builtin [ "$#" -eq 1 ] && builtin [ "$1" = 0.1 ] || return 125; qm_test_legacy=$((qm_test_legacy+1));qm_test_steps=$((qm_test_steps+1)); }
qm_driver_native_delay() { qm_test_native=$((qm_test_native+1));qm_test_steps=$((qm_test_steps+1));
)SH" + (fail ? "return 125\n" : "return 0\n") +
                      "}\nqm_wait_ram_driver\nqm_test_rc=$?\necho COUNTS=$qm_test_native:$qm_test_legacy:$qm_test_steps\nexit \"$qm_test_rc\"\n");

        return run(script, {}, "", self, record);
    };

    for (unsigned ready : {0U, 2U, 161U}) {
        const auto old = invoke("", ready);
        const unsigned count = ready <= 160 ? ready : 160;
        assert(old.code == (ready < 160 ? 0 : 1));
        assert(old.output ==
               (ready < 160
                        ? "QMC RAM driver-ready=1\n"
                        : "QMC RAM path=/proc/boot/bin/devb-ram readable=0 executable=0\nQMC FAIL RAM-driver-timeout\n") +
                       std::string("COUNTS=0:") + std::to_string(count) + ":" +
                       std::to_string(count) + "\n");
    }

    if (default_only) {
        std::cout << "driver default transcript/160attempts unchanged PASS\n";

        return;
    }

    for (unsigned ready : {0U, 2U, 161U}) {
        const auto actual = invoke("1", ready);
        const unsigned count = ready <= 160 ? ready : 160;
        assert(actual.code == (ready < 160 ? 0 : 1));
        assert(actual.output.find("COUNTS=" + std::to_string(count) +
                                  ":0:" + std::to_string(count) + "\n") != std::string::npos);
    }

    const auto bad = invoke("bad", 0);
    assert(bad.code == 125 && bad.output == "QMC FAIL RAM-delay-mode\nCOUNTS=0:0:0\n");
    const auto fail = invoke("1", 2, true);
    assert(fail.code == 125 && fail.output == "QMC FAIL RAM-native-delay\nCOUNTS=1:0:1\n");
    assert(helper != std::string::npos);
    const auto helper_end = original.find("qm_wait_ram_driver() {", helper);
    const auto actual_helper = original.substr(helper, helper_end - helper);
    write(script, actual_helper + R"SH(
export VOYEMU_QBW_ARG_00=stale VOYEMU_QBW_ARG_15=stale
function /proc/boot/bin/ksh { builtin [ "$#" -eq 2 ] && builtin [ "$1" = -c ] && builtin [ "$2" = 'exit 125' ] || return 125; builtin [ "$VOYEMU_QBW_ENABLE" = driver-delay100ms-v1 ] && builtin [ "$VOYEMU_QBW_ARGC" = 0 ] && builtin [ "$LD_PRELOAD" = /proc/boot/bin/Snapdragon.rgb ] && builtin [ "${VOYEMU_QBW_ARG_00+x}" != x ] && builtin [ "${VOYEMU_QBW_ARG_15+x}" != x ] || return 125; echo TRANSPORT_EXACT; }
function exec { "$@"; }
qm_driver_native_delay
qm_test_rc=$?
qm_driver_native_delay extra
qm_test_bad=$?
echo RC=$qm_test_rc:$qm_test_bad
)SH");
    const auto transport = run(script, {}, "", self, record);
    assert(transport.code == 0 && transport.output == "TRANSPORT_EXACT\nRC=0:125\n");
    std::cout
            << "driver native opt-in only/default parity/160 attempts/failure/strict transport PASS; guest unverified\n";
}

int main(int argc, char **argv) try {
    if (argc >= 3 && std::string(argv[1]) == "--record") {
        std::ofstream f(argv[2]);

        for (int i = 3; i < argc; i++) {
            f << argv[i] << '\n';
        }

        return f.good() ? 0 : 125;
    }

    const bool driver_only = argc == 4 && std::string(argv[1]) == "--source" &&
                             std::string(argv[3]) == "--driver-only";
    const bool driver_default = argc == 4 && std::string(argv[1]) == "--source" &&
                                std::string(argv[3]) == "--driver-default-only";
    const bool mcd_only = argc == 4 && std::string(argv[1]) == "--source" &&
                          std::string(argv[3]) == "--mcd-only";
    const bool mcd_default_only = argc == 4 && std::string(argv[1]) == "--source" &&
                                  std::string(argv[3]) == "--mcd-default-only";
    const bool default_only = argc == 4 && std::string(argv[1]) == "--source" &&
                              std::string(argv[3]) == "--default-only";
    const auto source = argc >= 3 && std::string(argv[1]) == "--source"
                                ? fs::path(argv[2])
                                : fs::path(__FILE__).parent_path() / "qnx-runtime.sh";
    const auto original = read(source);
    assert(original.size() > 10000 && original.size() < 32768);
    const std::string main_marker =
            "# devb-ram arrives with ifs_disk after this display script starts.";
    const auto at = original.find(main_marker);
    assert(at != std::string::npos && original.find(main_marker, at + 1) == std::string::npos);
    const auto prefix = original.substr(0, at);
    const auto call = original.find("qm_bounded files qm_files", at);
    assert(call != std::string::npos);
    std::string route = "qm_bounded files qm_files\n";
    const auto branch = original.rfind("if [ \"${VOYEMU_QBW_EXTERNAL-}\" = 1 ]; then", call);

    if (branch != std::string::npos && branch > at) {
        const auto end = original.find("\nfi", call);
        assert(end != std::string::npos);
        route = original.substr(branch, end + 3 - branch) + "\n";
    }

    const auto root =
            fs::canonical(__FILE__).parent_path().parent_path().parent_path().parent_path();
    assert(fs::is_regular_file(root / "src/guest/qnx/qnx-runtime.sh"));
    const auto temp = fs::canonical(fs::temp_directory_path()) /
                      ("qnx-" + std::to_string(getpid()));
    assert(!fs::exists(temp));
    fs::create_directory(temp);
    const auto self = fs::canonical(argv[0]);
    const auto script = temp / "script.sh", record = temp / "argv.txt";
    write(script, mock() + prefix + native_mock() + "\necho MAIN_REACHED\n" + route +
                          "qm_test_rc=$?\necho \"END_RC=$qm_test_rc\"\nexit \"$qm_test_rc\"\n");

    if (driver_only || driver_default) {
        driver_tests(original, temp, self, record, driver_default);
        fs::remove_all(temp);

        return 0;
    }

    if (mcd_only || mcd_default_only) {
        mcd_tests(original, temp, self, record, mcd_default_only);
        fs::remove_all(temp);

        return 0;
    }

    const std::vector<std::string> paths{"/dev/hd0", "/usr/bin/gauge",
                                         "/usr/share/theme/application_gauge.cfg",
                                         "/lib64/libkzcoreui.so"};
    std::string expected;

    for (const auto &p : paths) {
        expected += "QMC CHECK file=" + p + "\nQMC FILE " +
                    (p == "/dev/hd0" ? "absent=" : "present=") + p + "\n";
    }

    const std::string ordinary_expected =
            "QMC BEGIN raw-OEM-QNX6-readonly\nQMC WAIT initial_sleep before\nQMC WAIT initial_sleep after\nMAIN_REACHED\nQMC CHILD begin=files\n" +
            expected + "QMC CHILD exit=files code=0\nEND_RC=0\n";

    if (default_only) {
        const auto old = run(script, {}, "", self, record);
        assert(old.code == 0 && old.output == ordinary_expected);
        std::cout << "legacy default byte transcript PASS\n";
        fs::remove_all(temp);

        return 0;
    }

    auto entry = run(script, {"--voyemu-files-only"}, "", self, record);
    assert(entry.code == 0 && entry.output.find("MAIN_REACHED") == std::string::npos);
    assert(entry.output == expected);

    for (const auto &args : std::vector<std::vector<std::string>>{{"--voyemu-files-only", "extra"},
                                                                  {"--voyemu-files-invalid"}}) {
        const auto bad = run(script, args, "", self, record);
        assert(bad.code == 125 && bad.output.find("MAIN_REACHED") == std::string::npos);
    }

    const auto error = run(script, {"--voyemu-files-only"}, "", self, record, true);
    assert(error.code == 7 && error.output == expected);
    const auto ordinary = run(script, {}, "", self, record);
    assert(ordinary.code == 0 && ordinary.output == ordinary_expected && !fs::exists(record));
    const auto invalid = run(script, {}, "bad", self, record);
    assert(invalid.code == 125 && invalid.output.find("QMC CHECK") == std::string::npos &&
           !fs::exists(record));
    const auto enabled = run(script, {}, "1", self, record);
    assert(enabled.code == 0 && enabled.output.find("CHILD_REAPED rc=0") != std::string::npos);
    assert(read(record) == "/proc/boot/bin/ksh\n/proc/boot/bin/ssplash\n--voyemu-files-only\n");
    assert(enabled.output.find(expected) != std::string::npos);
    const auto fail = run(script, {}, "1", self, record, true);
    assert(fail.code == 7 && fail.output.find("CHILD_REAPED rc=7") != std::string::npos &&
           fail.output.find("QMC CHILD exit=files code=7") != std::string::npos);
    driver_tests(original, temp, self, record, false);
    mcd_tests(original, temp, self, record, false);
    std::cout
            << "actual /bin/bash: reserved entry/default/native argv/error rc/direct-child cleanup PASS\n";
    fs::remove_all(temp);

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
} catch (...) {
    std::cerr << "unexpected non-standard exception\n";

    return 1;
}
