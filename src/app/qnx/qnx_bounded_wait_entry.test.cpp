#include <cassert>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#define QBW_ENTRY_TEST
#include "../../guest/qnx/bounded_wait_entry.c"
struct Exit : std::exception {
    explicit Exit(int value) : code(value) {}

    int code;
};
static std::map<std::string, std::string> environment;
static std::vector<std::string> environment_storage, spawn_arguments, spawn_environment;
static std::vector<char *> environment_pointers;
char **qbw_environ = nullptr;
static QbwTime fake_time, exit_at, write_advance;
static std::vector<QbwTime> kill_times;
static int clock_calls, pause_calls, clock_fail_at, pause_failure_at;
static bool clock_bad_time, clock_reverse_after_zero, clock_parent_change, clock_fractional_short;
static int spawn_calls, status_value, spawn_failure, poll_errno, poll_interrupts, error_value,
        parent, spawned_pid, kill_error;
static std::string write_log;
static bool write_failure;
static bool term_exits, reaped, parent_changes, clock_backwards, kill_error_exited;
static std::vector<int> signals;

char *qbw_getenv(const char *k) {
    auto i = environment.find(k);

    return i == environment.end() ? nullptr : i->second.data();
}

int *qbw_errno() {
    return &error_value;
}

int qbw_getpid() {
    return 37;
}

int qbw_getppid() {
    return (parent_changes && spawn_calls) || (clock_parent_change && pause_calls) ? 1 : parent;
}

int qbw_clock(int which, QbwTimespec *t) {
    assert(which == 2);
    ++clock_calls;

    if (clock_calls == clock_fail_at) {
        error_value = 5;

        return -1;
    }

    QbwTime time = clock_backwards && spawn_calls            ? 0
                   : clock_reverse_after_zero && pause_calls ? fake_time - 1
                                                             : fake_time;
    t->seconds = (long long)(time / 1000000000ULL);
    t->nanoseconds = clock_bad_time ? 1000000000LL : (long long)(time % 1000000000ULL);

    return 0;
}

int qbw_nanosleep(const QbwTimespec *t, QbwTimespec *) {
    assert(t->seconds == 0 && t->nanoseconds >= 0 && t->nanoseconds <= 100000000);
    ++pause_calls;

    if (pause_calls == pause_failure_at) {
        error_value = 4;

        return -1;
    }

    if (!clock_fractional_short) {
        fake_time += (QbwTime)t->nanoseconds;
    }

    return 0;
}

int qbw_spawn(int *pid, const char *path, const void *actions, const void *attributes,
              char *const *argv, char *const *envp) {
    assert(!actions && !attributes && envp && argv && path);
    ++spawn_calls;

    if (spawn_failure) {
        return spawn_failure;
    }

    *pid = spawned_pid;

    for (unsigned i = 0; argv[i]; ++i) {
        spawn_arguments.emplace_back(argv[i]);
    }

    for (unsigned i = 0; envp[i]; ++i) {
        spawn_environment.emplace_back(envp[i]);
    }

    return 0;
}

int qbw_waitpid(int pid, int *status, int options) {
    assert(pid == spawned_pid && pid > 0);
    assert(options == 0x40 || options == 0);

    if (poll_errno) {
        error_value = poll_errno;

        return -1;
    }

    if (poll_interrupts) {
        --poll_interrupts;
        error_value = 4;

        return -1;
    }

    if (!options || fake_time >= exit_at) {
        reaped = !options || ((status_value & 0x7f) != 127);
        *status = status_value;

        return pid;
    }

    return 0;
}

int qbw_kill(int pid, int sig) {
    assert(pid == spawned_pid && pid > 0 && !reaped);
    assert(sig == 15 || sig == 9);
    signals.push_back(sig);
    kill_times.push_back(fake_time);

    if (sig == 9 && kill_error) {
        if (kill_error_exited) {
            exit_at = fake_time;
        }

        error_value = 1;

        return -1;
    }

    if ((sig == 15 && term_exits) || sig == 9) {
        exit_at = fake_time;
    }

    return 0;
}

