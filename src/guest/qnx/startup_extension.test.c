#include "startup_extension.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "check failed line %d\n", __LINE__);                                   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int test_bytes(void) {
    const unsigned char base[] = "#!/bin/sh\necho synthetic\nclusterboot &\necho done\n";
    const QseSize bn = sizeof(base) - 1;
    unsigned char changed[140000];
    QseSize offset = 0, length = 0;

    CHECK(!qse_extract(base, bn, base, bn, &offset, &length) && !length);
    const QseSize at = (QseSize)(strstr((const char *)base, "clusterboot &") - (const char *)base);
    const char hook[] = ". /var/example/startup.sh\n";
    const QseSize hn = sizeof(hook) - 1;
    memcpy(changed, base, at);
    memcpy(changed + at, hook, hn);
    memcpy(changed + at + hn, base + at, bn - at);
    CHECK(!qse_extract(base, bn, changed, bn + hn, &offset, &length) && offset == at &&
          length == hn);
    changed[2] ^= 1;
    CHECK(qse_extract(base, bn, changed, bn + hn, &offset, &length));
    changed[2] ^= 1;
    changed[bn + hn - 2] ^= 1;
    CHECK(qse_extract(base, bn, changed, bn + hn, &offset, &length));
    changed[bn + hn - 2] ^= 1;
    changed[at] = 0;
    CHECK(qse_extract(base, bn, changed, bn + hn, &offset, &length));
    const unsigned char duplicate[] = "clusterboot &\nclusterboot &\n";
    CHECK(qse_extract(duplicate, sizeof(duplicate) - 1, duplicate, sizeof(duplicate) - 1, &offset,
                      &length));
    const unsigned char partial[] = "echo clusterboot &\n";
    CHECK(qse_extract(partial, sizeof(partial) - 1, partial, sizeof(partial) - 1, &offset,
                      &length));
    CHECK(qse_extract(base, bn, base, bn - 1, &offset, &length));
    memcpy(changed, base, at);
    memset(changed + at, 'x', 65535);
    changed[at + 65535] = '\n';
    memcpy(changed + at + 65536, base + at, bn - at);
    CHECK(!qse_extract(base, bn, changed, bn + 65536, &offset, &length) && length == 65536);
    CHECK(qse_extract(base, bn, changed, bn + 65537, &offset, &length));
    CHECK(qse_extract(changed, 65537, changed, 65537, &offset, &length));
    CHECK(qse_extract(base, bn, changed, 131073, &offset, &length));
    CHECK(qse_extract(base, 0, base, 0, &offset, &length));
    CHECK(qse_extract(base, bn, base, bn, 0, &length));
    puts("QNX guest startup byte guards passed");

    return 0;
}

/* Real operational helper, private POSIX mocks: no copied implementation. */
#include <setjmp.h>
#include <stdarg.h>
#define QSE_TEST
static const unsigned char mock_base[] = "#!/bin/sh\necho synthetic\nclusterboot &\necho done\n";
#define QSE_BASELINE_BYTES mock_base
#define QSE_BASELINE_LENGTH (sizeof(mock_base) - 1)
#include "startup_extension.c"
static unsigned char mock_installed[1024], mock_output[65536];
static QseSize mock_size, mock_pos[5], mock_output_size;
static int mock_scenario, mock_created, mock_read_interrupt, mock_write_interrupt, mock_exit_code,
        mock_closes;
static int mock_error;
static const char *mock_mode;
static const char *mock_userdata_mode, *mock_device;
static unsigned char mock_marker[4096];
static char mock_diagnostic[1024];
static int mock_device_reads, mock_device_opens;
static jmp_buf mock_jump;
enum {
    NORMAL,
    SYMLINK_INPUT,
    GROUP_WRITABLE,
    FOREIGN_OWNER,
    OPEN_REPLACED,
    READ_REPLACED,
    SHORT_INTERRUPTED,
    SNAPSHOT_EXISTS,
    SNAPSHOT_OPEN_FAIL,
    SNAPSHOT_WRITE_FAIL,
    SNAPSHOT_SYNC_FAIL,
    FACTORY_MAP_FAIL,
    FACTORY_NONZERO_END,
    FACTORY_WRONG_SIZE,
    FACTORY_EOF_EARLY,
    FACTORY_EXTRA,
    FACTORY_SYMLINK,
    FACTORY_REPLACED,
    FACTORY_SHORT_READ,
    FACTORY_FOREIGN_UID,
    FACTORY_GROUP_WRITE,
    FACTORY_HARDLINK,
    FACTORY_RDEV_REPLACED,
    FACTORY_SIZED,
    FACTORY_SIZE_ZERO,
    FACTORY_SIZE_BYTES,
    FACTORY_SIZE_EXTRA
};

