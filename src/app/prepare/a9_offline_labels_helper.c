/* Preparation-only helper. Never use on a mounted or shared image. */
#include "e2fs_helper_file.h"
#include "e2fs_helper_sha256.h"
#include <ext2fs/ext2fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIMIT (16ULL << 30)
#define ATTR_LIMIT 64
#define ATTR_BYTES (1U << 20)
struct attr {
    char *name;
    unsigned char *value;
    size_t len;
};
struct attrs {
    struct attr values[ATTR_LIMIT];
    size_t count, bytes;
};
struct item {
    const char *path, *context, *digest;
    unsigned mode;
    ext2_ino_t ino;
    struct attrs attrs;
};

static _Noreturn void fail(const char *message) {
    fprintf(stderr, "A9_OFFLINE_LABELS: %s\n", message);
    exit(1);
}

static void check(errcode_t e, const char *message) {
    if (e) {
        fail(message);
    }
}

static uint64_t num(const unsigned char *p, unsigned n) {
    uint64_t v = 0;

    for (unsigned i = 0; i < n; i++) {
        v |= (uint64_t)p[i] << (8 * i);
    }

    return v;
}

static uint32_t crc(const unsigned char *p, size_t n) {
    uint32_t v = ~0U;

    while (n--) {
        v ^= *p++;

        for (unsigned i = 0; i < 8; i++) {
            v = (v >> 1) ^ (0xedb88320U & (uint32_t)-(int32_t)(v & 1));
        }
    }

    return ~v;
}

static void readat(HelperFile *fd, uint64_t pos, void *buf, size_t len) {
    if (!helper_read(fd, pos, buf, len)) {
        fail("short image read");
    }
}

static int valid_hash(const char *s) {
    if (strlen(s) != 64) {
        return 0;
    }

    for (; *s; s++) {
        if (!((*s >= '0' && *s <= '9') || (*s >= 'a' && *s <= 'f'))) {
            return 0;
        }
    }

    return 1;
}

static void finish_hash(Sha256 *ctx, char out[65]) {
    unsigned char digest[32];
    static const char hex[] = "0123456789abcdef";

    sha256_final(ctx, digest);

    for (unsigned i = 0; i < sizeof digest; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 15];
    }

    out[64] = 0;
}

static void file_hash(HelperFile *fd, uint64_t bytes, char out[65]) {
    unsigned char buf[65536];
    Sha256 c;

    sha256_init(&c);

    for (uint64_t at = 0; at < bytes;) {
        size_t n = bytes - at < sizeof buf ? (size_t)(bytes - at) : sizeof buf;
        readat(fd, at, buf, n);
        sha256_update(&c, buf, n);
        at += n;
    }

    finish_hash(&c, out);
}

static void outside_hash(HelperFile *fd, uint64_t size, uint64_t offset, uint64_t extent,
                         char out[65]) {
    unsigned char buf[65536];
    Sha256 c;

    sha256_init(&c);

    for (unsigned range = 0; range < 2; range++) {
        uint64_t at = range ? offset + extent : 0, end = range ? size : offset;

        while (at < end) {
            size_t n = end - at < sizeof buf ? (size_t)(end - at) : sizeof buf;
            readat(fd, at, buf, n);
            sha256_update(&c, buf, n);
            at += n;
        }
    }

    finish_hash(&c, out);
}