long qbw_write(int fd, const void *data, unsigned long size) {
    assert(fd == 2);

    if (write_failure) {
        error_value = 5;

        return -1;
    }

    fake_time += write_advance;
    write_log.append((const char *)data, size);

    return (long)size;
}

void qbw_exit(int code) {
    throw Exit{code};
}

static void reset() {
    clock_calls = 0;
    pause_calls = 0;
    clock_fail_at = 0;
    pause_failure_at = 0;
    clock_bad_time = false;
    clock_reverse_after_zero = false;
    clock_parent_change = false;
    clock_fractional_short = false;
    environment = {{"VOYEMU_QBW_ENABLE", "1"},
                   {"VOYEMU_QBW_ARGC", "1"},
                   {"VOYEMU_QBW_ARG_00", "/bin/true"}};
    fake_time = 0;
    exit_at = 0;
    write_advance = 0;
    kill_times.clear();
    spawn_calls = 0;
    status_value = 0;
    spawn_failure = 0;
    poll_errno = 0;
    poll_interrupts = 0;
    error_value = 0;
    parent = 91;
    spawned_pid = 73;
    kill_error = 0;
    kill_error_exited = false;
    term_exits = false;
    reaped = false;
    parent_changes = false;
    clock_backwards = false;
    signals.clear();
    spawn_arguments.clear();
    spawn_environment.clear();
    environment_storage.clear();
    environment_pointers.clear();
    qbw_environ = nullptr;
    write_log.clear();
    write_failure = false;
}

static void set_environment(std::vector<std::string> values = {}) {
    environment_storage = std::move(values);

    for (const auto &[k, v] : environment) {
        environment_storage.push_back(k + "=" + v);
    }

    environment_pointers.clear();

    for (auto &v : environment_storage) {
        environment_pointers.push_back(v.data());
    }

    environment_pointers.push_back(nullptr);
    qbw_environ = environment_pointers.data();
}

static int run() {
    try {
        qbw_start();

        return -1;
    } catch (const Exit &e) {
        return e.code;
    }
}

