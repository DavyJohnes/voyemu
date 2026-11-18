/* Source-owned Ethernet transport only; original io-pkt and qconn implement IP/TCP. */
#include "interguest_network.h"
#ifndef QNET_TEST
#define QNET_ALIAS(n) __asm__(#n)
#else
#define QNET_ALIAS(n)
#endif
extern void qnet_sync(void) QNET_ALIAS(sync);
extern int qnet_spawn(int *, const char *, const void *, const void *, char *const *, char *const *)
        QNET_ALIAS(posix_spawn);
extern int qnet_waitpid(int, int *, int) QNET_ALIAS(waitpid);
extern int qnet_kill(int, int) QNET_ALIAS(kill);
extern int qnet_clock_settime(int, const QnetTimespec *) QNET_ALIAS(clock_settime);
extern int qnet_clock_gettime(int, QnetTimespec *) QNET_ALIAS(clock_gettime);
extern int qnet_open(const char *, int, ...) QNET_ALIAS(open);
extern int qnet_close(int) QNET_ALIAS(close);
extern int qnet_poll(QnetPoll *, unsigned, int) QNET_ALIAS(poll);
extern long qnet_read(int, void *, QnetSize) QNET_ALIAS(read);
extern long qnet_write(int, const void *, QnetSize) QNET_ALIAS(write);
extern int *qnet_errno(void) QNET_ALIAS(__get_errno_ptr);
extern int qnet_sleep(unsigned) QNET_ALIAS(usleep);
extern int qnet_align(void **, QnetSize, QnetSize) QNET_ALIAS(posix_memalign);
extern int qnet_lock(const void *, QnetSize) QNET_ALIAS(mlock);
extern int qnet_unlock(const void *, QnetSize) QNET_ALIAS(munlock);
extern int qnet_offset(const void *, int, QnetSize, QnetI64 *, QnetSize *) QNET_ALIAS(mem_offset);
extern void qnet_free(void *) QNET_ALIAS(free);
extern char *qnet_env(const char *) QNET_ALIAS(getenv);
extern int qnet_format(char *, QnetSize, const char *, ...) QNET_ALIAS(snprintf);
extern int qnet_pid(void) QNET_ALIAS(getpid);
extern void qnet_exit(int) QNET_ALIAS(_exit) __attribute__((noreturn));
#undef QNET_ALIAS

/* One immutable UTC request per freshly zeroed relay page. Seconds in [2000,2100).
 * The verified QNX AArch64 timespec uses signed 64-bit seconds/nanoseconds.
 * Release ACK publishes status and readback, never merely a successful set call. */
static int qnet_utc(QnetPage *p) {
    QnetU32 seq = __atomic_load_n(&p->utc_request, __ATOMIC_ACQUIRE);

    if (!seq || seq == __atomic_load_n(&p->utc_ack, __ATOMIC_RELAXED)) {
        return 0;
    }

    if (p->utc_ack) {
        p->fault = 9;

        return -1;
    }

    QnetU64 epoch = p->utc_epoch;
    int status = 0;
    QnetTimespec desired, actual = {0, 0};

    if (seq != 1 || epoch < 946684800ULL || epoch >= 4102444800ULL ||
        epoch > 0x7fffffffffffffffULL) {
        status = -1;
    } else {
        desired.seconds = (QnetI64)epoch;
        desired.nanoseconds = 0;

        if (qnet_clock_settime(0, &desired)) {
            status = -2;
        } else if (qnet_clock_gettime(0, &actual)) {
            status = -3;
        } else if (actual.seconds < 946684800LL || actual.seconds >= 4102444800LL ||
                   actual.seconds < (QnetI64)epoch || actual.seconds > (QnetI64)epoch + 2 ||
                   actual.nanoseconds < 0 || actual.nanoseconds >= 1000000000LL) {
            status = -4;
        }
    }

    p->utc_actual = actual.seconds >= 946684800LL && actual.seconds < 4102444800LL
                            ? (QnetU32)actual.seconds
                            : 0;
    p->utc_status = status;
    __atomic_store_n(&p->utc_ack, seq, __ATOMIC_RELEASE);

    return 1;
}

/* Fixed OEM utility, clean environment: never inherit this preload constructor.
 * QNX waitpid WNOHANG is 0x0040. Host also enforces the overall flush deadline. */
