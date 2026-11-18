#include "startup_extension.h"
#ifdef VOYEMU_OEM_CAPACITY
#define QSE_TEXT(x) #x
#define QSE_STRING(x) QSE_TEXT(x)
#define QSE_OEM_SIZE QSE_STRING(VOYEMU_OEM_CAPACITY)
/* The CI-built Free A9 front module reserves this section; preparation writes the OEM startup
 * baseline and the packed OEM sysmgrtool into it as data: the baseline, original and packed lengths
 * at 8, 12 and 16, the capacity at 20 and the bytes from 32. The symbol is defined only here in
 * assembly, so the compiler reads the bytes the file carries instead of folding the zero fill. */
__asm__(".pushsection .voyemu_oem,\"a\"\n"
        ".balign 16\n"
        ".globl qse_oem\n"
        ".hidden qse_oem\n"
        "qse_oem:\n"
        ".ascii \"VOYEMUOD\"\n"
        ".word 0, 0, 0, " QSE_OEM_SIZE ", 0, 0\n"
        ".zero " QSE_OEM_SIZE " - 32\n"
        ".popsection\n");
extern const unsigned char qse_oem[] __attribute__((visibility("hidden")));

static QseSize qse_oem_word(unsigned at) {
    return (QseSize)qse_oem[at] | (QseSize)qse_oem[at + 1] << 8 | (QseSize)qse_oem[at + 2] << 16 |
           (QseSize)qse_oem[at + 3] << 24;
}

/* A length that leaves the section reads as absent. */
static QseSize qse_oem_baseline_length(void) {
    QseSize n = qse_oem_word(8);

    return n <= VOYEMU_OEM_CAPACITY - 32 ? n : 0;
}

static QseSize qse_oem_packed_length(void) {
    QseSize n = qse_oem_word(16);

    return n <= VOYEMU_OEM_CAPACITY - 32 - qse_oem_baseline_length() ? n : 0;
}

#define QSE_BASELINE_BYTES (qse_oem + 32)
#define QSE_BASELINE_LENGTH qse_oem_baseline_length()
#define QST_ORIGINAL_LENGTH qse_oem_word(12)
#define QST_PACKED_LENGTH qse_oem_packed_length()
#define qst_original_pack (qse_oem + 32 + qse_oem_baseline_length())
#endif
#ifndef QSE_BASELINE_BYTES
/* Unconfigured builds refuse helper mode; normal carrier startup stays inert. */
static const unsigned char qse_unconfigured_baseline[] = {0};
#define QSE_BASELINE_BYTES qse_unconfigured_baseline
#define QSE_BASELINE_LENGTH 0
#endif
/* QNX AArch64 ABI: libc 0.2.189 unix/nto/mod.rs stat and fcntl definitions;
 * unix/nto/arch/aarch64.rs time_t=i64. Do not substitute host struct stat.
 * Reference: https://github.com/rust-lang/libc/tree/0.2.189/src/unix/nto */
typedef unsigned QseU32;
typedef unsigned long long QseU64;
typedef long long QseI64;
typedef struct {
    QseI64 seconds, nanoseconds;
} QseTime;
typedef struct {
    QseU64 ino;
    QseI64 size;
    QseU32 dev, rdev, uid, gid, old_mtime, old_atime, old_ctime, mode, nlink, blocksize;
    int nblocks;
    QseU32 blksize;
    QseU64 blocks;
    QseTime mtim, atim, ctim;
} QseStat;
_Static_assert(sizeof(QseStat) == 120 && __builtin_offsetof(QseStat, size) == 8 &&
                       __builtin_offsetof(QseStat, uid) == 24 &&
                       __builtin_offsetof(QseStat, mode) == 44 &&
                       __builtin_offsetof(QseStat, mtim) == 72,
               "QNX startup stat ABI");
