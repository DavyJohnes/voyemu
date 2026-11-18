/* Private host mocks execute the production helper, including every mount call. */
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define QSE_TEST
#define QSC_TEST
#include "startup_extension.c"
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "scenario=%d line=%d failed: %s\n", scenario, __LINE__, #x);           \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define REQUIRE(x)                                                                                 \
    do {                                                                                           \
        if (!(x))                                                                                  \
            abort();                                                                               \
    } while (0)
enum {
    NORMAL,
    REGULAR,
    MISSING,
    FOREIGN_LINK,
    DANGLING,
    FOREIGN_TARGET,
    HARDLINK_TARGET,
    WRITABLE_TARGET,
    FOREIGN_PARENT,
    SYMLINK_PARENT,
    OTHER_ROOT_FS,
    OTHER_PARENT_FS,
    WRONG_FS_TYPE,
    ROOT_RW,
    NONROOT,
    ROOT_FOREIGN_GROUP,
    ROOT_WORLD_WRITABLE,
    ROOT_SPECIAL_MODE,
    PARENT_GROUP_WRITABLE,
    SOURCE_SYMLINK,
    SOURCE_OWNER,
    SOURCE_HARDLINK,
    SOURCE_WRITABLE_GROUP,
    SOURCE_CHANGED,
    SOURCE_GROWN,
    SOURCE_CHANGED_SECOND_READ,
    SOURCE_CHANGED_THIRD_READ,
    TEMP_EXISTS,
    CREATE_FAIL,
    WRITE_FAIL,
    FILE_SYNC_FAIL,
    RENAME_FAIL,
    PARENT_SYNC_FAIL,
    RW_MOUNT_FAIL,
    RW_FLAGS_DIFFER,
    RESTORE_FAIL,
    PARSE_FAIL,
    SHORT_IO,
    RETAINED_REPLACED,
    TEMP_REPLACED,
    MODE_UMASK,
    CHMOD_FAIL,
    CLOSE_FAIL,
    READ_FAIL,
    ALLOC_FAIL,
    UNDERLYING_SAME,
    UNDERLYING_DIFFERENT,
    UNDERLYING_UNSAFE,
    UNLINK_PREFIX_FAIL,
    RESTORE_PREFIX_FAIL,
    FILESYSTEM_SYMLINK,
    INIT_PERMISSION,
    INIT_LINK_FAIL,
    INIT_CLOSE_FAIL,
    INIT_FLAGS_CHANGED,
    RETAINED_FILE_MOUNT_BAD,
    SOURCE_FILE_MOUNT_BAD,
    TEMP_FILE_MOUNT_BAD,
    PUBLISHED_FILE_MOUNT_BAD,
    SOURCE_WRONG_DEV,
    RETAINED_WRONG_DEV,
    RENAME_CTIME_CHANGED,
    TEMP_NAMESPACE_FULL,
    INIT_DIRECTORY_MISSING,
    INIT_MKDIR_FAIL,
    REGULAR_HIGH_FLAGS
};
static const unsigned char payload_builtin[] =
        "<clsboot_root>\n<env>QGFX_GL_BATCH=1</env>\n<env>LD_PRELOAD=/var/extension.so:/proc/boot/bin/Snapdragon.rgb</env>\n<env>WAL=retain</env>\n</clsboot_root>\n";
static const unsigned char *payload = payload_builtin;
static QseSize payload_size = sizeof(payload_builtin) - 1;
typedef struct {
    QseStat st;
    unsigned char bytes[32768];
    int present;
} Object;
typedef struct {
    int object, active;
    QseSize position;
} Descriptor;
static unsigned scenarios_run;
enum {
    LOG_FOREIGN_GROUP = 100,
    LOG_WORLD_WRITABLE,
    LOG_SPECIAL_MODE,
    LOG_CHILD_WRITABLE,
    LOG_ROOT_FS,
    LOG_CHILD_FS,
    LOG_WRONG_TYPE,
    LOG_READONLY
};
enum {
    INIT_PARENT_STABLE = 200,
    INIT_PARENT_REPLACED,
    INIT_PARENT_MODE_CHANGED,
    INIT_PARENT_GROUP_CHANGED,
    INIT_PARENT_TWO_LINKS
};
static Object objects[9];
static Descriptor descriptors[64];
static int next_fd, scenario, error_code, mount_calls, rename_calls, writes, unlink_calls,
        source_opens, read_interrupt, write_interrupt, parse_calls, live_fds;
static int parent_syncs, chmod_calls, exit_status, failed_close, prefix_active, prefix_unlinks,
        prefix_restores, ram_mkdirs;
static QseStat prefix_stat;
static QseU64 flags, original_flags;
static const char *environment;
static char diagnostic[256], metadata[2048];
static jmp_buf jump;

static int path_object(const char *path) {
    for (int i = 0; i < 5; ++i) {
        if (!strcmp(path, qsc_paths[i])) {
            return i;
        }
    }

    if (!strcmp(path, qsc_target)) {
        return 5;
    }

    if (!strcmp(path, qsc_legacy)) {
        return 6;
    }

    if (!strncmp(path, "/etc/clsboot/.voyemu-supervisor-321-",
                 sizeof("/etc/clsboot/.voyemu-supervisor-321-") - 1)) {
        return scenario == TEMP_NAMESPACE_FULL ||
                               (scenario == TEMP_EXISTS &&
                                !strcmp(path, "/etc/clsboot/.voyemu-supervisor-321-0"))
                       ? 8
                       : 7;
    }

    return -1;
}