static uint64_t gpt(HelperFile *fd, uint64_t size, uint64_t *extent) {
    unsigned char h[512], test[512];

    readat(fd, 512, h, sizeof h);

    if (memcmp(h, "EFI PART", 8) || num(h + 8, 4) != 0x10000 || num(h + 12, 4) < 92 ||
        num(h + 12, 4) > 512 || num(h + 24, 8) != 1) {
        fail("GPT header invalid");
    }

    memcpy(test, h, sizeof h);
    memset(test + 16, 0, 4);

    if (crc(test, (size_t)num(h + 12, 4)) != num(h + 16, 4)) {
        fail("GPT header checksum");
    }

    uint64_t count = num(h + 80, 4), width = num(h + 84, 4), table = num(h + 72, 8),
             first = num(h + 40, 8), last = num(h + 48, 8);

    if (!count || count > 256 || width < 128 || width > 1024 || width % 8 || size % 512 ||
        table > size / 512 || count * width > size - table * 512 || first > last ||
        last >= size / 512 || num(h + 32, 8) >= size / 512) {
        fail("GPT bounds invalid");
    }

    size_t len = (size_t)(count * width);
    unsigned char *entries = malloc(len);

    if (!entries) {
        fail("allocation");
    }

    readat(fd, table * 512, entries, len);

    if (crc(entries, len) != num(h + 88, 4)) {
        fail("GPT table checksum");
    }

    uint64_t backup = num(h + 32, 8);

    if (backup != size / 512 - 1 || table < 2 || table * 512 + len > first * 512) {
        fail("GPT primary geometry");
    }

    unsigned char bh[512], bc[512];
    readat(fd, backup * 512, bh, sizeof bh);

    if (memcmp(bh, "EFI PART", 8) || num(bh + 8, 4) != 0x10000 ||
        num(bh + 12, 4) != num(h + 12, 4) || num(bh + 24, 8) != backup || num(bh + 32, 8) != 1 ||
        num(bh + 40, 8) != first || num(bh + 48, 8) != last || memcmp(bh + 56, h + 56, 16) ||
        num(bh + 80, 4) != count || num(bh + 84, 4) != width || num(bh + 88, 4) != num(h + 88, 4)) {
        fail("GPT backup header mismatch");
    }

    memcpy(bc, bh, sizeof bh);
    memset(bc + 16, 0, 4);

    if (crc(bc, (size_t)num(bh + 12, 4)) != num(bh + 16, 4)) {
        fail("GPT backup header checksum");
    }

    uint64_t bt = num(bh + 72, 8);

    if (bt <= last || bt >= backup || len > (backup - bt) * 512) {
        fail("GPT backup geometry");
    }

    unsigned char *be = malloc(len);

    if (!be) {
        fail("allocation");
    }

    readat(fd, bt * 512, be, len);

    if (crc(be, len) != num(bh + 88, 4) || memcmp(entries, be, len)) {
        fail("GPT backup table mismatch");
    }

    free(be);
    unsigned found = 0;
    uint64_t begin = 0, end = 0;
    unsigned char zero[16] = {0};

    for (size_t i = 0; i < count; i++) {
        unsigned char *e = entries + i * width;

        if (!memcmp(e, zero, 16)) {
            continue;
        }

        begin = num(e + 32, 8);
        end = num(e + 40, 8);

        if (++found != 1 || begin < first || end > last || begin > end || end >= size / 512) {
            fail("one bounded partition required");
        }
    }

    free(entries);

    if (found != 1 || begin * 512 < table * 512 + len || begin < 2) {
        fail("partition missing or overlaps GPT");
    }

    *extent = (end - begin + 1) * 512;

    if (*extent < 2048) {
        fail("partition too small");
    }

    return begin * 512;
}

static int save_attr(char *name, char *value, size_t len, void *arg) {
    struct attrs *a = arg;

    if (!strcmp(name, "security.selinux")) {
        return 0;
    }

    if (a->count == ATTR_LIMIT || len > ATTR_BYTES - a->bytes || strlen(name) > 255) {
        fail("xattr bounds");
    }

    struct attr *v = &a->values[a->count++];
    v->name = strdup(name);
    v->value = malloc(len ? len : 1);
    v->len = len;

    if (!v->name || !v->value) {
        fail("allocation");
    }

    memcpy(v->value, value, len);
    a->bytes += len;

    return 0;
}

static int compare_attr(char *name, char *value, size_t len, void *arg) {
    struct attrs *a = arg;

    if (!strcmp(name, "security.selinux")) {
        return 0;
    }

    for (size_t i = 0; i < a->count; i++) {
        if (a->values[i].name && !strcmp(name, a->values[i].name)) {
            if (a->values[i].len != len || memcmp(a->values[i].value, value, len)) {
                fail("other xattr changed");
            }

            free(a->values[i].name);
            a->values[i].name = NULL;

            return 0;
        }
    }

    fail("unexpected xattr");
}

static void attrs_free(struct attrs *a) {
    for (size_t i = 0; i < a->count; i++) {
        free(a->values[i].name);
        free(a->values[i].value);
    }
}