static QseStat mock_stat(int fd) {
    QseStat s = {0};

    s.ino = (QseU64)fd;
    s.dev = 1;
    s.uid = 0;
    s.gid = 0;
    s.mode = 0100644;
    s.nlink = 1;
    s.size = fd == 1   ? (QseI64)(sizeof(mock_base) - 1)
             : fd == 2 ? (QseI64)mock_size
                       : (QseI64)mock_output_size;

    if (fd == 2) {
        if (mock_scenario == SYMLINK_INPUT) {
            s.mode = 0120777;
        }

        if (mock_scenario == GROUP_WRITABLE) {
            s.mode = 0100664;
        }

        if (mock_scenario == FOREIGN_OWNER) {
            s.uid = 99;
        }
    }

    if (fd == 3) {
        s.mode = 0100600;
    }

    if (fd == 4) {
        s.mode = 0060600;
        s.size = 65536;

        if (mock_scenario == FACTORY_WRONG_SIZE) {
            s.size = 65535;
        }

        if (mock_scenario == FACTORY_SYMLINK) {
            s.mode = 0120777;
        }

        if (mock_scenario == FACTORY_FOREIGN_UID) {
            s.uid = 99;
        }

        if (mock_scenario == FACTORY_GROUP_WRITE) {
            s.mode = 0060660;
        }

        if (mock_scenario == FACTORY_HARDLINK) {
            s.nlink = 2;
        }

        if (mock_scenario == FACTORY_SIZED) {
            s.size = 65536;
        }

        if (mock_scenario == FACTORY_SIZE_ZERO) {
            s.size = 0;
        }

        if (mock_scenario == FACTORY_SIZE_BYTES) {
            s.size = 33554432;
        }

        if (mock_scenario == FACTORY_SIZE_EXTRA) {
            s.size = 65537;
        }
    }

    return s;
}

int qse_open(const char *path, int flags, ...) {
    int fd = !strcmp(path, "/proc/boot/startup-base")       ? 1
             : !strcmp(path, "/scripts/startup_cluster.sh") ? 2
             : !strcmp(path, "/log/startup-extension.sh")   ? 3
             : !strcmp(path, "/dev/ud/hd0")                 ? 4
                                                            : -1;

    if (fd < 0) {
        return -1;
    }

    if (fd == 4) {
        ++mock_device_opens;
    }

    if (fd == 3) {
        if (flags != (1 | 0400 | 02000 | 010000 | 020000)) {
            abort();
        }

        if (mock_created || mock_scenario == SNAPSHOT_OPEN_FAIL) {
            return -1;
        }

        mock_created = 1;
        mock_output_size = 0;
    } else if (flags != (010000 | 020000)) {
        abort();
    }

    return fd;
}

int qse_close(int fd) {
    if (fd < 1 || fd > 4) {
        abort();
    }

    ++mock_closes;

    return 0;
}

int qse_fstat(int fd, QseStat *s) {
    *s = mock_stat(fd);

    if (fd == 2 && mock_scenario == OPEN_REPLACED) {
        ++s->ino;
    }

    if (fd == 4 && mock_scenario == FACTORY_REPLACED && mock_pos[4]) {
        ++s->ino;
    }

    if (fd == 4 && mock_scenario == FACTORY_RDEV_REPLACED && mock_pos[4]) {
        ++s->rdev;
    }

    return 0;
}

