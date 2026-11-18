/* Exercise the actual initializers with the native helper entry and side-effect mocks. */
#define main qbw_entry_regressions
#include "qnx_bounded_wait_entry.test.cpp"
#undef main
#define QGFX_INITIALIZER
#define QGFX_HELPER_DISPATCH() qbw_start()
static int worker_starts, fifo_creates, module_opens;
static char enabled_flag[] = "1";

static char *initializer_getenv(const char *) {
    return enabled_flag;
}

static void *initializer_dlopen(const char *, int) {
    ++module_opens;

    return (void *)1;
}

static int initializer_mkfifo(const char *, unsigned) {
    ++fifo_creates;

    return 0;
}

static void *initializer_dlsym(void *, const char *) {
    return (void *)1;
}

static auto initializer_fifo_symbol(void *, const char *) -> int (*)(const char *, unsigned) {
    return initializer_mkfifo;
}

static int initializer_thread(int *, const void *, void *(*)(void *), void *) {
    ++worker_starts;

    return 0;
}

static int initializer_join(int, void *) {
    return 0;
}

static void initializer_emit(const char *) {}

static void initializer_drain() {}

static void initializer_lock() {}

static void initializer_sleep(unsigned) {}

using u64 = unsigned long long;

static u64 observation_call(unsigned, const u64 *, unsigned, const void *, unsigned) {
    return 1;
}

#define getpid qbw_getpid
#define getenv initializer_getenv
#define dlopen initializer_dlopen
#define dlsym initializer_dlsym
#define pthread_create initializer_thread
#define pthread_join initializer_join
#define emit initializer_emit
#define drain_mcd_trace initializer_drain
#define acquire initializer_lock
#define release initializer_lock
#define usleep initializer_sleep
#include "../../guest/qnx/manager_observe.c"
#undef dlsym
#define dlsym initializer_fifo_symbol
#include "../../guest/qnx/mcu_fifo_setup.c"

#include "../../guest/qnx/mcd_observer_worker.c"
#undef getpid
#undef getenv
#undef dlopen
#undef dlsym
#undef pthread_create
#undef pthread_join
#undef emit
#undef drain_mcd_trace
#undef acquire
#undef release
#undef usleep
namespace capture_fixture {
#define main capture_entry_regressions
#include "../../guest/qnx/screen_capture_entry.test.cpp"
#include <exception>
#include <iostream>
#undef main
} // namespace capture_fixture

static void initialize(void (*entry)(), bool helper) {
    try {
        entry();
        assert(!helper);
    } catch (const Exit &e) {
        assert(helper && e.code == 7 && reaped);
    }
}

int main() try {
    qbw_entry_regressions();
    const auto entries = {start_manager_observer, setup_mcu_fifo, start_trace_observer,
                          capture_fixture::source_capture_start};

    for (auto entry : entries) {
        reset();
        set_environment();
        status_value = 7 << 8;
        worker_starts = fifo_creates = module_opens = 0;
        capture_fixture::reset();
        initialize(entry, true);
        assert(!worker_starts && !fifo_creates && !module_opens && !capture_fixture::calls &&
               !capture_fixture::capture_calls);
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "driver-delay100ms-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        try {
            entry();
            assert(false);
        } catch (const Exit &e) {
            assert(e.code == 0 && !spawn_calls && pause_calls == 1 && fake_time == 100000000ULL);
        }

        assert(!worker_starts && !fifo_creates && !module_opens && !capture_fixture::calls &&
               !capture_fixture::capture_calls);
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "initial-delay10ms-v1";
        environment["VOYEMU_QBW_ARGC"] = "0";
        environment.erase("VOYEMU_QBW_ARG_00");
        set_environment();
        try {
            entry();
            assert(false);
        } catch (const Exit &e) {
            assert(e.code == 0 && !spawn_calls && pause_calls == 1 && fake_time == 10000000ULL);
        }

        assert(!worker_starts && !fifo_creates && !module_opens && !capture_fixture::calls &&
               !capture_fixture::capture_calls);
        reset();
        environment["VOYEMU_QBW_ENABLE"] = "invalid";
        set_environment();
        try {
            entry();
            assert(false);
        } catch (const Exit &e) {
            assert(e.code == 125 && !spawn_calls);
        }

        assert(!worker_starts && !fifo_creates && !module_opens && !capture_fixture::calls);
    }

    reset();
    environment.erase("VOYEMU_QBW_ENABLE");
    set_environment();
    worker_starts = fifo_creates = module_opens = 0;
    start_manager_observer();
    setup_mcu_fifo();
    start_trace_observer();
    assert(worker_starts == 2 && fifo_creates == 1 && module_opens == 1);
    stop_manager_observer();
    stop_trace_observer();
    capture_fixture::capture_entry_regressions();
    std::cout
            << "Actual initializer dispatch preserves normal workers/capture; helper reaps child before immediate exit\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