#ifdef QSE_TEST
#define QSE_ALIAS(name)
#else
#define QSE_ALIAS(name) __asm__(#name)
#endif
extern int qse_open(const char *, int, ...) QSE_ALIAS(open);
extern int qse_close(int) QSE_ALIAS(close);
extern int qse_fstat(int, QseStat *) QSE_ALIAS(fstat);
extern int qse_lstat(const char *, QseStat *) QSE_ALIAS(lstat);
extern long qse_read(int, void *, QseSize) QSE_ALIAS(read);
extern long qse_write(int, const void *, QseSize) QSE_ALIAS(write);
extern int qse_fsync(int) QSE_ALIAS(fsync);
extern void *qse_malloc(QseSize) QSE_ALIAS(malloc);
extern void qse_free(void *) QSE_ALIAS(free);
extern int *qse_errno(void) QSE_ALIAS(__get_errno_ptr);
extern char *qse_getenv(const char *) QSE_ALIAS(getenv);
extern void qse_exit(int) QSE_ALIAS(_exit) __attribute__((noreturn));
extern void *qse_mmap(void *, QseSize, int, int, int, QseU64) QSE_ALIAS(mmap);
extern int qse_munmap(void *, QseSize) QSE_ALIAS(munmap);
extern int qse_snprintf(char *, QseSize, const char *, ...) QSE_ALIAS(snprintf);
#undef QSE_ALIAS

static int qse_identity(const QseStat *a, const QseStat *b) {
    return a->dev == b->dev && a->rdev == b->rdev && a->ino == b->ino && a->uid == b->uid &&
           a->gid == b->gid && a->mode == b->mode && a->nlink == b->nlink && a->size == b->size &&
           a->mtim.seconds == b->mtim.seconds && a->mtim.nanoseconds == b->mtim.nanoseconds &&
           a->ctim.seconds == b->ctim.seconds && a->ctim.nanoseconds == b->ctim.nanoseconds;
}

static int qse_regular(const QseStat *s, QseSize maximum) {
    return (s->mode & 0170000) == 0100000 && s->uid == 0 && !(s->mode & 0022) && s->size >= 0 &&
           (QseU64)s->size <= maximum;
}

static int qse_load(const char *path, unsigned char *bytes, QseSize maximum, QseSize *length) {
    QseStat before, opened, after;
    int fd = -1, rc = -1;
    QseSize used = 0;

    if (qse_lstat(path, &before) || !qse_regular(&before, maximum)) {
        return -1;
    }

    fd = qse_open(path, 010000 | 020000);

    if (fd < 0) {
        return -1;
    }

    if (qse_fstat(fd, &opened) || !qse_identity(&before, &opened)) {
        goto done;
    }

    for (;;) {
        long n = qse_read(fd, bytes + used, maximum + 1 - used);

        if (n < 0) {
            if (*qse_errno() == 4) {
                continue;
            }

            goto done;
        }

        if (!n) {
            break;
        }

        used += (QseSize)n;

        if (used > maximum) {
            goto done;
        }
    }

    if (used != (QseSize)before.size || qse_fstat(fd, &after) || !qse_identity(&before, &after) ||
        qse_lstat(path, &after) || !qse_identity(&before, &after)) {
        goto done;
    }

    *length = used;
    rc = 0;
done:

    if (qse_close(fd)) {
        rc = -1;
    }

    return rc;
}

#ifdef VOYEMU_SYSMGR_TOOL_ASSETS
#include "sysmgr_tool_assets.h"
#include "sysmgr_tool_install.c"
#endif
#if !defined(QSE_TEST) || defined(QSC_TEST)
#include "supervisor_config.c"
#endif