int qse_lstat(const char *path, QseStat *s) {
    int fd = !strcmp(path, "/proc/boot/startup-base")       ? 1
             : !strcmp(path, "/scripts/startup_cluster.sh") ? 2
             : !strcmp(path, "/log/startup-extension.sh")   ? 3
             : !strcmp(path, "/dev/ud/hd0")                 ? 4
                                                            : -1;

    if (fd < 0 || (fd == 3 && !mock_created)) {
        return -1;
    }

    *s = mock_stat(fd);

    if (fd == 2 && mock_scenario == READ_REPLACED && mock_pos[fd]) {
        ++s->ino;
    }

    return 0;
}

long qse_read(int fd, void *out, QseSize size) {
    if (fd == 4) {
        if (size != 4096) {
            abort();
        }

        ++mock_device_reads;

        if (mock_scenario == FACTORY_EOF_EARLY && mock_pos[4] >= 16777216) {
            return 0;
        }

        if (mock_pos[4] >= 33554432 && mock_scenario != FACTORY_EXTRA) {
            return 0;
        }

        memset(out, 0, size);

        if (mock_scenario == FACTORY_NONZERO_END && mock_pos[4] == 33554432 - 4096) {
            ((unsigned char *)out)[4095] = 1;
        }

        mock_pos[4] += size;

        if (mock_scenario == FACTORY_SHORT_READ) {
            return 4095;
        }

        return (long)size;
    }

    if (fd != 1 && fd != 2) {
        abort();
    }

    if (mock_scenario == SHORT_INTERRUPTED && !mock_read_interrupt++) {
        mock_error = 4;

        return -1;
    }

    const unsigned char *data = fd == 1 ? mock_base : mock_installed;
    QseSize total = fd == 1 ? sizeof(mock_base) - 1 : mock_size;
    QseSize remaining = total - mock_pos[fd];

    if (size > remaining) {
        size = remaining;
    }

    if (mock_scenario == SHORT_INTERRUPTED && size > 2) {
        size = 2;
    }

    memcpy(out, data + mock_pos[fd], size);
    mock_pos[fd] += size;

    return (long)size;
}

long qse_write(int fd, const void *data, QseSize size) {
    if (fd == 2) {
        if (size >= sizeof(mock_diagnostic)) {
            abort();
        }

        memcpy(mock_diagnostic, data, size);
        mock_diagnostic[size] = 0;

        return (long)size;
    }

    if (fd != 3) {
        abort();
    }

    if (mock_scenario == SNAPSHOT_WRITE_FAIL) {
        mock_error = 5;

        return -1;
    }

    if (mock_scenario == SHORT_INTERRUPTED && !mock_write_interrupt++) {
        mock_error = 4;

        return -1;
    }

    if (mock_scenario == SHORT_INTERRUPTED && size > 2) {
        size = 2;
    }

    if (size > sizeof(mock_output) - mock_output_size) {
        abort();
    }

    memcpy(mock_output + mock_output_size, data, size);
    mock_output_size += size;

    return (long)size;
}

int qse_fsync(int fd) {
    if (fd != 3) {
        abort();
    }

    return mock_scenario == SNAPSHOT_SYNC_FAIL ? -1 : 0;
}

int qse_snprintf(char *out, QseSize size, const char *format, ...) {
    va_list args;

    va_start(args, format);
    int n = vsnprintf(out, size, format, args);
    va_end(args);

    return n;
}

void *qse_malloc(QseSize size) {
    return malloc(size);
}

void qse_free(void *p) {
    free(p);
}

int *qse_errno(void) {
    return &mock_error;
}

char *qse_getenv(const char *name) {
    if (!strcmp(name, "VOYEMU_STARTUP_EXTENSION")) {
        return (char *)mock_mode;
    }

    if (!strcmp(name, "VOYEMU_USERDATA_INITIALIZE")) {
        return (char *)mock_userdata_mode;
    }

    if (!strcmp(name, "VOYEMU_USERDATA_DEVICE")) {
        return (char *)mock_device;
    }

    abort();
}

void *qse_mmap(void *address, QseSize size, int prot, int flags, int fd, QseU64 physical) {
    if (address || size != 4096 || prot != 0x900 || flags != 0x10001 || fd != -1 ||
        physical != 0x867ff000ULL) {
        abort();
    }

    return mock_scenario == FACTORY_MAP_FAIL ? (void *)-1 : mock_marker;
}