static QseStat object_stat(int object) {
    return objects[object].st;
}

static void reset(int which) {
    ++scenarios_run;
    memset(objects, 0, sizeof(objects));
    memset(descriptors, 0, sizeof(descriptors));
    scenario = which;
    error_code = 0;
    next_fd = 10;
    mount_calls = rename_calls = writes = unlink_calls = source_opens = read_interrupt =
            write_interrupt = parse_calls = live_fds = 0;
    parent_syncs = chmod_calls = failed_close = prefix_unlinks = prefix_restores = ram_mkdirs = 0;
    prefix_active = 1;
    exit_status = -1;
    environment = "ensure";
    diagnostic[0] = metadata[0] = 0;
    original_flags = 0x0400007fUL;
    flags = original_flags;

    for (int i = 0; i < 9; ++i) {
        objects[i].present = 1;
        objects[i].st = (QseStat){.ino = (QseU64)(100 + i),
                                  .dev = i == 3 || i == 4 || i == 6 ? 22 : 11,
                                  .uid = 0,
                                  .gid = 0,
                                  .mode = i < 5 ? 0040755 : 0100644,
                                  .nlink = i < 5 ? 2 : 1,
                                  .mtim = {10, 20},
                                  .ctim = {30, 40}};
    }

    /* Pinned original A9 system_la.img: root inode 1 is uid=gid=0, mode 040775; /etc and
     * /etc/clsboot are 040755. */
    objects[0].st.mode = 0040775;
    objects[3].st.mode = 0040775;
    objects[5].st.mode = 0120777;
    objects[5].st.size = (QseI64)sizeof(qsc_legacy) - 1;
    prefix_stat = objects[5].st;
    prefix_stat.dev = 33;
    objects[5].present = 0;
    objects[6].st.mode = 0100666;
    objects[6].st.size = payload_size;
    memcpy(objects[6].bytes, payload, payload_size);
    objects[7].present = 0;

    if (which == REGULAR || which == RETAINED_REPLACED || which == HARDLINK_TARGET ||
        which == WRITABLE_TARGET || which == RETAINED_FILE_MOUNT_BAD ||
        which == RETAINED_WRONG_DEV || which == REGULAR_HIGH_FLAGS) {
        objects[5] = objects[6];
        objects[5].st.dev = 11;
        objects[5].st.ino = 105;
        objects[5].st.mode = 0100600;
        prefix_active = 0;
    }

    if (which == MISSING) {
        objects[5].present = 0;
        prefix_active = 0;
    }

    if (which == DANGLING) {
        objects[6].present = 0;
    }

    if (which == FOREIGN_TARGET) {
        prefix_stat.uid = 7;
    }

    if (which == HARDLINK_TARGET) {
        objects[5].st.nlink = 2;
    }

    if (which == WRITABLE_TARGET) {
        objects[5].st.mode = 0100666;
    }

    if (which == ROOT_FOREIGN_GROUP) {
        objects[0].st.gid = 7;
    }

    if (which == ROOT_WORLD_WRITABLE) {
        objects[0].st.mode = 0040777;
    }

    if (which == ROOT_SPECIAL_MODE) {
        objects[0].st.mode = 0042775;
    }

    if (which == PARENT_GROUP_WRITABLE) {
        objects[2].st.mode = 0040775;
    }

    if (which == LOG_FOREIGN_GROUP) {
        objects[3].st.gid = 7;
    }

    if (which == LOG_WORLD_WRITABLE) {
        objects[3].st.mode = 0040777;
    }

    if (which == LOG_SPECIAL_MODE) {
        objects[3].st.mode = 0042775;
    }

    if (which == LOG_CHILD_WRITABLE) {
        objects[4].st.mode = 0040775;
    }

    if (which == FOREIGN_PARENT) {
        objects[2].st.uid = 7;
    }

    if (which == SYMLINK_PARENT) {
        objects[2].st.mode = 0120777;
    }

    if (which == OTHER_ROOT_FS) {
        objects[2].st.dev = 99;
    }

    if (which == REGULAR_HIGH_FLAGS) {
        original_flags = flags = 0x10400007fULL;
    }

    if (which == ROOT_RW) {
        flags = original_flags & ~1ULL;
    }

    if (which == SOURCE_WRONG_DEV) {
        objects[6].st.dev = 999;
    }

    if (which == RETAINED_WRONG_DEV) {
        objects[5].st.dev = 999;
    }

    if (which == SOURCE_SYMLINK) {
        objects[6].st.mode = 0120777;
    }

    if (which == SOURCE_OWNER) {
        objects[6].st.uid = 9;
    }

    if (which == SOURCE_HARDLINK) {
        objects[6].st.nlink = 2;
    }

    if (which == SOURCE_WRITABLE_GROUP) {
        objects[6].st.mode = 0100664;
    }

    if (which == FILESYSTEM_SYMLINK) {
        prefix_active = 0;
        objects[5].present = 1;
    }

    if (which == UNDERLYING_SAME || which == UNDERLYING_DIFFERENT || which == UNDERLYING_UNSAFE) {
        objects[5] = objects[6];
        objects[5].st.dev = 11;
        objects[5].st.ino = 105;
        objects[5].st.mode = 0100644;

        if (which == UNDERLYING_DIFFERENT) {
            objects[5].bytes[1] ^= 1;
        }

        if (which == UNDERLYING_UNSAFE) {
            objects[5].st.nlink = 2;
        }
    }

    if (which == TEMP_EXISTS || which == TEMP_NAMESPACE_FULL) {
        objects[8].present = 1;
        objects[8].st.size = 9;
        memcpy(objects[8].bytes, "foreign!", 9);
    } else {
        objects[8].present = 0;
    }
}

