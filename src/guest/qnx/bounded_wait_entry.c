/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#include "bounded_wait.h"
#include "bounded_wait_arguments.h"
/* Numeric ABI: QNX7 AArch64; independently corroborated by pinned OEM ksh callers.
 * https://github.com/rust-lang/libc/blob/master/src/unix/nto/mod.rs
 * https://github.com/rust-lang/libc/blob/master/src/unix/nto/arch/aarch64.rs
 * No standalone CRT: libc is initialized by the dedicated OEM preload carrier. */
#ifndef QBW_ENTRY_TEST
#define QBW_API(name) __asm__(#name)
#else
#define QBW_API(name)
#endif
typedef struct {
    long long seconds, nanoseconds;
} QbwTimespec;
#ifdef __cplusplus
static_assert(sizeof(int) == 4 && sizeof(long long) == 8 && sizeof(void *) == 8 &&
                      sizeof(QbwTimespec) == 16,
              "QNX AArch64 ABI");
#else
_Static_assert(sizeof(int) == 4 && sizeof(long long) == 8 && sizeof(void *) == 8 &&
                       sizeof(QbwTimespec) == 16,
               "QNX AArch64 ABI");
#endif
extern char *qbw_getenv(const char *) QBW_API(getenv);
extern char **qbw_environ QBW_API(environ);
extern int qbw_getpid(void) QBW_API(getpid);
extern int qbw_getppid(void) QBW_API(getppid);
extern int *qbw_errno(void) QBW_API(__get_errno_ptr);
extern int qbw_clock(int, QbwTimespec *) QBW_API(clock_gettime);
extern int qbw_nanosleep(const QbwTimespec *, QbwTimespec *) QBW_API(nanosleep);
extern int qbw_spawn(int *, const char *, const void *, const void *, char *const *, char *const *)
        QBW_API(posix_spawn);
extern int qbw_waitpid(int, int *, int) QBW_API(waitpid);
extern int qbw_kill(int, int) QBW_API(kill);
extern long qbw_write(int, const void *, unsigned long) QBW_API(write);
/* Dedicated helper owns no buffered streams/atexit work; cleanup happens above. */
extern void qbw_exit(int) QBW_API(_exit) __attribute__((noreturn));
#undef QBW_API
static volatile unsigned qbw_file_backed_data __attribute__((used)) = 1;
typedef struct {
    int parent, self, child, trace_error;
    QbwArguments args;
    char *environment[QBW_MAX_ENV + 1];
} QbwNative;

static const char *qbw_environment_value(void *unused, const char *key) {
    (void)unused;

    return qbw_getenv(key);
}

static int qbw_log(const char *bytes, unsigned size) {
    unsigned at = 0;

    for (unsigned i = 0; i < 16 && at < size; ++i) {
        long n = qbw_write(2, bytes + at, size - at);

        if (n < 0 && *qbw_errno() == 4) {
            continue;
        }

        if (n <= 0 || (unsigned long)n > size - at) {
            return 0;
        }

        at += (unsigned)n;
    }

    return at == size;
}

static unsigned qbw_text(char *out, unsigned at, const char *s) {
    for (unsigned i = 0; s[i]; ++i) {
        out[at++] = s[i];
    }

    return at;
}

static unsigned qbw_number(char *out, unsigned at, unsigned value) {
    char digits[10];
    unsigned n = 0;

    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    while (n) {
        out[at++] = digits[--n];
    }

    return at;
}

static void qbw_child_notice(QbwNative *n, int pid) {
    char bytes[96];
    unsigned at = qbw_text(bytes, 0, "QBW child=");

    at = qbw_number(bytes, at, (unsigned)pid);
    at = qbw_text(bytes, at, " owner=");
    at = qbw_number(bytes, at, (unsigned)n->self);
    at = qbw_text(bytes, at, " parent=");
    at = qbw_number(bytes, at, (unsigned)n->parent);
    bytes[at++] = '\n';

    if (!qbw_log(bytes, at)) {
        n->trace_error = 1;
    }
}

