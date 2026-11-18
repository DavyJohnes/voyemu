#ifndef QSM_SHARED_MEMORY_H
#define QSM_SHARED_MEMORY_H
/* QNX native memory object: SHMCTL_ANON without LAZY is implicitly locked.
 * No ability changes, physical overlays, fabricated offsets, or mlock hooks. */
typedef unsigned long QsmSize;
typedef unsigned long long QsmU64;
#ifdef QSM_TEST
#define QSM_ABI(name)
#else
#define QSM_ABI(name) __asm__(name)
#endif
extern int qsm_open(const char *, int, unsigned) QSM_ABI("shm_open");
extern int qsm_ctl(int, int, QsmU64, QsmU64) QSM_ABI("shm_ctl");
extern void *qsm_map(void *, QsmSize, int, int, int, long long) QSM_ABI("mmap");
extern int qsm_close(int) QSM_ABI("close");
extern int qsm_unmap(void *, QsmSize) QSM_ABI("munmap");
#ifdef QGFX_DIAGNOSTIC_TRANSPORT
/* Same native QNX AArch64 stat ABI as startup_extension.c QseStat. */
typedef long long QsmDiagI64;
typedef struct {
    QsmDiagI64 seconds, nanoseconds;
} QsmDiagTime;
typedef struct {
    QsmU64 ino;
    QsmDiagI64 size;
    unsigned dev, rdev, uid, gid, old_mtime, old_atime, old_ctime, mode, nlink, blocksize;
    int nblocks;
    unsigned blksize;
    QsmU64 blocks;
    QsmDiagTime mtim, atim, ctim;
} QsmDiagStat;
_Static_assert(sizeof(QsmDiagStat) == 120 && __builtin_offsetof(QsmDiagStat, size) == 8 &&
                       __builtin_offsetof(QsmDiagStat, mtim) == 72,
               "QNX diagnostic stat ABI");
extern int qsm_diag_open(const char *, int, ...) QSM_ABI("open");
extern int qsm_diag_fstat(int, QsmDiagStat *) QSM_ABI("fstat");
extern int qsm_diag_close(int) QSM_ABI("close");
typedef struct {
    QsmU64 cur, max;
} QsmDiagLimit;
_Static_assert(sizeof(QsmDiagLimit) == 16, "QNX rlimit ABI");
extern int qsm_diag_getrlimit(int, QsmDiagLimit *) QSM_ABI("getrlimit");
extern long qsm_diag_sysconf(int) QSM_ABI("sysconf");
static unsigned qsm_diag_failures, qsm_diag_snapshot_seen;

/* Raw numeric resources avoid importing unsupported resource names or units.
 * Probe failures retain sentinel values and their own errno, never inferred data. */
static void qsm_diag_snapshot(void) {
    int *ep = __get_errno_ptr(), saved = *ep;

    if (!__atomic_exchange_n(&qsm_diag_snapshot_seen, 1, __ATOMIC_RELAXED)) {
        char text[224];
        QsmDiagStat stat = {0};
        stat.size = -1;
        *ep = 0;
        int proc_fd = qsm_diag_open("/proc", 0), open_error = *ep;
        int stat_rc = -2, stat_error = 0, close_rc = -2, close_error = 0;

        if (proc_fd >= 0) {
            *ep = 0;
            stat_rc = qsm_diag_fstat(proc_fd, &stat);
            stat_error = *ep;
            *ep = 0;
            close_rc = qsm_diag_close(proc_fd);
            close_error = *ep;
        }

        int proc_n = snprintf(
                text, sizeof(text),
                "QG3 DIAG kind=qsm-proc-memory open-rc=%d open-errno=%d stat-rc=%d stat-errno=%d size=%lld close-rc=%d close-errno=%d\n",
                proc_fd, open_error, stat_rc, stat_error, stat.size, close_rc, close_error);

        if (proc_n > 0) {
            write(2, text, (QsmSize)proc_n < sizeof(text) ? (QsmSize)proc_n : sizeof(text) - 1);
        }

        const int names[] = {11, 67, 93};

        for (unsigned i = 0; i < 3; ++i) {
            *ep = 0;
            long value = qsm_diag_sysconf(names[i]);
            int error = *ep;
            int n = snprintf(text, sizeof(text),
                             "QG3 DIAG kind=qsm-sysconf name=%d value=%ld errno=%d\n", names[i],
                             value, error);

            if (n > 0) {
                write(2, text, (QsmSize)n < sizeof(text) ? (QsmSize)n : sizeof(text) - 1);
            }
        }

        for (int resource = 0; resource < 16; ++resource) {
            QsmDiagLimit limit = {~0ULL, ~0ULL};
            *ep = 0;
            int rc = qsm_diag_getrlimit(resource, &limit), error = *ep;
            int n = snprintf(
                    text, sizeof(text),
                    "QG3 DIAG kind=qsm-rlimit resource=%d rc=%d errno=%d cur=0x%llx max=0x%llx\n",
                    resource, rc, error, limit.cur, limit.max);

            if (n > 0) {
                write(2, text, (QsmSize)n < sizeof(text) ? (QsmSize)n : sizeof(text) - 1);
            }
        }
    }

    *ep = saved;
}