int qse_open(const char *path, int mode, ...) {
    int object = path_object(path);

    REQUIRE(object >= 0);

    if (object == 7 || object == 8) {
        REQUIRE(mode == QSC_CREATE);
        REQUIRE(!(flags & 1));

        if (objects[object].present) {
            error_code = 17;

            return -1;
        }

        if (scenario == CREATE_FAIL) {
            error_code = 5;

            return -1;
        }

        objects[7].present = 1;
        objects[7].st.mode = scenario == MODE_UMASK ? 0100000 : 0100600;
        objects[7].st.size = 0;
    } else {
        REQUIRE(mode == (object < 5 ? (QSC_READ | QSC_DIRECTORY) : QSC_READ));
    }

    if (!objects[object].present) {
        error_code = 2;

        return -1;
    }

    if (object == 6) {
        ++source_opens;

        if (scenario == SOURCE_CHANGED_SECOND_READ && source_opens == 2) {
            ++objects[6].st.mtim.nanoseconds;
        }

        if (scenario == SOURCE_CHANGED_THIRD_READ && source_opens == 3) {
            ++objects[6].st.ino;
        }
    }

    int fd = next_fd++;
    REQUIRE(fd < 64);
    descriptors[fd] = (Descriptor){.object = object, .active = 1};
    ++live_fds;

    return fd;
}

int qse_close(int fd) {
    REQUIRE(fd >= 10 && fd < 64 && descriptors[fd].active);
    descriptors[fd].active = 0;
    --live_fds;

    if (((scenario == CLOSE_FAIL && descriptors[fd].object == 6) ||
         (scenario == INIT_CLOSE_FAIL && descriptors[fd].object == 4)) &&
        !failed_close) {
        failed_close = 1;
        error_code = 5;

        return -1;
    }

    return 0;
}

int qse_fstat(int fd, QseStat *out) {
    REQUIRE(fd >= 10 && fd < 64 && descriptors[fd].active);
    int object = descriptors[fd].object;
    *out = object_stat(object);

    if (scenario == RETAINED_REPLACED && object == 5) {
        ++out->ino;
    }

    return 0;
}

int qse_lstat(const char *path, QseStat *out) {
    int object = path_object(path);

    REQUIRE(object >= 0);

    if (object == 5 && prefix_active) {
        *out = prefix_stat;

        return 0;
    }

    if (!objects[object].present) {
        error_code = scenario == INIT_PERMISSION && object == 5 ? 13 : 2;

        return -1;
    }

    if (scenario == SOURCE_GROWN && object == 6 && source_opens == 0 &&
        objects[6].st.size == (QseI64)payload_size) {
        static int source_stats;
        source_stats++;

        if (source_stats % 2 == 0) {
            ++objects[6].st.size;
        }
    }

    if (scenario == TEMP_REPLACED && object == 7 && source_opens == 3) {
        ++objects[7].st.ino;
    }

    *out = object_stat(object);

    return 0;
}

long qse_read(int fd, void *out, QseSize size) {
    REQUIRE(fd >= 10 && fd < 64 && descriptors[fd].active);
    Descriptor *d = &descriptors[fd];
    Object *o = &objects[d->object];
    REQUIRE(d->object == 5 || d->object == 6);

    if (scenario == READ_FAIL) {
        error_code = 5;

        return -1;
    }

    if (scenario == SHORT_IO && !read_interrupt++) {
        error_code = 4;

        return -1;
    }

    QseSize remaining = (QseSize)o->st.size - d->position;

    if (size > remaining) {
        size = remaining;
    }

    if (scenario == SHORT_IO && size > 7) {
        size = 7;
    }

    memcpy(out, o->bytes + d->position, size);
    d->position += size;

    if (!size && scenario == SOURCE_CHANGED && d->object == 6) {
        ++o->st.ctim.nanoseconds;
    }

    return (long)size;
}