static void qse_start(void) {
    const char *mode = qse_getenv("VOYEMU_STARTUP_EXTENSION");

    if (!mode) {
        return;
    }

    if (mode[0] != '1' || mode[1]) {
        qse_exit(125);
    }

    const unsigned char *base = QSE_BASELINE_BYTES;
    const QseSize bn = QSE_BASELINE_LENGTH;
    unsigned char *installed = qse_malloc(131073);
    QseSize in = 0, offset = 0, length = 0, written = 0;
    int fd = -1, rc = 125;
    QseStat created, path;

    if (!base || !bn || bn > 65536 || !installed) {
        goto done;
    }

    if (qse_load("/scripts/startup_cluster.sh", installed, 131072, &in) ||
        qse_extract(base, bn, installed, in, &offset, &length)) {
        goto done;
    }

    fd = qse_open("/log/startup-extension.sh", 1 | 0400 | 02000 | 010000 | 020000, 0600);

    if (fd < 0) {
        goto done;
    }

    if (qse_fstat(fd, &created) || !qse_regular(&created, 65536) || (created.mode & 0777) != 0600 ||
        created.size != 0 || created.nlink != 1) {
        goto done;
    }

    while (written < length) {
        long n = qse_write(fd, installed + offset + written, length - written);

        if (n < 0 && *qse_errno() == 4) {
            continue;
        }

        if (n <= 0) {
            goto done;
        }

        written += (QseSize)n;
    }

    if (qse_fsync(fd) || qse_fstat(fd, &created) || !qse_regular(&created, 65536) ||
        (created.mode & 0777) != 0600 || (QseSize)created.size != length || created.nlink != 1 ||
        qse_lstat("/log/startup-extension.sh", &path) || !qse_identity(&created, &path)) {
        goto done;
    }

    rc = 0;
done:

    if (fd >= 0 && qse_close(fd)) {
        rc = 125;
    }

    if (installed) {
        qse_free(installed);
    }

    qse_exit(rc);
}

/* Factory authorization comes only from a build-only RAM marker, never a failed
 * mount. This predicate performs no writes and never formats an existing disk. */
static int qse_userdata_device(const char *path) {
    static const char prefix[] = "/dev/ud/hd";

    if (!path) {
        return 0;
    }

    for (QseSize i = 0; i < sizeof(prefix) - 1; ++i) {
        if (path[i] != prefix[i]) {
            return 0;
        }
    }

    QseSize i = sizeof(prefix) - 1, first = i;

    for (; path[i]; ++i) {
        if (i - first >= 10 || path[i] < '0' || path[i] > '9') {
            return 0;
        }
    }

    return i > first;
}

/* Exact devb-ram raw block metadata reports 512-byte sectors, not bytes.
 * 65536 sectors must additionally pass the independent 32 MiB read+EOF proof. */
static int qse_userdata_stat(const QseStat *s) {
    return (s->mode & 0170000) == 0060000 && s->uid == 0 && !(s->mode & 0022) && s->nlink == 1 &&
           s->size == 65536;
}

static void qse_userdata_diagnostic(const char *step, int error, QseSize offset, long got,
                                    QseU64 magic, const QseStat *stat) {
    char line[512];
    int n = qse_snprintf(
            line, sizeof(line),
            "QSTORE INIT_FAIL step=%s errno=%d offset=%lu got=%ld magic=0x%llx uid=%u gid=%u mode=%o nlink=%u size=%lld dev=%u rdev=%u ino=%llu\n",
            step, error, offset, got, magic, stat->uid, stat->gid, stat->mode, stat->nlink,
            stat->size, stat->dev, stat->rdev, stat->ino);

    if (n > 0 && (QseSize)n < sizeof(line)) {
        qse_write(2, line, (QseSize)n);
    }
}