static ext2_ino_t lookup(ext2_filsys fs, const char *path) {
    ext2_ino_t cur = EXT2_ROOT_INO, next;
    const char *p = path + 1;

    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        struct ext2_inode dir;

        if (!len || len > 255) {
            fail("path component bounds");
        }

        check(ext2fs_read_inode(fs, cur, &dir), "parent inode read");

        if (!LINUX_S_ISDIR(dir.i_mode)) {
            fail("path parent not directory");
        }

        check(ext2fs_lookup(fs, cur, p, (int)len, NULL, &next), "target lookup");
        cur = next;

        if (!slash) {
            break;
        }

        p = slash + 1;
    }

    return cur;
}

static void inode_check(ext2_filsys fs, struct item *it) {
    struct ext2_inode in;

    check(ext2fs_read_inode(fs, it->ino, &in), "inode read");

    if (!LINUX_S_ISREG(in.i_mode) || (in.i_mode & 07777) != it->mode || inode_uid(in) ||
        inode_gid(in) || in.i_links_count != 1) {
        fail("target inode identity");
    }
}

static void content_check(ext2_filsys fs, struct item *it) {
    ext2_file_t file;
    __u64 size;
    unsigned char buf[65536];
    char out[65];
    Sha256 ctx;

    check(ext2fs_file_open(fs, it->ino, 0, &file), "target open");
    check(ext2fs_file_get_lsize(file, &size), "target size");

    if (!size || size > (256ULL << 20)) {
        fail("target size bounds");
    }

    sha256_init(&ctx);

    for (__u64 at = 0; at < size;) {
        unsigned got = 0, want = (unsigned)(size - at < sizeof buf ? size - at : sizeof buf);
        check(ext2fs_file_read(file, buf, want, &got), "target read");

        if (got != want) {
            fail("target short read");
        }

        sha256_update(&ctx, buf, got);
        at += got;
    }

    check(ext2fs_file_close(file), "target close");
    finish_hash(&ctx, out);

    if (strcmp(out, it->digest)) {
        fail("target hash mismatch");
    }
}

static void verify(ext2_filsys fs, struct item *it, int before) {
    struct ext2_xattr_handle *h = NULL;
    void *value = NULL;
    size_t len = 0;

    it->ino = lookup(fs, it->path);
    inode_check(fs, it);
    content_check(fs, it);
    check(ext2fs_xattrs_open(fs, it->ino, &h), "xattr open");
    check(ext2fs_xattrs_read(h), "xattr read");
    check(ext2fs_xattr_get(h, "security.selinux", &value, &len), "SELinux xattr missing");
    const char old[] = "u:object_r:system_file:s0";

    if (!((len == strlen(it->context) + 1 && !memcmp(value, it->context, len)) ||
          (before && len == sizeof old && !memcmp(value, old, len)))) {
        fail("unexpected SELinux xattr");
    }

    ext2fs_free_mem(&value);
    check(ext2fs_xattrs_iterate(h, before ? save_attr : compare_attr, &it->attrs), "xattr iterate");

    if (!before) {
        for (size_t i = 0; i < it->attrs.count; i++) {
            if (it->attrs.values[i].name) {
                fail("other xattr missing");
            }
        }
    }

    check(ext2fs_xattrs_close(&h), "xattr close");
}

static ext2_filsys open_fs(const char *file, uint64_t offset, uint64_t extent, int flags,
                           int require_clean) {
    ext2_filsys fs = NULL;
    char opt[64];

    snprintf(opt, sizeof opt, "offset=%llu", (unsigned long long)offset);
    check(ext2fs_open2(file, opt, flags, 0, 0, default_io_manager, &fs), "ext4 open");

    if ((fs->super->s_state & EXT2_ERROR_FS) ||
        (require_clean && !(fs->super->s_state & EXT2_VALID_FS)) ||
        (fs->super->s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER)) {
        fail("ext4 requires clean unmounted filesystem");
    }

    uint64_t blocks = ext2fs_blocks_count(fs->super);

    if (!blocks || !fs->blocksize || blocks > extent / fs->blocksize) {
        fail("ext4 exceeds partition");
    }

    return fs;
}