long qse_write(int fd, const void *bytes, QseSize size) {
    if (fd == 2) {
        if (size >= 18 && !memcmp(bytes, "QSTORE SUPERVISOR ", 18)) {
            QseSize used = strlen(metadata);
            REQUIRE(size < sizeof(metadata) - used);
            memcpy(metadata + used, bytes, size);
            metadata[used + size] = 0;
        } else {
            REQUIRE(size < sizeof(diagnostic));
            memcpy(diagnostic, bytes, size);
            diagnostic[size] = 0;
        }

        return (long)size;
    }

    REQUIRE(fd >= 10 && fd < 64 && descriptors[fd].active && descriptors[fd].object == 7);
    ++writes;

    if (scenario == WRITE_FAIL) {
        error_code = 5;

        return -1;
    }

    if (scenario == SHORT_IO && !write_interrupt++) {
        error_code = 4;

        return -1;
    }

    if (scenario == SHORT_IO && size > 3) {
        size = 3;
    }

    Object *o = &objects[7];
    REQUIRE((QseSize)o->st.size + size <= sizeof(o->bytes));
    memcpy(o->bytes + o->st.size, bytes, size);
    o->st.size += (QseI64)size;
    ++o->st.mtim.nanoseconds;

    return (long)size;
}

int qse_fsync(int fd) {
    REQUIRE(fd >= 10 && fd < 64 && descriptors[fd].active);
    int object = descriptors[fd].object;
    REQUIRE(object == 7 || object == 2);

    if (object == 2) {
        ++parent_syncs;

        if (scenario == PARENT_SYNC_FAIL && rename_calls) {
            error_code = 5;

            return -1;
        }
    }

    if (object == 7 && scenario == FILE_SYNC_FAIL) {
        error_code = 5;

        return -1;
    }

    return 0;
}

int qse_snprintf(char *out, QseSize size, const char *format, ...) {
    va_list args;

    va_start(args, format);
    int n = vsnprintf(out, size, format, args);
    va_end(args);

    return n;
}

void *qse_malloc(QseSize size) {
    return scenario == ALLOC_FAIL ? 0 : malloc(size);
}

void qse_free(void *pointer) {
    free(pointer);
}

int *qse_errno(void) {
    return &error_code;
}

char *qse_getenv(const char *name) {
    if (!strcmp(name, "VOYEMU_SUPERVISOR_CONFIG")) {
        return (char *)environment;
    }

    return 0;
}

void qse_exit(int code) {
    exit_status = code;
    longjmp(jump, 1);
}

void *qse_mmap(void *address, QseSize size, int prot, int map, int fd, QseU64 physical) {
    (void)address;
    (void)size;
    (void)prot;
    (void)map;
    (void)fd;
    (void)physical;
    abort();
}

int qse_munmap(void *address, QseSize size) {
    (void)address;
    (void)size;
    abort();
}

long qsc_readlink(const char *path, char *out, QseSize maximum) {
    REQUIRE(!strcmp(path, qsc_target));
    const char *value = scenario == FOREIGN_LINK ? "/log/elsewhere.xml" : qsc_legacy;
    QseSize size = strlen(value);

    if (size > maximum) {
        size = maximum;
    }

    /* NOLINTNEXTLINE(bugprone-not-null-terminated-result): readlink results carry no NUL. */
    memcpy(out, value, size);

    return (long)size;
}

int qsc_mkdir(const char *path, unsigned mode) {
    REQUIRE(!strcmp(path, qsc_paths[4]) && mode == 0755 && !objects[4].present && (flags & 1));
    ++ram_mkdirs;

    if (scenario == INIT_MKDIR_FAIL) {
        error_code = 13;

        return -1;
    }

    objects[4].present = 1;
    objects[4].st.mode = 0040755;

    if (scenario != INIT_PARENT_STABLE) {
        ++objects[3].st.nlink;
    }

    if (scenario == INIT_PARENT_REPLACED) {
        ++objects[3].st.ino;
    }

    if (scenario == INIT_PARENT_MODE_CHANGED) {
        objects[3].st.mode = 0040777;
    }

    if (scenario == INIT_PARENT_GROUP_CHANGED) {
        objects[3].st.gid = 7;
    }

    if (scenario == INIT_PARENT_TWO_LINKS) {
        ++objects[3].st.nlink;
    }

    return 0;
}

int qsc_fchmod(int fd, unsigned mode) {
    REQUIRE(fd >= 10 && fd < 64 && descriptors[fd].active && descriptors[fd].object == 7 &&
            mode == 0600);
    ++chmod_calls;

    if (scenario == CHMOD_FAIL) {
        error_code = 5;

        return -1;
    }

    objects[7].st.mode = 0100600;

    return 0;
}

int qsc_rename(const char *from, const char *to) {
    REQUIRE(path_object(from) == 7 && !strcmp(to, qsc_target) && objects[7].present &&
            !(flags & 1));
    ++rename_calls;
    REQUIRE(!prefix_active && !objects[5].present && objects[7].st.mode == 0100600 &&
            objects[7].st.size == (QseI64)payload_size &&
            !memcmp(objects[7].bytes, payload, payload_size));

    if (scenario == RENAME_FAIL || scenario == RESTORE_PREFIX_FAIL) {
        error_code = 5;

        return -1;
    }

    objects[5] = objects[7];
    objects[7].present = 0;

    if (scenario == RENAME_CTIME_CHANGED) {
        ++objects[5].st.ctim.nanoseconds;
    }

    return 0;
}