int qse_munmap(void *page, QseSize size) {
    if (page != mock_marker || size != 4096) {
        abort();
    }

    return 0;
}

void qse_exit(int code) {
    mock_exit_code = code;
    longjmp(mock_jump, 1);
}

static void mock_reset(int scenario, int changed) {
    mock_scenario = scenario;
    mock_created = scenario == SNAPSHOT_EXISTS;
    mock_read_interrupt = 0;
    mock_write_interrupt = 0;
    mock_exit_code = -1;
    mock_error = 0;
    mock_closes = 0;
    mock_mode = "1";
    mock_userdata_mode = 0;
    mock_device = "/dev/ud/hd0";
    mock_device_reads = 0;
    mock_device_opens = 0;
    memset(mock_marker, 0, sizeof(mock_marker));
    mock_diagnostic[0] = 0;
    memset(mock_pos, 0, sizeof(mock_pos));
    memset(mock_output, 'Z', sizeof(mock_output));
    mock_output_size = mock_created ? 8 : 0;
    QseSize at =
            (QseSize)(strstr((const char *)mock_base, "clusterboot &") - (const char *)mock_base);
    const char hook[] = ". /var/example/startup.sh\n";
    QseSize hn = changed ? sizeof(hook) - 1 : 0;
    memcpy(mock_installed, mock_base, at);
    memcpy(mock_installed + at, hook, hn);
    memcpy(mock_installed + at + hn, mock_base + at, sizeof(mock_base) - 1 - at);
    mock_size = sizeof(mock_base) - 1 + hn;
}

static int mock_run(void) {
    if (!setjmp(mock_jump)) {
        qse_start();
    }

    return mock_exit_code;
}

static int test_operations(void) {
    mock_reset(NORMAL, 1);
    CHECK(mock_run() == 0 && mock_created && mock_closes == 2);
    CHECK(mock_output_size == strlen(". /var/example/startup.sh\n") &&
          !memcmp(mock_output, ". /var/example/startup.sh\n", mock_output_size));
    mock_reset(NORMAL, 0);
    CHECK(mock_run() == 0 && mock_created && !mock_output_size);

    for (int s = SYMLINK_INPUT; s <= READ_REPLACED; ++s) {
        mock_reset(s, 1);
        CHECK(mock_run() == 125 && !mock_created);
    }

    mock_reset(SHORT_INTERRUPTED, 1);
    CHECK(mock_run() == 0 && mock_output_size == strlen(". /var/example/startup.sh\n") &&
          mock_read_interrupt && mock_write_interrupt);
    mock_reset(SNAPSHOT_EXISTS, 1);
    CHECK(mock_run() == 125 && mock_output_size == 8 && mock_output[0] == 'Z');
    mock_reset(SNAPSHOT_OPEN_FAIL, 1);
    CHECK(mock_run() == 125 && !mock_created);
    mock_reset(SNAPSHOT_WRITE_FAIL, 1);
    CHECK(mock_run() == 125 && mock_created && !mock_output_size);
    mock_reset(SNAPSHOT_SYNC_FAIL, 1);
    CHECK(mock_run() == 125 && mock_created);
    mock_reset(NORMAL, 1);
    mock_installed[1] = 0;
    CHECK(mock_run() == 125 && !mock_created);
    mock_reset(NORMAL, 1);
    mock_mode = "other";
    CHECK(mock_run() == 125 && !mock_created && !mock_closes);
    mock_reset(NORMAL, 1);
    mock_mode = 0;
    qse_start();
    CHECK(mock_exit_code == -1 && !mock_created && !mock_closes);
    puts("QNX guest startup file/snapshot guards passed");

    return 0;
}

static void mock_factory_reset(int scenario, int marker) {
    mock_reset(scenario, 0);
    mock_userdata_mode = "1";

    if (marker) {
        QseU64 magic = 0x315241564d45564fULL;

        for (unsigned i = 0; i < 8; ++i) {
            mock_marker[i] = (unsigned char)(magic >> (i * 8));
        }
    }
}