int main() try {
    {
        reset();
        environment.erase("VOYEMU_QBW_ENABLE");
        set_environment();
        assert(run() == -1 && spawn_calls == 0);
    }
    {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "2";
        set_environment();
        assert(run() == 125 && !spawn_calls);
    }
    {
        reset();
        environment["VOYEMU_QBW_ARGC"] = "3";
        environment["VOYEMU_QBW_ARG_01"] = "literal spaces $() ;\n";
        environment["VOYEMU_QBW_ARG_02"] = "";
        set_environment(
                {"PATH=/bin", "LD_PRELOAD=/own/candidate.so", "VOYEMU_QBW_EXTERNAL=1", "KEEP=ok"});
        status_value = 7 << 8;
        assert(run() == 7 && spawn_calls == 1 && reaped && signals.empty());
        assert((spawn_arguments ==
                std::vector<std::string>{"/bin/true", "literal spaces $() ;\n", ""}));
        assert((spawn_environment == std::vector<std::string>{"PATH=/bin", "KEEP=ok"}));
        assert(write_log.find("QBW child=73 owner=37 parent=91\n") != std::string::npos);
        assert(write_log.find("QBW reaped=1 cleanup_unverified=0") != std::string::npos);
    }
    {
        reset();
        spawn_failure = 2;
        set_environment();
        assert(run() == 127 && signals.empty());
    }
    {
        reset();
        poll_errno = 10;
        set_environment();
        assert(run() == 125 && !reaped && signals.empty());
    }
    {
        reset();
        poll_interrupts = 3;
        exit_at = 20000000;
        set_environment();
        assert(run() == 0 && reaped && signals.empty());
    }
    {
        reset();
        status_value = 9 | 128;
        set_environment();
        assert(run() == 137 && reaped && signals.empty());
    }
    {
        reset();
        exit_at = ~QbwTime(0);
        set_environment();
        assert(run() == 124 && fake_time == QBW_DEADLINE_NS + QBW_TERM_GRACE_NS && reaped);
        assert((signals == std::vector<int>{15, 9}));
    }
    {
        reset();
        exit_at = ~QbwTime(0);
        term_exits = true;
        set_environment();
        assert(run() == 124 && fake_time == QBW_DEADLINE_NS && reaped);
        assert((signals == std::vector<int>{15}));
    }
    {
        reset();
        parent_changes = true;
        exit_at = ~QbwTime(0);
        set_environment();
        assert(run() == 125 && reaped && (signals == std::vector<int>{9}));
    }
    {
        reset();
        parent_changes = true;
        set_environment();
        assert(run() == 125 && reaped && signals.empty());
    }
    {
        reset();
        status_value = 127;
        set_environment();
        assert(run() == 125 && reaped && (signals == std::vector<int>{9}));
    }
    {
        reset();
        environment["VOYEMU_QBW_ARG_00"] = "qm_files";
        set_environment();
        assert(run() == 125 && !spawn_calls);
    }

    for (const std::string count : {"0", "01", "17", "-1", "1x", "999"}) {
        reset();
        environment["VOYEMU_QBW_ARGC"] = count;
        set_environment();
        assert(run() == 125 && !spawn_calls);
    }

    {
        reset();
        environment["VOYEMU_QBW_ARGC"] = "16";

        for (unsigned i = 1; i < 16; ++i) {
            char key[24];
            qbw_arg_key(i, key);
            environment[key] = "arg";
        }

        set_environment();
        assert(run() == 0 && spawn_arguments.size() == 16);
    }
    {
        reset();
        environment["VOYEMU_QBW_ARGC"] = "4";
        environment["VOYEMU_QBW_ARG_00"] = "/" + std::string(1022, 'x');

        for (unsigned i = 1; i < 4; ++i) {
            char key[24];
            qbw_arg_key(i, key);
            environment[key] = std::string(1023, 'x');
        }

        set_environment();
        assert(run() == 0 && spawn_arguments.size() == 4);
    }
    {
        reset();
        environment["VOYEMU_QBW_ARG_01"] = "unexpected";
        set_environment();
        assert(run() == 125 && !spawn_calls);
    }
    {
        reset();
        environment["VOYEMU_QBW_ARG_00"] = "/" + std::string(1024, 'x');
        set_environment();
        assert(run() == 125 && !spawn_calls);
    }
    {
        reset();
        environment["VOYEMU_QBW_ARGC"] = "5";

        for (unsigned i = 1; i < 5; ++i) {
            char key[24];
            qbw_arg_key(i, key);
            environment[key] = std::string(1024, 'x');
        }

        set_environment();
        assert(run() == 125 && !spawn_calls);
    }
    {
        reset();
        set_environment({"DUP=1", "DUP=2"});
        assert(run() == 125 && !spawn_calls);
    }
    {
        reset();
        set_environment({"MALFORMED"});
        assert(run() == 125 && !spawn_calls);
    }
    {
        reset();
        std::vector<std::string> values;

        for (unsigned i = 0; i < 129; ++i) {
            values.push_back("K" + std::to_string(i) + "=v");
        }

        set_environment(std::move(values));
        assert(run() == 125 && !spawn_calls);
    }
    {
        reset();
        kill_error = 1;
        kill_error_exited = true;
        exit_at = ~QbwTime(0);
        set_environment();
        assert(run() == 125 && reaped);
    }
    {
        reset();
        kill_error = 1;
        exit_at = ~QbwTime(0);
        set_environment();
        assert(run() == 125 && !reaped);
    }
    {
        reset();
        clock_backwards = true;
        fake_time = 1000000000;
        exit_at = ~QbwTime(0);
        set_environment();
        assert(run() == 125 && reaped && (signals == std::vector<int>{9}));
    }
    {
        reset();
        write_failure = true;
        set_environment();
        assert(run() == 125 && reaped && signals.empty());
    }
    {
        reset();
        write_advance = 10000000000ULL;
        exit_at = ~QbwTime(0);
        set_environment();
        assert(run() == 124 && reaped);

        if (kill_times !=
            std::vector<QbwTime>{QBW_DEADLINE_NS, QBW_DEADLINE_NS + QBW_TERM_GRACE_NS}) {
            std::cerr << "trace write extended child watchdog deadline\n";

            return 1;
        }
    }

    for (const std::string count : {"0", "1", "00", "-1"}) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "clock-zero-fractional-v1";
        environment["VOYEMU_QBW_ARGC"] = count;
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        const int code = run();
        assert(code == (count == "0" ? 0 : 125) && !spawn_calls && signals.empty());

        if (count == "0") {
            assert(clock_calls == 3 && pause_calls == 2 &&
                   write_log.find("QBC clock0 pid=37 parent=91 ns=0\n") != std::string::npos &&
                   write_log.find("QBC clock1 pid=37 parent=91 ns=0\n") != std::string::npos &&
                   write_log.find("QBC clock2 pid=37 parent=91 ns=10000000\n") !=
                           std::string::npos &&
                   write_log.find("QBC complete\n") != std::string::npos);
        }
    }

    for (const std::string key : {"VOYEMU_QBW_ARG_00", "VOYEMU_QBW_ARG_16", "VOYEMU_QBW_ARG_99"}) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "clock-zero-fractional-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment.erase("VOYEMU_QBW_ARG_00");
        environment[key] = "unexpected";
        set_environment();
        assert(run() == 125 && clock_calls == 0 && pause_calls == 0 && !spawn_calls);
    }

    for (unsigned fault = 0; fault < 7; ++fault) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "clock-zero-fractional-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();

        if (fault == 0) {
            clock_fail_at = 1;
        }

        if (fault == 1) {
            clock_bad_time = true;
        }

        if (fault == 2) {
            clock_reverse_after_zero = true;
            fake_time = 100;
        }

        if (fault == 3) {
            pause_failure_at = 1;
        }

        if (fault == 4) {
            pause_failure_at = 2;
        }

        if (fault == 5) {
            clock_parent_change = true;
        }

        if (fault == 6) {
            clock_fractional_short = true;
        }

        assert(run() == 125 && !spawn_calls && signals.empty() &&
               write_log.find("QBC complete") == std::string::npos);

        if (fault == 4) {
            assert(write_log.find("QBC clock1") != std::string::npos &&
                   write_log.find("QBC clock2") == std::string::npos);
        }
    }

    {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "clock-zero-fractional-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        write_advance = 10000000000ULL;
        assert(run() == 0 && pause_calls == 2 && fake_time == 40010000000ULL);
        assert(write_log.find("QBC clock2 pid=37 parent=91 ns=20010000000\n") != std::string::npos);
    }

    for (const std::string count : {"0", "1", "00", "-1"}) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "driver-delay100ms-v1";
        environment["VOYEMU_QBW_ARGC"] = count;
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        const int code = run();
        assert(code == (count == "0" ? 0 : 125) && !spawn_calls && signals.empty());

        if (count == "0") {
            assert(clock_calls == 2 && pause_calls == 1 && fake_time == 100000000ULL);
            assert(write_log.find(
                           "QBD delay100ms-complete pid=37 parent=91 before_ns=0 after_ns=100000000\n") !=
                   std::string::npos);
        }
    }

    for (unsigned fault = 0; fault < 7; ++fault) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "driver-delay100ms-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment.erase("VOYEMU_QBW_ARG_00");

        if (fault == 0) {
            environment["VOYEMU_QBW_ARG_00"] = "unexpected";
        }

        if (fault == 1) {
            clock_fail_at = 1;
        }

        if (fault == 2) {
            clock_bad_time = true;
        }

        if (fault == 3) {
            pause_failure_at = 1;
        }

        if (fault == 4) {
            clock_parent_change = true;
        }

        if (fault == 5) {
            clock_fractional_short = true;
        }

        if (fault == 6) {
            write_failure = true;
        }

        set_environment();
        assert(run() == 125 && !spawn_calls && signals.empty());
    }

    for (const std::string count : {"0", "1", "00", "-1"}) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "initial-delay10ms-v1";
        environment["VOYEMU_QBW_ARGC"] = count;
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        assert(run() == (count == "0" ? 0 : 125) && !spawn_calls && signals.empty());

        if (count == "0") {
            assert(clock_calls == 2 && pause_calls == 1 && fake_time == 10000000ULL &&
                   write_log ==
                           "QBI delay10ms-complete pid=37 parent=91 before_ns=0 after_ns=10000000\n");
        }
    }

    for (unsigned fault = 0; fault < 8; ++fault) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "initial-delay10ms-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment.erase("VOYEMU_QBW_ARG_00");

        if (fault == 0) {
            environment["VOYEMU_QBW_ARG_16"] = "stale";
        }

        if (fault == 1) {
            clock_fail_at = 1;
        }

        if (fault == 2) {
            clock_bad_time = true;
        }

        if (fault == 3) {
            pause_failure_at = 1;
        }

        if (fault == 4) {
            clock_parent_change = true;
        }

        if (fault == 5) {
            clock_fractional_short = true;
        }

        if (fault == 6) {
            write_failure = true;
        }

        if (fault == 7) {
            parent = 1;
        }

        set_environment();
        assert(run() == 125 && !spawn_calls && signals.empty());
    }

    for (const std::string mode : {"initial-delay10ms-v1", "driver-delay100ms-v1"}) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = mode;
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment["VOYEMU_QBW_TRACE_DELAY"] = "1";
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        const auto result = run();

        if (result != 0 ||
            write_log.find("QBP phase=entry pid=37 parent=91\n") == std::string::npos) {
            std::cerr << "opt-in delay phases missing\n";

            return 1;
        }

        std::size_t cursor = 0;

        for (const std::string phase :
             {"entry", "clock_before", "clock_after", "nanosleep_before", "nanosleep_after",
              "end_clock_before", "end_clock_after", "before_exit"}) {
            const auto at = write_log.find("QBP phase=" + phase + " ", cursor);
            assert(at != std::string::npos);
            cursor = at + 1;
        }

        assert(clock_calls == 2 && pause_calls == 1 && !spawn_calls && signals.empty());
        assert(fake_time == (mode == "initial-delay10ms-v1" ? 10000000ULL : 100000000ULL));
    }

    for (unsigned fault = 0; fault < 5; ++fault) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "initial-delay10ms-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment["VOYEMU_QBW_TRACE_DELAY"] = "1";
        environment.erase("VOYEMU_QBW_ARG_00");

        if (fault == 0) {
            clock_fail_at = 1;
        }

        if (fault == 1) {
            clock_fail_at = 2;
        }

        if (fault == 2) {
            pause_failure_at = 1;
        }

        if (fault == 3) {
            clock_parent_change = true;
        }

        if (fault == 4) {
            write_failure = true;
        }

        set_environment();
        assert(run() == 125 && !spawn_calls && signals.empty() &&
               write_log.find("QBP phase=before_exit") == std::string::npos);

        if (fault == 0) {
            assert(write_log.find("QBP phase=clock_before") != std::string::npos &&
                   write_log.find("QBP phase=clock_after") == std::string::npos);
        }

        if (fault == 1) {
            assert(write_log.find("QBP phase=nanosleep_after") != std::string::npos &&
                   write_log.find("QBP phase=end_clock_after") == std::string::npos);
        }

        if (fault == 2) {
            assert(pause_calls == 1 && error_value == 4 &&
                   write_log.find("QBP phase=nanosleep_after") != std::string::npos);
        }
    }

    {
        reset();
        environment["VOYEMU_QBW_TRACE_DELAY"] = "1";
        set_environment();
        assert(run() == 0 && reaped && write_log.find("QBP ") == std::string::npos);
    }

    for (const std::string flag : {"", "0", "01", "1x"}) {
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "initial-delay10ms-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment["VOYEMU_QBW_TRACE_DELAY"] = flag;
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        assert(run() == 0 &&
               write_log ==
                       "QBI delay10ms-complete pid=37 parent=91 before_ns=0 after_ns=10000000\n");
    }

    for (const bool fail : {false, true}) {
        reset();
        error_value = 71;
        write_failure = fail;
        QbwNative native{};
        native.parent = 91;
        native.self = 37;
        assert(qbw_delay_phase(&native, "entry", 0, 0) == !fail && error_value == 71);
    }

    std::cout << "bounded wait carrier ABI/argv/environment/ownership mocks passed\n";

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