int qsc_unlink(const char *path) {
    REQUIRE(path_object(path) == 7 && objects[7].present && !(flags & 1));
    objects[7].present = 0;
    ++unlink_calls;

    return 0;
}

int qsc_pathmgr_unlink(const char *path) {
    REQUIRE(!strcmp(path, qsc_target));
    ++prefix_unlinks;

    if (!prefix_active || scenario == UNLINK_PREFIX_FAIL) {
        error_code = 2;

        return -1;
    }

    prefix_active = 0;

    return 0;
}

int qsc_pathmgr_symlink(const char *source, const char *target) {
    REQUIRE(!strcmp(source, qsc_legacy) && !strcmp(target, qsc_target) && !prefix_active);
    ++prefix_restores;

    if (scenario == RESTORE_PREFIX_FAIL || scenario == INIT_LINK_FAIL) {
        error_code = 5;

        return -1;
    }

    prefix_active = 1;
    ++prefix_stat.ino;

    if (scenario == INIT_FLAGS_CHANGED) {
        flags |= 0x100;
    }

    return 0;
}

int qsc_getpid(void) {
    return 321;
}

unsigned qsc_geteuid(void) {
    return scenario == NONROOT ? 7 : 0;
}

int qsc_fstatvfs(int fd, QscStatvfs *out) {
    REQUIRE(fd >= 10 && fd < 64 && descriptors[fd].active);
    int object = descriptors[fd].object;
    REQUIRE(object >= 0 && object < 8);
    memset(out, 0, sizeof(*out));
    out->fsid = object == 3 || object == 4 || object == 6 ? 0x200000058ULL : 0x10000004dULL;

    if ((scenario == OTHER_PARENT_FS && object == 2) ||
        (scenario == RETAINED_FILE_MOUNT_BAD && object == 5) ||
        (scenario == SOURCE_FILE_MOUNT_BAD && object == 6) ||
        (scenario == TEMP_FILE_MOUNT_BAD && object == 7) ||
        (scenario == PUBLISHED_FILE_MOUNT_BAD && object == 5 && rename_calls)) {
        out->fsid = 999;
    }

    out->flag = object == 3 || object == 4 || object == 6 ? 0x200000020ULL : flags;
    strcpy(out->basetype, scenario == WRONG_FS_TYPE ? "ram" : "qnx6");

    if (scenario == LOG_ROOT_FS && object >= 3 && object <= 4) {
        out->fsid = 0x10000004dULL;
    }

    if (scenario == LOG_CHILD_FS && object == 4) {
        out->fsid = 999;
    }

    if (scenario == LOG_WRONG_TYPE && object == 3) {
        strcpy(out->basetype, "ram");
    }

    if (scenario == LOG_READONLY && object == 3) {
        out->flag |= 1;
    }

    return 0;
}

char *qsc_mount_parse_generic_args(char *option, int *mount_flags) {
    REQUIRE(!strcmp(option, "remount"));
    ++parse_calls;

    if (scenario == PARSE_FAIL) {
        return option;
    }

    *mount_flags |= 0x10000000;

    return 0;
}

int qsc_mount(const char *device, const char *directory, int mount_flags, const char *type,
              const void *data, int size) {
    REQUIRE(!device && !strcmp(directory, "/") && !strcmp(type, "qnx6") && !data && size == -1);
    REQUIRE(mount_flags & 0x10000000);
    QseU64 requested = (QseU64)mount_flags & ~0x10000000ULL;
    REQUIRE(requested == original_flags || requested == (original_flags & ~1ULL));
    ++mount_calls;

    if (scenario == RESTORE_FAIL && mount_calls == 2) {
        error_code = 5;

        return -1;
    }

    flags = requested;

    if (scenario == RW_MOUNT_FAIL && mount_calls == 1) {
        error_code = 5;

        return -1;
    }

    if (scenario == RW_FLAGS_DIFFER && mount_calls == 1) {
        flags |= 0x00800000;
    }

    return 0;
}

static int check_migration(void) {
    reset(NORMAL);
    CHECK(qsc_ensure() == 0);
    CHECK(!live_fds && mount_calls == 2 && parse_calls == 2 && flags == original_flags &&
          rename_calls == 1 && parent_syncs == 1 && source_opens == 3 && chmod_calls == 1 &&
          !objects[7].present && !prefix_active && prefix_unlinks == 1 && !prefix_restores &&
          !diagnostic[0]);
    CHECK(strstr(metadata, "directory=0 mode=40775 uid=0 gid=0 dev=11") &&
          strstr(metadata, "directory=3 mode=40775 uid=0 gid=0 dev=22") &&
          strstr(metadata, "mount=3 flags=0x200000020 basetype=qnx6 fsid=0x200000058") &&
          strstr(metadata, "flags=0x400007f basetype=qnx6"));
    CHECK(objects[5].st.mode == 0100600 && objects[5].st.size == (QseI64)payload_size &&
          !memcmp(objects[5].bytes, payload, payload_size));
    CHECK(objects[6].st.mode == 0100666 && !memcmp(objects[6].bytes, payload, payload_size));
    reset(SHORT_IO);
    CHECK(qsc_ensure() == 0 && !live_fds && flags == original_flags && read_interrupt &&
          write_interrupt && rename_calls == 1);
    reset(MODE_UMASK);
    CHECK(qsc_ensure() == 0 && objects[5].st.mode == 0100600 && flags == original_flags);
    reset(TEMP_EXISTS);
    CHECK(qsc_ensure() == 0 && objects[8].present && !memcmp(objects[8].bytes, "foreign!", 9) &&
          !unlink_calls && !live_fds && flags == original_flags);
    reset(RENAME_CTIME_CHANGED);
    CHECK(qsc_ensure() == 0 && objects[5].st.ctim.nanoseconds == 41 && !live_fds &&
          !prefix_active && flags == original_flags);
    puts("migration: byte-preserving atomic publish, narrow legacy 0666, short I/O, umask, all mount flags restored");

    return 0;
}