static int qnet_readonly(const char *path) {
    char *argv[] = {(char *)"/bin/mount", (char *)"-u", (char *)"-r", (char *)path, 0};
    char *env[] = {(char *)"PATH=/bin:/usr/bin", 0};
    int child = 0, status = 0;

    if (qnet_spawn(&child, argv[0], 0, 0, argv, env) || child <= 0) {
        return -1;
    }

    for (unsigned i = 0; i < 1000; ++i) {
        int rc = qnet_waitpid(child, &status, 0x0040);

        if (rc == child) {
            return status == 0 ? 0 : -1;
        }

        if (rc < 0 && *qnet_errno() != 4) {
            return -2;
        }

        qnet_sleep(1000);
    }

    if (qnet_kill(child, 9)) {
        return -2;
    }

    for (unsigned i = 0; i < 100; ++i) {
        int rc = qnet_waitpid(child, &status, 0x0040);

        if (rc == child) {
            return -1;
        }

        if (rc < 0 && *qnet_errno() != 4) {
            return -2;
        }

        qnet_sleep(1000);
    }

    return -2;
}

static int qnet_flush(QnetPage *p) {
    QnetU32 seq = __atomic_load_n(&p->flush_request, __ATOMIC_ACQUIRE),
            ack = __atomic_load_n(&p->flush_ack, __ATOMIC_RELAXED);

    if (!seq || seq == ack) {
        return 0;
    }

    if (seq < ack) {
        p->fault = 10;

        return -1;
    }

    qnet_sync();
    int status = 0, rc = qnet_readonly("/var");

    if (rc == -2) {
        p->fault = 11;

        return -1;
    }

    if (rc) {
        status = -2;
    } else {
        rc = qnet_readonly("/");

        if (rc == -2) {
            p->fault = 11;

            return -1;
        }

        if (rc) {
            status = -3;
        }
    }

    if (!status) {
        qnet_sync();
    }

    p->flush_status = status;
    __atomic_store_n(&p->flush_ack, seq, __ATOMIC_RELEASE);

    return 1;
}

/* Opt-in Free A11 probe occupies reserved page padding; transport and ACK fields are unchanged. */
static unsigned qnet_diagnostics;
typedef struct {
    QnetU32 magic, phase, heartbeat, result, error, exit_code, pid;
} QnetDiagnostic;
_Static_assert(sizeof(QnetDiagnostic) <= sizeof(((QnetPage *)0)->padding) &&
                       __builtin_offsetof(QnetPage, padding) == 3148,
               "QNET diagnostic padding");

static void qnet_trace(QnetPage *p, unsigned phase, long result, int error) {
    if (!qnet_diagnostics) {
        return;
    }

    QnetDiagnostic *d = (QnetDiagnostic *)p->padding;
    d->magic = 0x31444e51U;
    d->result = (QnetU32)result;
    d->error = (QnetU32)error;

    if (phase == 1) {
        ++d->heartbeat;
    }

    __atomic_store_n(&d->phase, phase, __ATOMIC_RELEASE);
}

/* The Passion backing also contains OEM disk regions: a relay must stay in the
 * IPL identity-mapped prefix, independent of the larger QEMU memory extent. */
static int qnet_page_valid(QnetI64 physical, QnetSize contiguous) {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    const QnetI64 end = 0x180000000LL;
#else
    const QnetI64 end = 0x240000000LL;
#endif

    return contiguous >= 4096 && physical >= 0x40000000LL && physical <= end - 4096 &&
           !(physical & 4095);
}

static int qnet_frame(QnetU32 n) {
    return n >= 14 && n <= 1518;
}