int main(int argc, char **argv) {
    helper_binary_stdout();
    const int inspect = argc == 9 && !strcmp(argv[8], "--check-before-fsck");

    if ((argc != 8 && !inspect) || !helper_absolute(argv[1]) || !valid_hash(argv[2]) ||
        !valid_hash(argv[3]) || !valid_hash(argv[4]) || !valid_hash(argv[5]) ||
        !valid_hash(argv[6]) || !valid_hash(argv[7])) {
        fail("expected absolute raw image and six SHA256 values");
    }

    HelperIdentity a, b;
    HelperFile file, *fd = &file;

    if (!helper_open(argv[1], NULL, fd, &a)) {
        fail("unaliased regular image required");
    }

    if (a.size < 2048 || a.size > LIMIT) {
        helper_close(fd);
        fail("unaliased regular image required");
    }

    char hash[65];
    file_hash(fd, a.size, hash);

    if (strcmp(hash, argv[2])) {
        fail("image hash mismatch");
    }

    uint64_t extent, offset = gpt(fd, a.size, &extent);
    char outside[65];
    outside_hash(fd, a.size, offset, extent, outside);
    helper_close(fd);
    struct item items[5] = {{"/system/bin/voyemu-early-inputs",
                             "u:object_r:can_early_inputs_exec:s0",
                             argv[3],
                             0755,
                             0,
                             {0}},
                            {"/system/bin/voyemu-can-model",
                             "u:object_r:can_simulator_exec:s0",
                             argv[4],
                             0755,
                             0,
                             {0}},
                            {"/system/etc/voyemu/can-model.fixture",
                             "u:object_r:can_simulator_fixture:s0",
                             argv[5],
                             0644,
                             0,
                             {0}},
                            {"/system/etc/voyemu/qg-settings-guest.db",
                             "u:object_r:can_early_input_file:s0",
                             argv[6],
                             0444,
                             0,
                             {0}},
                            {"/system/etc/voyemu/hybrid-nvram.img",
                             "u:object_r:can_early_input_file:s0",
                             argv[7],
                             0444,
                             0,
                             {0}}};

    ext2_filsys fs = open_fs(argv[1], offset, extent, 0, !inspect);

    for (unsigned i = 0; i < 5; i++) {
        verify(fs, &items[i], 1);
    }

    for (unsigned i = 0; i < 5; i++) {
        for (unsigned j = 0; j < i; j++) {
            if (items[i].ino == items[j].ino) {
                fail("target inode alias");
            }
        }
    }

    check(ext2fs_close(fs), "read close");

    if (inspect) {
        for (unsigned i = 0; i < 5; i++) {
            attrs_free(&items[i].attrs);
        }

        printf("offset=%llu bytes=%llu outside_sha256=%s\n", (unsigned long long)offset,
               (unsigned long long)extent, outside);

        return 0;
    }

    if (!helper_open(argv[1], &a, fd, &b)) {
        fail("image changed before write");
    }

    file_hash(fd, b.size, hash);
    helper_close(fd);

    if (strcmp(hash, argv[2])) {
        fail("image changed before write");
    }

    fs = open_fs(argv[1], offset, extent, EXT2_FLAG_RW, 1);
    check(ext2fs_read_bitmaps(fs), "allocation bitmap read");

    for (unsigned i = 0; i < 5; i++) {
        if (lookup(fs, items[i].path) != items[i].ino) {
            fail("target identity changed");
        }

        struct ext2_xattr_handle *h = NULL;
        check(ext2fs_xattrs_open(fs, items[i].ino, &h), "xattr open write");
        check(ext2fs_xattrs_read(h), "xattr read write");
        check(ext2fs_xattr_set(h, "security.selinux", items[i].context,
                               strlen(items[i].context) + 1),
              "xattr set");
        check(ext2fs_xattrs_write(h), "xattr write");
        check(ext2fs_xattrs_close(&h), "xattr close write");
    }

    check(ext2fs_close(fs), "write close");
    fs = open_fs(argv[1], offset, extent, 0, 1);

    for (unsigned i = 0; i < 5; i++) {
        ext2_ino_t expected = items[i].ino;
        verify(fs, &items[i], 0);

        if (items[i].ino != expected) {
            fail("target inode changed");
        }

        attrs_free(&items[i].attrs);
    }

    check(ext2fs_close(fs), "verification close");

    if (!helper_open(argv[1], &a, fd, &b)) {
        fail("output image identity");
    }

    char outside_after[65];
    outside_hash(fd, b.size, offset, extent, outside_after);

    if (strcmp(outside, outside_after)) {
        fail("outside partition changed");
    }

    uint64_t verify_extent;

    if (gpt(fd, b.size, &verify_extent) != offset || verify_extent != extent) {
        fail("GPT changed");
    }

    helper_close(fd);
    printf("offset=%llu bytes=%llu\n", (unsigned long long)offset, (unsigned long long)extent);

    return 0;
}