static int check_preserve(void) {
    reset(REGULAR);
    Object before;
    memcpy(&before, &objects[5], sizeof(before));
    CHECK(qsc_ensure() == 0 && !live_fds && !mount_calls && !rename_calls && !writes &&
          !source_opens &&
          !memcmp((const unsigned char *)&before, (const unsigned char *)&objects[5],
                  sizeof(before)));
    reset(REGULAR_HIGH_FLAGS);
    CHECK(qsc_ensure() == 0 && !live_fds && !mount_calls && flags == 0x10400007fULL);
    reset(MISSING);
    CHECK(qsc_ensure() == 2 && !live_fds && !mount_calls && !writes && !rename_calls);
    reset(NORMAL);
    environment = 0;
    qse_supervisor_start();
    CHECK(exit_status == -1 && !live_fds && !mount_calls);
    reset(NORMAL);
    environment = "inspect";

    if (!setjmp(jump)) {
        qse_supervisor_start();
    }

    CHECK(exit_status == 125 && !live_fds && !mount_calls);
    reset(NORMAL);

    if (!setjmp(jump)) {
        qse_supervisor_start();
    }

    CHECK(exit_status == 0 && !live_fds && flags == original_flags);
    reset(UNDERLYING_SAME);
    before = objects[5];
    CHECK(qsc_ensure() == 0 && !live_fds && flags == original_flags && mount_calls == 2 &&
          !rename_calls && !prefix_active && prefix_unlinks == 1 && !prefix_restores &&
          !objects[7].present &&
          !memcmp((const unsigned char *)&before, (const unsigned char *)&objects[5],
                  sizeof(before)));
    reset(REGULAR);
    objects[0].st.mode = 0040755;
    CHECK(qsc_ensure() == 0 && !mount_calls && !writes && !live_fds &&
          strstr(metadata, "directory=0 mode=40755 uid=0 gid=0"));
    puts("preserve: regular and byte-equal hidden config untouched, missing returns 2, only explicit ensure dispatches");

    return 0;
}

static int check_failures(void) {
    const int before[] = {FOREIGN_LINK,
                          DANGLING,
                          FOREIGN_TARGET,
                          HARDLINK_TARGET,
                          WRITABLE_TARGET,
                          FOREIGN_PARENT,
                          SYMLINK_PARENT,
                          OTHER_ROOT_FS,
                          OTHER_PARENT_FS,
                          WRONG_FS_TYPE,
                          ROOT_RW,
                          NONROOT,
                          ROOT_FOREIGN_GROUP,
                          ROOT_WORLD_WRITABLE,
                          ROOT_SPECIAL_MODE,
                          PARENT_GROUP_WRITABLE,
                          SOURCE_SYMLINK,
                          SOURCE_OWNER,
                          SOURCE_HARDLINK,
                          SOURCE_WRITABLE_GROUP,
                          SOURCE_CHANGED,
                          SOURCE_GROWN,
                          SOURCE_CHANGED_SECOND_READ,
                          RETAINED_REPLACED,
                          CLOSE_FAIL,
                          READ_FAIL,
                          ALLOC_FAIL,
                          RETAINED_FILE_MOUNT_BAD,
                          SOURCE_FILE_MOUNT_BAD,
                          SOURCE_WRONG_DEV,
                          RETAINED_WRONG_DEV};

    for (QseSize i = 0; i < sizeof(before) / sizeof(*before); ++i) {
        reset(before[i]);
        CHECK(qsc_ensure() == 125 && !live_fds && !mount_calls && !rename_calls && !writes);
        CHECK(strstr(diagnostic, "QSTORE FAIL supervisor-config step="));
    }

    const int after[] = {TEMP_NAMESPACE_FULL,
                         CREATE_FAIL,
                         WRITE_FAIL,
                         FILE_SYNC_FAIL,
                         RENAME_FAIL,
                         PARENT_SYNC_FAIL,
                         RW_MOUNT_FAIL,
                         RW_FLAGS_DIFFER,
                         SOURCE_CHANGED_THIRD_READ,
                         TEMP_REPLACED,
                         CHMOD_FAIL,
                         UNDERLYING_DIFFERENT,
                         UNDERLYING_UNSAFE,
                         UNLINK_PREFIX_FAIL,
                         FILESYSTEM_SYMLINK,
                         TEMP_FILE_MOUNT_BAD,
                         PUBLISHED_FILE_MOUNT_BAD};

    for (QseSize i = 0; i < sizeof(after) / sizeof(*after); ++i) {
        reset(after[i]);
        CHECK(qsc_ensure() == 125 && !live_fds && mount_calls == 2 && flags == original_flags);

        if (after[i] != FILESYSTEM_SYMLINK) {
            CHECK(prefix_active);
        }

        if (after[i] == PARENT_SYNC_FAIL) {
            CHECK(objects[5].st.mode == 0100600 && rename_calls == 1 && prefix_restores == 1);
        }

        if (after[i] == UNDERLYING_DIFFERENT) {
            CHECK(objects[5].st.mode == 0100644 &&
                  objects[5].bytes[1] == (unsigned char)(payload[1] ^ 1) && prefix_restores == 1);
        }

        if (after[i] == UNDERLYING_UNSAFE) {
            CHECK(objects[5].st.nlink == 2 && prefix_restores == 1);
        }

        if (after[i] == TEMP_NAMESPACE_FULL) {
            CHECK(objects[8].present && !unlink_calls && !memcmp(objects[8].bytes, "foreign!", 9));
        } else if (after[i] != TEMP_REPLACED) {
            CHECK(!objects[7].present);
        }
    }

    reset(RESTORE_FAIL);
    CHECK(qsc_ensure() == 125 && !live_fds && mount_calls == 2 && !(flags & 1) &&
          strstr(diagnostic, "step=restore-flags"));
    reset(RESTORE_PREFIX_FAIL);
    CHECK(qsc_ensure() == 125 && !live_fds && flags == original_flags && !prefix_active &&
          prefix_restores == 1 && strstr(diagnostic, "step=prefix-restore"));
    reset(PARSE_FAIL);
    CHECK(qsc_ensure() == 125 && !live_fds && parse_calls == 2 && !mount_calls &&
          flags == original_flags);
    puts("failures: authority, identity, snapshot, hidden config, prefix rollback, cleanup, remount and fsync paths fail closed");

    return 0;
}