static void qse_userdata_start(void) {
    const char *mode = qse_getenv("VOYEMU_USERDATA_INITIALIZE");

    if (!mode) {
        return;
    }

    const char *step = "environment";
    int error = 0, fd = -1, rc = 125;
    QseSize offset = 0;
    long got = 0;
    QseU64 magic = 0;
    QseStat before = {0}, opened = {0}, after = {0};
    const QseStat *observed = &before;
    unsigned char chunk[4096];

    if (mode[0] != '1' || mode[1]) {
        goto done;
    }

#ifdef VOYEMU_SYSMGR_TOOL_ASSETS
    if (qst_install()) {
        step = "sysmgrtool-install";
        goto done;
    }
#endif
    void *page = qse_mmap(0, 4096, 0x900, 0x10001, -1, 0x867ff000ULL);

    if (!page || page == (void *)-1) {
        step = "marker-map";
        error = *qse_errno();
        goto done;
    }

    const volatile unsigned char *marker = page;

    for (unsigned i = 0; i < 8; ++i) {
        magic |= (QseU64)marker[i] << (8 * i);
    }

    int marked = magic == 0x315241564d45564fULL, malformed = magic && !marked;

    if (marked) {
        for (unsigned i = 8; i < 4096; ++i) {
            if (marker[i]) {
                malformed = 1;
            }
        }
    }

    if (qse_munmap(page, 4096)) {
        step = "marker-unmap";
        error = *qse_errno();
        goto done;
    }

    if (malformed) {
        step = "marker-value";
        goto done;
    }

    if (!marked) {
        qse_exit(1);
    }

    const char *path = qse_getenv("VOYEMU_USERDATA_DEVICE");

    if (!qse_userdata_device(path)) {
        step = "device-path";
        goto done;
    }

    if (qse_lstat(path, &before)) {
        step = "device-lstat";
        error = *qse_errno();
        goto done;
    }

    if (!qse_userdata_stat(&before)) {
        step = "device-type";
        goto done;
    }

    fd = qse_open(path, 010000 | 020000);

    if (fd < 0) {
        step = "device-open";
        error = *qse_errno();
        goto done;
    }

    observed = &opened;

    if (qse_fstat(fd, &opened)) {
        step = "device-fstat";
        error = *qse_errno();
        goto done;
    }

    if (!qse_identity(&before, &opened)) {
        step = "device-open-identity";
        goto done;
    }

    for (offset = 0; offset < 33554432; offset += sizeof(chunk)) {
        do {
            got = qse_read(fd, chunk, sizeof(chunk));
        } while (got < 0 && *qse_errno() == 4);

        if (got < 0) {
            step = "device-read";
            error = *qse_errno();
            goto done;
        }

        if (got != (long)sizeof(chunk)) {
            step = "device-read-size";
            goto done;
        }

        for (QseSize i = 0; i < sizeof(chunk); ++i) {
            if (chunk[i]) {
                offset += i;
                step = "device-nonzero";
                goto done;
            }
        }
    }

    do {
        got = qse_read(fd, chunk, sizeof(chunk));
    } while (got < 0 && *qse_errno() == 4);

    if (got != 0) {
        step = "device-eof";

        if (got < 0) {
            error = *qse_errno();
        }

        goto done;
    }

    observed = &after;

    if (qse_fstat(fd, &after)) {
        step = "device-after-fstat";
        error = *qse_errno();
        goto done;
    }

    if (!qse_identity(&before, &after)) {
        step = "device-after-fstat-identity";
        goto done;
    }

    if (qse_lstat(path, &after)) {
        step = "device-after-lstat";
        error = *qse_errno();
        goto done;
    }

    if (!qse_identity(&before, &after)) {
        step = "device-after-lstat-identity";
        goto done;
    }

    rc = 0;
done:

    if (fd >= 0 && qse_close(fd)) {
        if (!rc) {
            step = "device-close";
            error = *qse_errno();
        }

        rc = 125;
    }

    if (rc) {
        qse_userdata_diagnostic(step, error, offset, got, magic, observed);
    }

    qse_exit(rc);
}

#ifdef VOYEMU_STARTUP_EXTENSION_STANDALONE
__attribute__((constructor)) static void qse_constructor(void) {
    qse_userdata_start();
    qse_supervisor_start();
    qse_start();
}
#endif