/* Returns -1 fault, 0 idle, 1 progress, 2 requested stop. */
static int qnet_step(QnetPage *p, int fd, QnetU32 *last_rx) {
    qnet_trace(p, 1, 0, 0);

    if (p->magic != 0x54454e51U || p->version != 1U) {
        p->fault = 1;

        return -1;
    }

    if (__atomic_load_n(&p->stop, __ATOMIC_ACQUIRE)) {
        return 2;
    }

    int utc = qnet_utc(p);

    if (utc < 0) {
        return -1;
    }

    int flush = qnet_flush(p);

    if (flush < 0) {
        return -1;
    }

    QnetU32 rx = __atomic_load_n(&p->rxlen, __ATOMIC_ACQUIRE);
    int progress = utc || flush;

    if (rx) {
        QnetU32 seq = p->rxseq;

        if (!qnet_frame(rx) || !seq || seq == *last_rx) {
            p->fault = 2;

            return -1;
        }

        qnet_trace(p, 2, 0, 0);
        long n = qnet_write(fd, p->rx, rx);
        qnet_trace(p, 3, n, n < 0 ? *qnet_errno() : 0);

        if (n < 0 && *qnet_errno() == 4) {
            return 0;
        }

        if (n != (long)rx) {
            p->fault = 3;

            return -1;
        }

        *last_rx = seq;
        ++p->rxpackets;
        __atomic_store_n(&p->rxlen, 0, __ATOMIC_RELEASE);
        progress = 1;
    }

    QnetU32 tx = __atomic_load_n(&p->txlen, __ATOMIC_ACQUIRE);

    if (tx && !qnet_frame(tx)) {
        p->fault = 4;

        return -1;
    }

    if (!tx) {
        QnetPoll v = {fd, 1, 0};
        qnet_trace(p, 4, 0, 0);
        int rc = qnet_poll(&v, 1, 0);
        qnet_trace(p, 5, rc, rc < 0 ? *qnet_errno() : 0);

        if (rc < 0) {
            if (*qnet_errno() == 4) {
                return progress;
            }

            p->fault = 5;

            return -1;
        }

        if (rc > 1 || (v.revents & ~1)) {
            p->fault = 6;

            return -1;
        }

        if (rc == 1 && (v.revents & 1)) {
            qnet_trace(p, 6, 0, 0);
            long n = qnet_read(fd, p->tx, sizeof(p->tx));
            qnet_trace(p, 7, n, n < 0 ? *qnet_errno() : 0);

            if (n < 0 && *qnet_errno() == 4) {
                return progress;
            }

            if (n < 0 || !qnet_frame((QnetU32)n)) {
                p->fault = 7;

                return -1;
            }

            if (++p->txseq == 0) {
                ++p->txseq;
            }

            ++p->txpackets;
            __atomic_store_n(&p->txlen, (QnetU32)n, __ATOMIC_RELEASE);
            progress = 1;
        }
    }

    return progress;
}

#ifndef QNET_TEST
static void qnet_start(void) {
    const char *mode = qnet_env("VOYEMU_QNET_RELAY");

    if (!mode) {
        return;
    }

    if (mode[0] != '1' || mode[1]) {
        qnet_exit(125);
    }

    const char *diagnostic = qnet_env("VOYEMU_QNET_DIAGNOSTICS");
#ifdef QGFX_QINGGAN11_MCU
    qnet_diagnostics = diagnostic && diagnostic[0] == '1' && !diagnostic[1];
#else
    (void)diagnostic;
#endif
    void *memory = 0;
    QnetPage *p = 0;
    int fd = -1, locked = 0, rc = 1;
    QnetI64 physical = -1;
    QnetSize contiguous = 0;

    if (qnet_align(&memory, 4096, 4096) || !memory) {
        goto done;
    }

    for (QnetSize i = 0; i < 4096; ++i) {
        ((unsigned char *)memory)[i] = 0;
    }

    if (qnet_lock(memory, 4096)) {
        goto done;
    }

    locked = 1;

    if (qnet_offset(memory, -1, 4096, &physical, &contiguous) ||
        !qnet_page_valid(physical, contiguous)) {
        goto done;
    }

    fd = qnet_open("/dev/tap0", 2);

    if (fd < 0) {
        goto done;
    }

    p = memory;
    p->magic = 0x54454e51U;
    p->version = 1;
    __atomic_thread_fence(__ATOMIC_RELEASE);
    char text[160];
    int n = qnet_format(text, sizeof(text),
                        "QNET READY pid=%d paddr=0x%llx length=4096 version=1\n", qnet_pid(),
                        physical);

    if (n <= 0 || n >= (int)sizeof(text) || qnet_write(2, text, (QnetSize)n) != n) {
        goto done;
    }

    if (qnet_diagnostics) {
        ((QnetDiagnostic *)p->padding)->pid = (QnetU32)qnet_pid();
    }

    QnetU32 last_rx = 0;

    for (;;) {
        int step = qnet_step(p, fd, &last_rx);

        if (step == 2) {
            rc = 0;
            break;
        }

        if (step < 0) {
            break;
        }

        if (step == 0) {
            qnet_trace(p, 8, 0, 0);
            int slept = qnet_sleep(1000);
            qnet_trace(p, 9, slept, slept ? *qnet_errno() : 0);

            if (slept && *qnet_errno() != 4) {
                p->fault = 8;
                break;
            }
        }
    }

done:

    if (p && qnet_diagnostics) {
        ((QnetDiagnostic *)p->padding)->exit_code = (QnetU32)rc;
        qnet_trace(p, 10, rc, *qnet_errno());
    }

    if (fd >= 0 && qnet_close(fd)) {
        rc = 1;
    }

    if (locked && qnet_unlock(memory, 4096)) {
        rc = 1;
    }

    if (memory) {
        qnet_free(memory);
    }

    qnet_exit(rc);
}
#endif