static void qbw_result_notice(QbwNative *n, const QbwResult *r) {
    char bytes[96];
    unsigned at = qbw_text(bytes, 0, "QBW reaped=");

    at = qbw_number(bytes, at, (unsigned)r->child_reaped);
    at = qbw_text(bytes, at, " cleanup_unverified=");
    at = qbw_number(bytes, at, (unsigned)r->cleanup_unverified);
    at = qbw_text(bytes, at, " polls=");
    at = qbw_number(bytes, at, r->polls);
    bytes[at++] = '\n';

    if (!qbw_log(bytes, at)) {
        n->trace_error = 1;
    }
}

static int qbw_native_owner(void *opaque) {
    QbwNative *n = (QbwNative *)opaque;

    return qbw_getppid() == n->parent && qbw_getpid() == n->self;
}

static int qbw_native_now(void *unused, QbwTime *out) {
    (void)unused;
    QbwTimespec t;

    if (qbw_clock(2, &t) || t.seconds < 0 || t.nanoseconds < 0 || t.nanoseconds >= 1000000000L) {
        return 0;
    }

    if ((QbwTime)t.seconds > (~(QbwTime)0 - (QbwTime)t.nanoseconds) / 1000000000ULL) {
        return 0;
    }

    *out = (QbwTime)t.seconds * 1000000000ULL + (QbwTime)t.nanoseconds;

    return 1;
}

static int qbw_native_spawn(void *opaque, int *pid) {
    QbwNative *n = (QbwNative *)opaque;
    int result = qbw_spawn(pid, n->args.values[0], 0, 0, n->args.values, n->environment);

    if (!result && *pid > 0) {
        n->child = *pid;
    }

    return result;
}

static int qbw_native_poll(void *unused, int pid, int *code) {
    (void)unused;
    int status = 0;
    int result = qbw_waitpid(pid, &status, 0x0040);

    if (result == 0) {
        return 0;
    }

    if (result < 0) {
        int error = *qbw_errno();

        return error == 4 ? 2 : error == 10 ? -1 : -2;
    }

    if (result != pid) {
        return -2;
    }

    unsigned value = (unsigned)status, signal = value & 0x7fU;

    if (!signal) {
        *code = (int)((value >> 8) & 255U);

        return 1;
    }

    if (signal == 127U) {
        return -2; /* stopped/continued is not reaped success. */
    }

    *code = 128 + (int)signal;

    return 1;
}

static int qbw_native_pause(void *unused, QbwTime ns) {
    (void)unused;
    QbwTimespec t = {0, (long)ns};

    return qbw_nanosleep(&t, 0) == 0 ? 0 : *qbw_errno() == 4 ? 1 : -1;
}

static int qbw_native_signal(void *unused, int pid, int which) {
    (void)unused;

    if (pid <= 0) {
        return -1;
    }

    for (unsigned i = 0; i < 16; ++i) {
        if (qbw_kill(pid, which == 1 ? 15 : 9) == 0) {
            return 0;
        }

        int error = *qbw_errno();

        if (error == 3) {
            return 0;
        }

        if (error != 4) {
            return -1;
        }
    }

    return -1;
}

static int qbw_native_reap(void *unused, int pid) {
    (void)unused;
    int status = 0;

    for (unsigned i = 0; i < 16; ++i) {
        int result = qbw_waitpid(pid, &status, 0);

        if (result == pid) {
            return 0;
        }

        if (result < 0 && *qbw_errno() == 4) {
            continue;
        }

        return -1;
    }

    return -1;
}

/* Opt-in diagnostic only. Each delta also includes the preceding bounded trace write. */
static unsigned qbw_time_number(char *out, unsigned at, QbwTime value) {
    char digits[20];
    unsigned n = 0;

    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);

    while (n) {
        out[at++] = digits[--n];
    }

    return at;
}

