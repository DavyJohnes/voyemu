#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long QseSize;
typedef struct {
    unsigned mode, uid, nlink;
    long size;
} QseStat;
static unsigned char qst_original_pack[] = {2, 'O', 'E', 'M', 130};
#define QST_ORIGINAL_LENGTH 6
#define QST_PACKED_LENGTH sizeof(qst_original_pack)
static const unsigned char qst_launcher[] = {'L', '!'}, qst_shim[] = {'S', '!'};
static struct {
    const char *path;
    QseStat stat;
    unsigned char bytes[64];
} files[8];
static int count, error, writes, creates, links, redirect, fail_link, fail_write;

static int find(const char *p) {
    for (int i = 0; i < count; i++) {
        if (!strcmp(files[i].path, p)) {
            return i;
        }
    }

    return -1;
}

static void *qse_malloc(QseSize n) {
    return malloc(n);
}

static void qse_free(void *p) {
    free(p);
}

static int *qse_errno(void) {
    return &error;
}

static int qse_lstat(const char *p, QseStat *s) {
    int i = find(p);

    if (i < 0) {
        error = 2;

        return -1;
    }

    *s = files[i].stat;

    return 0;
}

static int qse_regular(const QseStat *s, QseSize n) {
    return (s->mode & 0170000) == 0100000 && !s->uid && !(s->mode & 0022) && s->size >= 0 &&
           (unsigned long)s->size <= n;
}

static int qse_load(const char *p, unsigned char *b, QseSize max, QseSize *n) {
    int i = find(p);

    if (i < 0 || files[i].stat.size > (long)max) {
        return -1;
    }

    *n = files[i].stat.size;
    memcpy(b, files[i].bytes, *n);

    return 0;
}

static int qse_open(const char *p, int flags, ...) {
    assert(flags == (1 | 0400 | 02000 | 010000 | 020000));
    assert(find(p) < 0);
    int i = count++;
    files[i].path = p;
    files[i].stat = (QseStat){0100500, 0, 1, 0};
    creates++;

    return i;
}

static int qse_fstat(int fd, QseStat *s) {
    *s = files[fd].stat;

    return 0;
}

static long qse_write(int fd, const void *p, QseSize n) {
    writes++;

    if (fail_write) {
        return -1;
    }

    assert(files[fd].stat.size + (long)n <= 64);
    memcpy(files[fd].bytes + files[fd].stat.size, p, n);
    files[fd].stat.size += n;

    return (long)n;
}

static int qse_fsync(int fd) {
    (void)fd;

    return 0;
}

static int qse_close(int fd) {
    (void)fd;

    return 0;
}

#define QST_TEST
#include "sysmgr_tool_install.c"

int qst_mkdir(const char *p, unsigned mode) {
    assert(mode == 0700 && find(p) < 0);
    int i = count++;
    files[i].path = p;
    files[i].stat = (QseStat){0040700, 0, 2, 0};

    return 0;
}

int qst_readlink(const char *p, char *b, QseSize n) {
    assert(!strcmp(p, "/usr/bin/sysmgrtool"));

    if (!redirect) {
        error = 22;

        return -1;
    }

    const char *t = redirect == 1 ? "/log/voyemu-sysmgr/sysmgrtool" : "/foreign/tool";
    assert(strlen(t) < n);
    /* NOLINTNEXTLINE(bugprone-not-null-terminated-result): readlink results carry no NUL. */
    memcpy(b, t, strlen(t));

    return (int)strlen(t);
}

int qst_namespace_link(const char *t, const char *p) {
    assert(!strcmp(t, "/log/voyemu-sysmgr/sysmgrtool") && !strcmp(p, "/usr/bin/sysmgrtool"));
    links++;

    if (fail_link) {
        error = 1;

        return -1;
    }

    redirect = 1;

    return 0;
}

static void reset(void) {
    memset(files, 0, sizeof files);
    count = 1;
    files[0].path = "/usr/bin/sysmgrtool";
    files[0].stat = (QseStat){0100755, 0, 1, 6};
    memcpy(files[0].bytes, "OEM\0\0\0", 6);
    error = writes = creates = links = redirect = fail_link = fail_write = 0;
    qst_original_pack[4] = 130;
}

int main(void) {
    reset();
    assert(qst_install() == 0 && creates == 3 && links == 1);
    int oldwrites = writes;
    assert(qst_install() == 0 && creates == 3 && links == 1 && writes == oldwrites);
    reset();
    files[0].bytes[0] = 'X';
    assert(qst_install() < 0 && !creates && !links);
    reset();
    files[0].stat.nlink = 2;
    assert(qst_install() < 0 && !creates && !links);
    reset();
    qst_original_pack[4] = 255;
    assert(qst_install() < 0 && !creates && !links);
    reset();
    redirect = 2;
    assert(qst_install() < 0 && !creates && !links);
    reset();
    fail_link = 1;
    assert(qst_install() < 0 && creates == 3 && links == 1 && !redirect);
    reset();
    fail_write = 1;
    assert(qst_install() < 0 && creates == 1 && !links);
    reset();
    assert(qst_install() == 0);
    files[find("/log/voyemu-sysmgr/wait.so")].stat.uid = 1;
    assert(qst_install() < 0 && links == 1);
    reset();
    assert(qst_install() == 0);
    files[find("/log/voyemu-sysmgr/sysmgrtool.oem")].bytes[0] = 'X';
    assert(qst_install() < 0 && links == 1);
    reset();
    assert(qst_install() == 0);
    files[find("/log/voyemu-sysmgr")].stat.mode = 0120700;
    assert(qst_install() < 0 && links == 1);

    for (unsigned mode = 0400; mode <= 0600; mode += 0200) {
        reset();
        assert(qst_install() == 0);
        files[find("/log/voyemu-sysmgr/sysmgrtool")].stat.mode = 0100000 | mode;
        assert(qst_install() < 0 && links == 1);
    }

    puts("installer exact original/assets, malformed pack, unknown links, partial writes, ownership and idempotence passed");
}