static QsmU64 qsm_diag_successes, qsm_diag_live, qsm_diag_peak;

static void qsm_diag_failure(const char *stage, int native_errno, QsmSize bytes, QsmSize extent,
                             int fd) {
    int *ep = __get_errno_ptr(), saved = *ep;
    unsigned ordinal = __atomic_load_n(&qsm_diag_failures, __ATOMIC_RELAXED);

    while (ordinal < 16) {
        if (__atomic_compare_exchange_n(&qsm_diag_failures, &ordinal, ordinal + 1, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            char text[384];
            int n = snprintf(
                    text, sizeof(text),
                    "QG3 DIAG kind=qsm-failure ordinal=%u stage=%s native-errno=%d bytes=%lu extent=%lu fd=%d successes=%llu live-extent=%llu peak-extent=%llu\n",
                    ordinal + 1, stage, native_errno, bytes, extent, fd,
                    __atomic_load_n(&qsm_diag_successes, __ATOMIC_RELAXED),
                    __atomic_load_n(&qsm_diag_live, __ATOMIC_RELAXED),
                    __atomic_load_n(&qsm_diag_peak, __ATOMIC_RELAXED));

            if (n > 0) {
                write(2, text, (QsmSize)n < sizeof(text) ? (QsmSize)n : sizeof(text) - 1);
            }

            break;
        }
    }

    *ep = saved;
}

static void qsm_diag_allocated(QsmSize extent) {
    __atomic_fetch_add(&qsm_diag_successes, 1, __ATOMIC_RELAXED);
    QsmU64 live = __atomic_add_fetch(&qsm_diag_live, extent, __ATOMIC_RELAXED),
           peak = __atomic_load_n(&qsm_diag_peak, __ATOMIC_RELAXED);

    while (live > peak && !__atomic_compare_exchange_n(&qsm_diag_peak, &peak, live, 0,
                                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

#define QSM_DIAG_SNAPSHOT() qsm_diag_snapshot()
#define QSM_DIAG_ERR(name) int name = *__get_errno_ptr();
#define QSM_DIAG_FAIL(stage, error, bytes, extent, fd)                                             \
    qsm_diag_failure(stage, error, bytes, extent, fd)
#define QSM_DIAG_ALLOCATED(extent) qsm_diag_allocated(extent)
#define QSM_DIAG_RELEASED(extent) __atomic_fetch_sub(&qsm_diag_live, extent, __ATOMIC_RELAXED)
#else
#define QSM_DIAG_SNAPSHOT() ((void)0)
#define QSM_DIAG_ERR(name)
#define QSM_DIAG_FAIL(stage, error, bytes, extent, fd) ((void)0)
#define QSM_DIAG_ALLOCATED(extent) ((void)0)
#define QSM_DIAG_RELEASED(extent) ((void)0)
#endif

static QsmSize qsm_extent(QsmSize bytes) {
    if (!bytes || bytes > (64UL << 20)) {
        return 0;
    }

    return (bytes + 4095UL) & ~4095UL;
}

static void *qsm_allocate(QsmSize bytes) {
    QsmSize extent = qsm_extent(bytes);

    if (!extent) {
        QSM_DIAG_FAIL("extent", 0, bytes, 0, -1);

        return 0;
    }

    int fd = qsm_open((const char *)-1L, 2, 0600);

    if (fd < 0) {
        QSM_DIAG_ERR(error) QSM_DIAG_FAIL("shm_open", error, bytes, extent, fd);

        return 0;
    }

    if (qsm_ctl(fd, 1, 0, extent)) {
        QSM_DIAG_ERR(error) qsm_close(fd);
        QSM_DIAG_FAIL("shm_ctl", error, bytes, extent, fd);
        QSM_DIAG_SNAPSHOT();

        return 0;
    }

    void *p = qsm_map(0, extent, 0x300, 1, fd, 0);
    QSM_DIAG_ERR(map_errno)
    int closed = qsm_close(fd);
    QSM_DIAG_ERR(close_errno)

    if (p == (void *)-1L) {
        QSM_DIAG_FAIL("mmap", map_errno, bytes, extent, fd);

        return 0;
    }

    if (closed) {
        qsm_unmap(p, extent);
        QSM_DIAG_FAIL("close", close_errno, bytes, extent, fd);

        return 0;
    }

    QSM_DIAG_ALLOCATED(extent);

    return p;
}

static int qsm_release(void *p, QsmSize bytes) {
    QsmSize extent = qsm_extent(bytes);

    if (!p || p == (void *)-1L || !extent) {
        return -1;
    }

    int rc = qsm_unmap(p, extent);

    if (!rc) {
        QSM_DIAG_RELEASED(extent);
    }

    return rc;
}

#endif