static int qbw_clock_notice(const QbwNative *n, unsigned step, QbwTime now) {
    char bytes[128];
    unsigned at = qbw_text(bytes, 0, "QBC clock");

    at = qbw_number(bytes, at, step);
    at = qbw_text(bytes, at, " pid=");
    at = qbw_number(bytes, at, (unsigned)n->self);
    at = qbw_text(bytes, at, " parent=");
    at = qbw_number(bytes, at, (unsigned)n->parent);
    at = qbw_text(bytes, at, " ns=");
    at = qbw_time_number(bytes, at, now);
    bytes[at++] = '\n';

    return qbw_log(bytes, at);
}

static void qbw_clock_failure(const char *message) {
    qbw_log(message, qbw_length(message, 64));
    qbw_exit(QBW_INTERNAL);
}

static void qbw_clock_control(QbwNative *native) {
    QbwTime previous = 0;

    for (unsigned step = 0; step < 3; ++step) {
        QbwTime now = 0;

        if (!qbw_native_owner(native) || !qbw_native_now(native, &now) ||
            (step && now < previous)) {
            qbw_clock_failure("QBC failure=clock-or-owner\n");
        }

        if (!qbw_clock_notice(native, step, now)) {
            qbw_exit(QBW_INTERNAL);
        }

        if (step == 2) {
            if (now - previous < 10000000ULL) {
                qbw_clock_failure("QBC failure=short-interval\n");
            }

            if (!qbw_native_owner(native) || !qbw_log("QBC complete\n", 13)) {
                qbw_exit(QBW_INTERNAL);
            }

            qbw_exit(0);
        }

        previous = now;

        if (qbw_native_pause(native, step ? 10000000ULL : 0) != 0) {
            qbw_clock_failure("QBC failure=nanosleep\n");
        }
    }

    qbw_exit(QBW_INTERNAL);
}

/* Opt-in comparator phases use the existing guest clock samples; host wall time is separate. */
static int qbw_delay_phase(const QbwNative *native, const char *phase, QbwTime ns, int sampled) {
    char bytes[160];
    unsigned at = qbw_text(bytes, 0, "QBP phase=");

    at = qbw_text(bytes, at, phase);
    at = qbw_text(bytes, at, " pid=");
    at = qbw_number(bytes, at, (unsigned)native->self);
    at = qbw_text(bytes, at, " parent=");
    at = qbw_number(bytes, at, (unsigned)native->parent);

    if (sampled) {
        at = qbw_text(bytes, at, " ns=");
        at = qbw_time_number(bytes, at, ns);
    }

    bytes[at++] = '\n';
    const int saved = *qbw_errno();
    const int ok = qbw_log(bytes, at);
    *qbw_errno() = saved;

    return ok;
}