static int check_initialize(void) {
    reset(MISSING);
    objects[6].present = 0;
    CHECK(qsc_initialize() == 0 && !live_fds && prefix_active && prefix_restores == 1 &&
          !mount_calls && !writes && !rename_calls && !objects[5].present &&
          flags == original_flags);
    reset(MISSING);
    CHECK(qsc_initialize() == 125 && !live_fds && !prefix_active && !prefix_restores &&
          !mount_calls && !writes);
    const int reject[] = {NORMAL,
                          REGULAR,
                          FOREIGN_LINK,
                          FOREIGN_TARGET,
                          FOREIGN_PARENT,
                          SYMLINK_PARENT,
                          OTHER_ROOT_FS,
                          OTHER_PARENT_FS,
                          WRONG_FS_TYPE,
                          ROOT_RW,
                          NONROOT,
                          ROOT_FOREIGN_GROUP,
                          ROOT_WORLD_WRITABLE,
                          ROOT_SPECIAL_MODE,
                          PARENT_GROUP_WRITABLE};

    for (QseSize i = 0; i < sizeof(reject) / sizeof(*reject); ++i) {
        reset(reject[i]);
        CHECK(qsc_initialize() == 125 && !live_fds && !prefix_restores && !mount_calls && !writes &&
              !ram_mkdirs);
    }

    const int special[] = {INIT_PERMISSION, INIT_LINK_FAIL, INIT_CLOSE_FAIL, INIT_FLAGS_CHANGED};

    for (QseSize i = 0; i < sizeof(special) / sizeof(*special); ++i) {
        reset(special[i]);
        prefix_active = 0;
        objects[5].present = 0;
        objects[6].present = 0;
        CHECK(qsc_initialize() == 125 && !live_fds && !mount_calls && !writes && !rename_calls);

        if (special[i] == INIT_FLAGS_CHANGED || special[i] == INIT_CLOSE_FAIL) {
            CHECK(!prefix_active && prefix_unlinks == 1);
        }

        if (special[i] == INIT_PERMISSION) {
            CHECK(strstr(diagnostic, "errno=13"));
        }
    }

    reset(MISSING);
    objects[6].present = 0;
    environment = "ensure";

    if (!setjmp(jump)) {
        qse_supervisor_start();
    }

    CHECK(exit_status == 2 && !live_fds && prefix_active && !mount_calls && !writes);
    reset(INIT_DIRECTORY_MISSING);
    prefix_active = 0;
    objects[5].present = objects[6].present = objects[4].present = 0;
    CHECK(qsc_initialize() == 0 && ram_mkdirs == 1 && objects[4].st.mode == 0040755 &&
          prefix_active && !live_fds && !mount_calls && !writes);
    reset(INIT_MKDIR_FAIL);
    prefix_active = 0;
    objects[5].present = objects[6].present = objects[4].present = 0;
    CHECK(qsc_initialize() == 125 && ram_mkdirs == 1 && !objects[4].present && !prefix_active &&
          !live_fds && !mount_calls && !writes);
    reset(REGULAR);
    objects[4].present = 0;
    CHECK(qsc_initialize() == 125 && !ram_mkdirs && !live_fds);
    reset(MISSING);
    objects[6].present = 0;
    environment = "initialize";

    if (!setjmp(jump)) {
        qse_supervisor_start();
    }

    CHECK(exit_status == 125 && !prefix_active && !live_fds && !mount_calls);
    puts("initialize: strict target/source absence, RAM mkdir only after proof, ensure returns2 with prefix ready, no root writes");

    return 0;
}