static int mock_factory_run(void) {
    if (!setjmp(mock_jump)) {
        qse_userdata_start();
    }

    return mock_exit_code;
}

static int test_factory(void) {
    mock_factory_reset(NORMAL, 0);
    CHECK(mock_factory_run() == 1 && !mock_device_reads && !mock_device_opens && !mock_created &&
          !mock_diagnostic[0]);
    mock_factory_reset(NORMAL, 1);
    CHECK(mock_factory_run() == 0 && mock_device_reads == 8193 && mock_pos[4] == 33554432 &&
          mock_closes == 1 && !mock_created);
    mock_factory_reset(FACTORY_SIZED, 1);
    CHECK(mock_factory_run() == 0 && mock_device_reads == 8193);
    const int bad[] = {FACTORY_MAP_FAIL,    FACTORY_NONZERO_END, FACTORY_WRONG_SIZE,
                       FACTORY_EOF_EARLY,   FACTORY_EXTRA,       FACTORY_SYMLINK,
                       FACTORY_REPLACED,    FACTORY_SHORT_READ,  FACTORY_FOREIGN_UID,
                       FACTORY_GROUP_WRITE, FACTORY_HARDLINK,    FACTORY_RDEV_REPLACED,
                       FACTORY_SIZE_ZERO,   FACTORY_SIZE_BYTES,  FACTORY_SIZE_EXTRA};

    for (QseSize i = 0; i < sizeof(bad) / sizeof(*bad); ++i) {
        mock_factory_reset(bad[i], 1);
        CHECK(mock_factory_run() == 125 && !mock_created);
    }

    mock_factory_reset(FACTORY_NONZERO_END, 1);
    CHECK(mock_factory_run() == 125 &&
          strstr(mock_diagnostic, "step=device-nonzero errno=0 offset=33554431 got=4096") &&
          strstr(mock_diagnostic, "mode=60600 nlink=1 size=65536"));
    mock_factory_reset(FACTORY_EOF_EARLY, 1);
    CHECK(mock_factory_run() == 125 &&
          strstr(mock_diagnostic, "step=device-read-size errno=0 offset=16777216 got=0"));
    mock_factory_reset(FACTORY_GROUP_WRITE, 1);
    CHECK(mock_factory_run() == 125 && strstr(mock_diagnostic, "step=device-type errno=0") &&
          strstr(mock_diagnostic, "mode=60660"));
    mock_factory_reset(FACTORY_REPLACED, 1);
    CHECK(mock_factory_run() == 125 && strstr(mock_diagnostic, "step=device-after-fstat-identity"));
    mock_factory_reset(FACTORY_MAP_FAIL, 1);
    mock_error = 12;
    CHECK(mock_factory_run() == 125 && strstr(mock_diagnostic, "step=marker-map errno=12"));
    const char *paths[] = {"/dev/ud/hd", "/dev/ud/hd-1", "/dev/ud/hd0/../hd1", "/dev/hd0",
                           "/dev/ud/hd12345678901"};

    for (QseSize i = 0; i < sizeof(paths) / sizeof(*paths); ++i) {
        mock_factory_reset(NORMAL, 1);
        mock_device = paths[i];
        CHECK(mock_factory_run() == 125 && !mock_device_reads && !mock_device_opens);
    }

    mock_factory_reset(NORMAL, 1);
    mock_marker[0] ^= 1;
    CHECK(mock_factory_run() == 125 && !mock_device_reads);
    mock_factory_reset(NORMAL, 1);
    mock_marker[4095] = 1;
    CHECK(mock_factory_run() == 125 && !mock_device_reads);
    mock_factory_reset(NORMAL, 1);
    mock_userdata_mode = "invalid";
    CHECK(mock_factory_run() == 125 && !mock_device_reads);
    mock_factory_reset(NORMAL, 1);
    mock_userdata_mode = 0;
    qse_userdata_start();
    CHECK(mock_exit_code == -1 && !mock_device_reads);
    puts("QNX userdata factory authorization/full-extent guards passed");

    return 0;
}

int main(void) {
    return test_bytes() || test_operations() || test_factory();
}