/* Private opt-in API ablation: fixed driver-readiness delay, no child/argv. */
static void qbw_fixed_delay(QbwNative *native, QbwTime interval, const char *notice, int trace) {
    QbwTime before = 0, after = 0;

    if (!qbw_native_owner(native)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (trace && !qbw_delay_phase(native, "clock_before", 0, 0)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (!qbw_native_now(native, &before)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (trace && (!qbw_delay_phase(native, "clock_after", before, 1) ||
                  !qbw_delay_phase(native, "nanosleep_before", before, 1))) {
        qbw_exit(QBW_INTERNAL);
    }

    const int paused = qbw_native_pause(native, interval);

    if (trace && !qbw_delay_phase(native, "nanosleep_after", 0, 0)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (paused || !qbw_native_owner(native)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (trace && !qbw_delay_phase(native, "end_clock_before", 0, 0)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (!qbw_native_now(native, &after) || after < before || after - before < interval) {
        qbw_exit(QBW_INTERNAL);
    }

    if (trace && !qbw_delay_phase(native, "end_clock_after", after, 1)) {
        qbw_exit(QBW_INTERNAL);
    }

    char bytes[160];
    unsigned at = qbw_text(bytes, 0, notice);
    at = qbw_number(bytes, at, (unsigned)native->self);
    at = qbw_text(bytes, at, " parent=");
    at = qbw_number(bytes, at, (unsigned)native->parent);
    at = qbw_text(bytes, at, " before_ns=");
    at = qbw_time_number(bytes, at, before);
    at = qbw_text(bytes, at, " after_ns=");
    at = qbw_time_number(bytes, at, after);
    bytes[at++] = '\n';

    if (!qbw_log(bytes, at) || !qbw_native_owner(native)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (trace && (!qbw_delay_phase(native, "before_exit", after, 1) || !qbw_native_owner(native))) {
        qbw_exit(QBW_INTERNAL);
    }

    qbw_exit(0);
}

#ifndef QBW_ENTRY_TEST
__attribute__((constructor))
#endif

static void qbw_start(void) {
    const char *enabled = qbw_getenv("VOYEMU_QBW_ENABLE");

    if (!enabled) {
        return;
    }

    const int clock_mode = qbw_equal(enabled, "clock-zero-fractional-v1"),
              delay_mode = qbw_equal(enabled, "driver-delay100ms-v1"),
              initial_mode = qbw_equal(enabled, "initial-delay10ms-v1");

    if (!clock_mode && !delay_mode && !initial_mode && !qbw_equal(enabled, "1")) {
        qbw_exit(QBW_INTERNAL);
    }

    QbwNative native;
    native.parent = qbw_getppid();
    native.self = qbw_getpid();
    native.child = 0;
    native.trace_error = 0;
    const char *trace_flag = (delay_mode || initial_mode) ? qbw_getenv("VOYEMU_QBW_TRACE_DELAY")
                                                          : 0;
    const int delay_trace = trace_flag && qbw_equal(trace_flag, "1");

    if (delay_trace && !qbw_delay_phase(&native, "entry", 0, 0)) {
        qbw_exit(QBW_INTERNAL);
    }

    if (clock_mode || delay_mode || initial_mode) {
        const char *count = qbw_getenv("VOYEMU_QBW_ARGC");

        if (native.parent <= 1 || native.self <= 1 || !count || !qbw_equal(count, "0") ||
            !qbw_environment(qbw_environ, native.environment)) {
            qbw_exit(QBW_INTERNAL);
        }

        for (unsigned i = 0; qbw_environ[i]; ++i) {
            const char prefix[] = "VOYEMU_QBW_ARG_";
            unsigned k = 0;

            while (k < sizeof(prefix) - 1 && qbw_environ[i][k] == prefix[k]) {
                ++k;
            }

            if (k == sizeof(prefix) - 1) {
                qbw_exit(QBW_INTERNAL);
            }
        }

        if (initial_mode) {
            qbw_fixed_delay(&native, 10000000ULL, "QBI delay10ms-complete pid=", delay_trace);
        }

        if (delay_mode) {
            qbw_fixed_delay(&native, 100000000ULL, "QBD delay100ms-complete pid=", delay_trace);
        }

        qbw_clock_control(&native);
    }

    if (native.parent <= 1 || native.self <= 1 ||
        !qbw_arguments(qbw_environment_value, 0, &native.args) ||
        !qbw_environment(qbw_environ, native.environment)) {
        qbw_exit(QBW_INTERNAL);
    }

    QbwOps ops = {&native,         qbw_native_now,   qbw_native_owner,  qbw_native_spawn,
                  qbw_native_poll, qbw_native_pause, qbw_native_signal, qbw_native_reap};
    QbwResult result = qbw_run(&ops);

    if (native.child > 0) {
        qbw_child_notice(&native, native.child);
    }

    qbw_result_notice(&native, &result);

    if (native.trace_error) {
        result.internal_error = 1;
        result.code = QBW_INTERNAL;
    }

    const char *message = result.cleanup_unverified         ? "QBW cleanup-unverified\n"
                          : result.internal_error           ? "QBW internal\n"
                          : result.timed_out                ? "QBW timeout\n"
                          : result.code == QBW_SPAWN_FAILED ? "QBW spawn-failed\n"
                                                            : "QBW exit\n";

    if (!qbw_log(message, qbw_length(message, 32))) {
        result.code = QBW_INTERNAL;
    }

    qbw_exit(result.code);
}

/* NOLINTEND(modernize-use-nullptr) */