static int check_log_mount(void) {
    const int reject[] = {LOG_FOREIGN_GROUP,  LOG_WORLD_WRITABLE, LOG_SPECIAL_MODE,
                          LOG_CHILD_WRITABLE, LOG_ROOT_FS,        LOG_CHILD_FS,
                          LOG_WRONG_TYPE,     LOG_READONLY};

    for (QseSize i = 0; i < sizeof(reject) / sizeof(*reject); ++i) {
        reset(reject[i]);
        CHECK(qsc_ensure() == 125 && !live_fds && !mount_calls && !writes && !rename_calls);
        reset(reject[i]);
        prefix_active = 0;
        objects[5].present = objects[6].present = 0;
        CHECK(qsc_initialize() == 125 && !live_fds && !mount_calls && !writes && !prefix_restores);
    }

    reset(NORMAL);
    objects[3].st.mode = 0040755;
    CHECK(qsc_ensure() == 0 && !live_fds && flags == original_flags && !prefix_active);
    puts("log mount: exact owned0775 or strict0755, qnx6/RW/distinct-fsid and pinned-child identity; unsafe variants rejected");

    return 0;
}

static int check_abort(void) {
    reset(NORMAL);
    CHECK(qsc_abort() == 0 && !prefix_active && prefix_unlinks == 1 && !mount_calls && !writes &&
          !live_fds && flags == original_flags);
    reset(REGULAR);
    Object before;
    memcpy(&before, &objects[5], sizeof(before));
    CHECK(qsc_abort() == 0 && !prefix_unlinks && !mount_calls && !writes && !live_fds &&
          !memcmp((const unsigned char *)&before, (const unsigned char *)&objects[5],
                  sizeof(before)));
    reset(MISSING);
    CHECK(qsc_abort() == 0 && !prefix_unlinks && !mount_calls && !live_fds);
    reset(FOREIGN_LINK);
    CHECK(qsc_abort() == 125 && prefix_active && !prefix_unlinks && !mount_calls && !live_fds);
    reset(ROOT_RW);
    CHECK(qsc_abort() == 125 && prefix_active && !prefix_unlinks && !mount_calls && !live_fds);
    reset(NORMAL);
    environment = "abort";

    if (!setjmp(jump)) {
        qse_supervisor_start();
    }

    CHECK(exit_status == 0 && !prefix_active && !live_fds);
    puts("abort: exact temporary prefix removed, regular/absent preserved, foreign prefix and RW root refused");

    return 0;
}

static int check_owned_mkdir_parent(void) {
    const int reject[] = {INIT_PARENT_REPLACED, INIT_PARENT_MODE_CHANGED, INIT_PARENT_GROUP_CHANGED,
                          INIT_PARENT_TWO_LINKS};

    for (QseSize i = 0; i < sizeof(reject) / sizeof(*reject); ++i) {
        reset(reject[i]);
        prefix_active = 0;
        objects[5].present = objects[6].present = objects[4].present = 0;
        CHECK(qsc_initialize() == 125 && ram_mkdirs == 1 && !prefix_active && !prefix_restores &&
              !live_fds && !mount_calls && !writes &&
              strstr(diagnostic, "initialize-parent-after-mkdir"));
    }

    reset(INIT_PARENT_STABLE);
    prefix_active = 0;
    objects[5].present = objects[6].present = objects[4].present = 0;
    CHECK(qsc_initialize() == 0 && ram_mkdirs == 1 && prefix_active && !live_fds && !mount_calls &&
          !writes);
    puts("owned mkdir: parent unchanged or one new directory link only; identity/mode/group/extra-link changes rejected");

    return 0;
}

int main(int argc, char **argv) {
    unsigned char *input = 0;

    if (argc == 2) {
        FILE *file = fopen(argv[1], "rb");

        if (!file || fseek(file, 0, SEEK_END)) {
            return 2;
        }

        long size = ftell(file);

        if (size <= 1 || (unsigned long)size > sizeof(objects[0].bytes) ||
            fseek(file, 0, SEEK_SET)) {
            return 2;
        }

        input = malloc((QseSize)size);

        if (!input || fread(input, 1, (QseSize)size, file) != (QseSize)size || fclose(file)) {
            return 2;
        }

        payload = input;
        payload_size = (QseSize)size;
    } else if (argc != 1) {
        return 2;
    }

    int rc = check_migration() || check_preserve() || check_failures() || check_initialize() ||
             check_log_mount() || check_abort() || check_owned_mkdir_parent();

    if (!rc) {
        printf("PASS: %u host scenarios with production helper; input=%lu bytes\n", scenarios_run,
               payload_size);
    }

    free(input);

    return rc ? 1 : 0;
}
