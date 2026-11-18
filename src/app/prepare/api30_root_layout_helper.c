#include "e2fs_helper_file.h"
#include "e2fs_helper_sha256.h"
#include <errno.h>
#include <ext2fs/ext2fs.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void digest_ranges(HelperFile *fd, uint64_t size, uint64_t off, uint64_t len, int outside,
                          unsigned char out[32]) {
    Sha256 c;

    sha256_init(&c);
    unsigned char buf[65536];

    for (int i = 0; i < (outside ? 2 : 1); i++) {
        uint64_t at = outside ? (i ? off + len : 0) : 0, end = outside ? (i ? size : off) : size;

        while (at < end) {
            size_t n = end - at < sizeof buf ? (size_t)(end - at) : sizeof buf;

            if (!helper_read(fd, at, buf, n)) {
                fprintf(stderr, "short read\n");
                exit(1);
            }

            sha256_update(&c, buf, n);
            at += n;
        }
    }

    sha256_final(&c, out);
}

static int pin_ok(const char *s, const unsigned char *d) {
    if (strlen(s) != 64) {
        return 0;
    }

    char out[65];

    for (int i = 0; i < 32; i++) {
        sprintf(out + 2 * i, "%02x", d[i]);
    }

    return !strcmp(s, out);
}

static void ck(errcode_t e, const char *s) {
    if (e) {
        fprintf(stderr, "%s: %ld\n", s, (long)e);
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 5 || !helper_absolute(argv[1])) {
        fprintf(stderr, "usage: image sha256 offset extent\n");

        return 1;
    }

    char *end;

    for (int i = 3; i < 5; i++) {
        if (!argv[i][0] || (argv[i][0] == '0' && argv[i][1])) {
            return 1;
        }

        for (const char *p = argv[i]; *p; p++) {
            if (*p < '0' || *p > '9') {
                return 1;
            }
        }
    }

    errno = 0;
    unsigned long long offset = strtoull(argv[3], &end, 10);

    if (errno || *end) {
        return 1;
    }

    errno = 0;
    unsigned long long extent = strtoull(argv[4], &end, 10);

    if (errno || *end || !extent || offset % 4096 || extent % 4096) {
        return 1;
    }

    HelperIdentity st, checkst;
    HelperFile file, *fd = &file;

    if (!helper_open(argv[1], NULL, fd, &st)) {
        return 1;
    }

    if (offset > st.size || extent > st.size - offset || !helper_canonical(fd, argv[1])) {
        helper_close(fd);

        return 1;
    }

    unsigned char digest[32], outside[32], after[32];
    digest_ranges(fd, st.size, offset, extent, 0, digest);

    if (!pin_ok(argv[2], digest)) {
        return 1;
    }

    digest_ranges(fd, st.size, offset, extent, 1, outside);
    helper_close(fd);
    char opt[64];
    snprintf(opt, sizeof opt, "offset=%llu", offset);
    ext2_filsys fs;
    ck(ext2fs_open2(argv[1], opt, 0, 0, 0, default_io_manager, &fs), "open");

    if ((fs->super->s_state & EXT2_ERROR_FS) || !(fs->super->s_state & EXT2_VALID_FS) ||
        (fs->super->s_feature_incompat & EXT3_FEATURE_INCOMPAT_RECOVER) ||
        ext2fs_blocks_count(fs->super) > extent / fs->blocksize) {
        return 1;
    }

    ext2_ino_t found;
    errcode_t lookup = ext2fs_lookup(fs, EXT2_ROOT_INO, "private", 7, NULL, &found);

    if (lookup != EXT2_ET_FILE_NOT_FOUND) {
        fprintf(stderr, "private exists or lookup failed\n");

        return 1;
    }

    ck(ext2fs_close(fs), "preflight close");

    if (!helper_open(argv[1], &st, fd, &checkst)) {
        return 1;
    }

    digest_ranges(fd, st.size, offset, extent, 0, digest);
    helper_close(fd);

    if (!pin_ok(argv[2], digest)) {
        return 1;
    }

    ck(ext2fs_open2(argv[1], opt, EXT2_FLAG_RW, 0, 0, default_io_manager, &fs), "write open");
    ck(ext2fs_read_bitmaps(fs), "bitmaps");
    const char *names[] = {"private", "configs", "token"};
    const char label[] = "u:object_r:can_early_token_dir:s0";
    ext2_ino_t parent = EXT2_ROOT_INO;

    for (int i = 0; i < 3; i++) {
        ck(ext2fs_mkdir(fs, parent, 0, names[i]), "mkdir");
        ext2_ino_t ino;
        ck(ext2fs_lookup(fs, parent, names[i], strlen(names[i]), NULL, &ino), "lookup");
        struct ext2_inode in;
        ck(ext2fs_read_inode(fs, ino, &in), "read inode");
        in.i_mode = LINUX_S_IFDIR | 0755;
        in.i_uid = 0;
        in.i_gid = 1000;
        ck(ext2fs_write_inode(fs, ino, &in), "write inode");
        struct ext2_xattr_handle *h;
        ck(ext2fs_xattrs_open(fs, ino, &h), "xattr open");
        ck(ext2fs_xattrs_read(h), "xattr read");
        ck(ext2fs_xattr_set(h, "security.selinux", label, sizeof label), "xattr set");
        ck(ext2fs_xattrs_write(h), "xattr write");
        ck(ext2fs_xattrs_close(&h), "xattr close");
        parent = ino;
    }

    ck(ext2fs_close(fs), "close");
    ck(ext2fs_open2(argv[1], opt, 0, 0, 0, default_io_manager, &fs), "verify open");
    parent = EXT2_ROOT_INO;

    for (int i = 0; i < 3; i++) {
        ext2_ino_t ino;
        ck(ext2fs_lookup(fs, parent, names[i], strlen(names[i]), NULL, &ino), "verify lookup");
        struct ext2_inode in;
        ck(ext2fs_read_inode(fs, ino, &in), "verify inode");

        if (!LINUX_S_ISDIR(in.i_mode) || (in.i_mode & 07777) != 0755 || inode_uid(in) != 0 ||
            inode_gid(in) != 1000) {
            fprintf(stderr, "verify ownership or mode\n");

            return 1;
        }

        struct ext2_xattr_handle *h;
        void *value = NULL;
        size_t n = 0;
        ck(ext2fs_xattrs_open(fs, ino, &h), "verify xattr open");
        ck(ext2fs_xattrs_read(h), "verify xattr read");
        ck(ext2fs_xattr_get(h, "security.selinux", &value, &n), "verify xattr get");

        if (n != sizeof label || memcmp(value, label, n)) {
            fprintf(stderr, "verify xattr differs\n");

            return 1;
        }

        ext2fs_free_mem(&value);
        ck(ext2fs_xattrs_close(&h), "verify xattr close");
        parent = ino;
    }

    ck(ext2fs_close(fs), "verify close");

    if (!helper_open(argv[1], &st, fd, &checkst)) {
        return 1;
    }

    digest_ranges(fd, st.size, offset, extent, 1, after);
    helper_close(fd);

    if (memcmp(outside, after, 32)) {
        fprintf(stderr, "outside extent changed\n");

        return 1;
    }

    return 0;
}
