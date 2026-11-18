/* Fixed-path migration of the emulator's legacy RAM supervisor config.
 * Existing regular configs, including installed extensions, are never rewritten.
 * Explicit 64-bit ABI fields also keep host mocks correct on Windows LLP64.
 * QNX ABI: libc 0.2.189 unix/nto statvfs, fcntl, mount and mount_parse_generic_args.
 * This helper runs before OEM services. It never generates supervisor XML. */
#ifdef QSC_TEST
#define QSC_API(name)
#else
#define QSC_API(name) __asm__(#name)
#endif
typedef struct {
    QseU64 bsize, frsize;
    QseU64 blocks, bfree, bavail, files, ffree, favail;
    QseU64 fsid;
    char basetype[16];
    QseU64 flag, namemax;
    QseU32 filler[21];
} QscStatvfs;
_Static_assert(sizeof(QscStatvfs) == 192 && __builtin_offsetof(QscStatvfs, fsid) == 64 &&
                       __builtin_offsetof(QscStatvfs, basetype) == 72 &&
                       __builtin_offsetof(QscStatvfs, flag) == 88,
               "QNX supervisor statvfs ABI");
extern long qsc_readlink(const char *, char *, QseSize) QSC_API(readlink);
extern int qsc_mkdir(const char *, unsigned) QSC_API(mkdir);
extern int qsc_fchmod(int, unsigned) QSC_API(fchmod);
extern int qsc_rename(const char *, const char *) QSC_API(rename);
extern int qsc_unlink(const char *) QSC_API(unlink);
extern int qsc_pathmgr_unlink(const char *) QSC_API(pathmgr_unlink);
extern int qsc_pathmgr_symlink(const char *, const char *) QSC_API(pathmgr_symlink);
extern int qsc_getpid(void) QSC_API(getpid);
extern unsigned qsc_geteuid(void) QSC_API(geteuid);
extern int qsc_fstatvfs(int, QscStatvfs *) QSC_API(fstatvfs);
extern char *qsc_mount_parse_generic_args(char *, int *) QSC_API(mount_parse_generic_args);
extern int qsc_mount(const char *, const char *, int, const char *, const void *, int)
        QSC_API(mount);
#undef QSC_API
/* Values are the QNX fcntl/statvfs definitions, not host constants. */
enum {
    QSC_READ = 010000 | 020000,
    QSC_DIRECTORY = 04000000,
    QSC_CREATE = 1 | 0400 | 02000 | 010000 | 020000,
    QSC_RDONLY = 1,
    QSC_MAX_CONFIG = 1048576
};
static const char qsc_target[] = "/etc/clsboot/boot_cfg.xml";
static const char qsc_legacy[] = "/log/clsboot/boot_cfg.xml";
static const char *const qsc_paths[] = {"/", "/etc", "/etc/clsboot", "/log", "/log/clsboot"};
typedef struct {
    int fd;
    QseStat stat;
} QscDirectory;
typedef struct {
    QscDirectory dirs[5];
    unsigned count;
    QseU64 fsid, flags, log_fsid, log_flags;
} QscRoot;

static int qsc_bad(void) {
    *qse_errno() = 22;

    return -1;
}

static int qsc_string(const char *a, const char *b) {
    QseSize i = 0;

    while (a[i] && a[i] == b[i]) {
        ++i;
    }

    return a[i] == b[i];
}

static int qsc_directory_identity(const QseStat *a, const QseStat *b) {
    return a->ino == b->ino && a->dev == b->dev && a->rdev == b->rdev && a->uid == b->uid &&
           a->gid == b->gid && a->mode == b->mode && a->nlink == b->nlink;
}

/* OEM root and owned /log mount root are0775. Their mounts are verified before
 * mutation; intermediate config parents remain non-writable by group/other. */
static int qsc_directory(const QseStat *s, unsigned index) {
    return (s->mode & 0170000) == 0040000 && s->uid == 0 &&
           (!(s->mode & 0022) ||
            ((index == 0 || index == 3) && s->gid == 0 && (s->mode & 07777) == 0775)) &&
           s->nlink > 0;
}

static int qsc_file(const QseStat *s, int legacy) {
    return (s->mode & 0170000) == 0100000 && s->uid == 0 && s->nlink == 1 && s->size > 0 &&
           (QseU64)s->size <= QSC_MAX_CONFIG && !(s->mode & 07000) &&
           (!(s->mode & 0022) || (legacy && (s->mode & 0777) == 0666));
}

static int qsc_filesystem(int fd, QscStatvfs *fs) {
    if (qsc_fstatvfs(fd, fs)) {
        return -1;
    }

    static const char name[] = "qnx6";

    for (QseSize i = 0; i < sizeof(name); ++i) {
        if (fs->basetype[i] != name[i]) {
            return qsc_bad();
        }
    }

    return 0;
}

static int qsc_file_mount(int fd, const QscDirectory *parent) {
    QscStatvfs file, owner;

    if (qsc_filesystem(fd, &file) || qsc_filesystem(parent->fd, &owner)) {
        return -1;
    }

    return file.fsid == owner.fsid && file.flag == owner.flag ? 0 : qsc_bad();
}

static void qsc_root_metadata(const QseStat *stat, const QscStatvfs *fs, unsigned index) {
    char line[192];
    int saved = *qse_errno();
    int n = stat ? qse_snprintf(
                           line, sizeof(line),
                           "QSTORE SUPERVISOR directory=%u mode=%o uid=%u gid=%u dev=%u nlink=%u\n",
                           index, stat->mode, stat->uid, stat->gid, stat->dev, stat->nlink)
                 : qse_snprintf(
                           line, sizeof(line),
                           "QSTORE SUPERVISOR mount=%u flags=0x%llx basetype=%.16s fsid=0x%llx\n",
                           index, fs->flag, fs->basetype, fs->fsid);

    if (n > 0 && (QseSize)n < sizeof(line)) {
        qse_write(2, line, (QseSize)n);
    }

    *qse_errno() = saved;
}

static void qsc_pin_failure(unsigned index, const char *step) {
    char line[128];
    int saved = *qse_errno();
    int n = qse_snprintf(line, sizeof(line), "QSTORE SUPERVISOR pin=%u step=%s errno=%d\n", index,
                         step, saved);

    if (n > 0 && (QseSize)n < sizeof(line)) {
        qse_write(2, line, (QseSize)n);
    }

    *qse_errno() = saved;
}

static int qsc_pin(QscRoot *root, unsigned index) {
    QseStat before, opened;

    if (qse_lstat(qsc_paths[index], &before)) {
        qsc_pin_failure(index, "lstat");

        return -1;
    }

    qsc_root_metadata(&before, 0, index);

    if (!qsc_directory(&before, index)) {
        qsc_bad();
        qsc_pin_failure(index, "directory");

        return -1;
    }

    int fd = qse_open(qsc_paths[index], QSC_READ | QSC_DIRECTORY);

    if (fd < 0) {
        qsc_pin_failure(index, "open");

        return -1;
    }

    root->dirs[index].fd = fd;
    root->count = index + 1;

    if (qse_fstat(fd, &opened)) {
        qsc_pin_failure(index, "fstat");

        return -1;
    }

    if (!qse_identity(&before, &opened)) {
        qsc_bad();
        qsc_pin_failure(index, "identity");

        return -1;
    }

    root->dirs[index].stat = opened;

    if (index >= 3) {
        QscStatvfs fs = {0};
        int result = qsc_filesystem(fd, &fs);
        qsc_root_metadata(0, &fs, index);

        if (result) {
            return -1;
        }

        if (index == 3) {
            root->log_fsid = fs.fsid;
            root->log_flags = fs.flag;
        }
    }

    return 0;
}

static int qsc_pins(const QscRoot *root) {
    for (unsigned i = 0; i < root->count; ++i) {
        QseStat path, opened;

        if (qse_lstat(qsc_paths[i], &path) || qse_fstat(root->dirs[i].fd, &opened)) {
            return -1;
        }

        if (!qsc_directory_identity(&root->dirs[i].stat, &path) ||
            !qsc_directory_identity(&root->dirs[i].stat, &opened)) {
            qsc_root_metadata(&path, 0, i);
            qsc_bad();
            qsc_pin_failure(i, "directory-recheck");

            return -1;
        }

        if (i < 3 && path.dev != root->dirs[0].stat.dev) {
            return qsc_bad();
        }

        if (i == 4 && path.dev != root->dirs[3].stat.dev) {
            return qsc_bad();
        }

        if (i >= 3) {
            QscStatvfs fs;

            if (qsc_filesystem(root->dirs[i].fd, &fs)) {
                return -1;
            }

            if (fs.fsid != root->log_fsid || fs.flag != root->log_flags || fs.fsid == root->fsid ||
                (fs.flag & QSC_RDONLY) || path.dev == root->dirs[0].stat.dev) {
                qsc_bad();
                qsc_pin_failure(i, "filesystem");

                return -1;
            }
        }
    }

    return 0;
}

static int qsc_flags(const QscRoot *root, QseU64 flags) {
    if (qsc_pins(root)) {
        return -1;
    }

    for (unsigned i = 0; i < 3; ++i) {
        QscStatvfs fs;

        if (qsc_filesystem(root->dirs[i].fd, &fs)) {
            return -1;
        }

        if (fs.fsid != root->fsid || fs.flag != flags) {
            return qsc_bad();
        }
    }

    return 0;
}

/* Restoration depends only on the pinned root, so a changed config/parent cannot
 * prevent a necessary attempt to restore the original filesystem flags. */
static int qsc_remount(const QscRoot *root, QseU64 requested) {
    QseStat path, opened;
    QscStatvfs fs;

    if (qse_lstat("/", &path) || qse_fstat(root->dirs[0].fd, &opened) ||
        qsc_filesystem(root->dirs[0].fd, &fs)) {
        return -1;
    }

    if (!qsc_directory_identity(&root->dirs[0].stat, &path) ||
        !qsc_directory_identity(&root->dirs[0].stat, &opened) || fs.fsid != root->fsid ||
        requested > 0x7fffffffUL) {
        return qsc_bad();
    }

    int flags = (int)requested;
    char option[] = "remount";

    if (qsc_mount_parse_generic_args(option, &flags) || flags < 0) {
        return qsc_bad();
    }

    QseU64 extra = (QseU64)flags & ~requested;

    if (((QseU64)flags & requested) != requested || !extra || (extra & (extra - 1))) {
        return qsc_bad();
    }

    return qsc_mount(0, "/", flags, "qnx6", 0, -1);
}

static int qsc_link(const QseStat *expected) {
    QseStat observed;
    char target[sizeof(qsc_legacy)];

    if (qse_lstat(qsc_target, &observed)) {
        return -1;
    }

    if ((observed.mode & 0170000) != 0120000 || observed.uid != 0 || observed.nlink != 1 ||
        (expected && !qse_identity(expected, &observed))) {
        return qsc_bad();
    }

    long n = qsc_readlink(qsc_target, target, sizeof(target));

    if (n < 0) {
        return -1;
    }

    if (n != (long)sizeof(qsc_legacy) - 1 ||
        !qse_equal((const unsigned char *)target, (const unsigned char *)qsc_legacy,
                   sizeof(qsc_legacy) - 1)) {
        return qsc_bad();
    }

    return 0;
}

/* A second complete read must reproduce the first snapshot and metadata.
 * Only the exact emulator legacy path may accept its historical 0666 mode. */
static int qsc_read_file(const char *path, int legacy, unsigned char *bytes, QseSize *length,
                         const QseStat *expected, int compare, const QscDirectory *parent) {
    QseStat before, opened, after;
    int fd = -1, rc = -1;
    QseSize used = 0;
    unsigned char chunk[4096];

    if (qse_lstat(path, &before)) {
        return -1;
    }

    if (!qsc_file(&before, legacy) || before.dev != parent->stat.dev ||
        (expected && !qse_identity(expected, &before))) {
        return qsc_bad();
    }

    fd = qse_open(path, QSC_READ);

    if (fd < 0) {
        return -1;
    }

    if (qse_fstat(fd, &opened)) {
        goto done;
    }

    if (!qse_identity(&before, &opened)) {
        qsc_bad();
        goto done;
    }

    if (qsc_file_mount(fd, parent)) {
        goto done;
    }

    for (;;) {
        long n = qse_read(fd, chunk, sizeof(chunk));

        if (n < 0 && *qse_errno() == 4) {
            continue;
        }

        if (n < 0) {
            goto done;
        }

        if (!n) {
            break;
        }

        if ((QseSize)n > (QseSize)before.size - used) {
            qsc_bad();
            goto done;
        }

        if (compare) {
            if (!qse_equal(bytes + used, chunk, (QseSize)n)) {
                qsc_bad();
                goto done;
            }
        } else {
            for (QseSize i = 0; i < (QseSize)n; ++i) {
                bytes[used + i] = chunk[i];
            }
        }

        used += (QseSize)n;
    }

    if (used != (QseSize)before.size) {
        qsc_bad();
        goto done;
    }

    if (qse_fstat(fd, &after)) {
        goto done;
    }

    if (!qse_identity(&before, &after)) {
        qsc_bad();
        goto done;
    }

    if (qse_lstat(path, &after)) {
        goto done;
    }

    if (!qse_identity(&before, &after)) {
        qsc_bad();
        goto done;
    }

    if (qsc_file_mount(fd, parent)) {
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

static int qsc_matches(const char *path, const unsigned char *bytes, QseSize length,
                       const QseStat *expected, const QscDirectory *parent) {
    QseStat before;

    if (qse_lstat(path, &before)) {
        return -1;
    }

    if (!qsc_file(&before, 0) || (before.mode & 0777) != 0600 || (QseSize)before.size != length ||
        !qsc_directory_identity(expected, &before) ||
        expected->mtim.seconds != before.mtim.seconds ||
        expected->mtim.nanoseconds != before.mtim.nanoseconds) {
        return qsc_bad();
    }

    QseSize read = 0;

    return qsc_read_file(path, 0, (unsigned char *)bytes, &read, &before, 1, parent) ||
                           read != length
                   ? qsc_bad()
                   : 0;
}

static void qsc_diagnostic(const char *step, int error) {
    char line[192];
    int n = qse_snprintf(line, sizeof(line), "QSTORE FAIL supervisor-config step=%s errno=%d\n",
                         step, error);

    if (n > 0 && (QseSize)n < sizeof(line)) {
        qse_write(2, line, (QseSize)n);
    }
}

static int qsc_ensure(void) {
    QscRoot root = {0};
    QscStatvfs fs = {0};
    QseStat original, source, created = {0}, path;
    unsigned char *bytes = 0;
    QseSize length = 0, at = 0;
    int fd = -1, rc = 125, error = 0, restore = 0, temp_owned = 0, published = 0,
        prefix_removed = 0, durable = 0;
    const char *step = "authority";
    char temp[128] = {0};

    if (qsc_geteuid() != 0) {
        goto done;
    }

    step = "root-open";

    for (unsigned i = 0; i < 3; ++i) {
        if (qsc_pin(&root, i)) {
            goto failed;
        }
    }

    int fs_result = qsc_filesystem(root.dirs[0].fd, &fs);
    qsc_root_metadata(0, &fs, 0);

    if (fs_result) {
        goto failed;
    }

    root.fsid = fs.fsid;
    root.flags = fs.flag;
    step = "root-flags";

    if (!(root.flags & QSC_RDONLY)) {
        qsc_bad();
        goto failed;
    }

    if (qsc_flags(&root, root.flags)) {
        goto failed;
    }

    step = "target-stat";

    if (qse_lstat(qsc_target, &original)) {
        if (*qse_errno() == 2 && qsc_flags(&root, root.flags) == 0) {
            rc = 2;
            goto done;
        }

        goto failed;
    }

    if (qsc_file(&original, 0)) {
        step = "retained-config";
        fd = qse_open(qsc_target, QSC_READ);

        if (fd < 0 || qse_fstat(fd, &path)) {
            goto failed;
        }

        if (original.dev != root.dirs[0].stat.dev || !qse_identity(&original, &path)) {
            qsc_bad();
            goto failed;
        }

        if (qsc_file_mount(fd, &root.dirs[0]) || qse_lstat(qsc_target, &path)) {
            goto failed;
        }

        if (!qse_identity(&original, &path)) {
            qsc_bad();
            goto failed;
        }

        if (qsc_flags(&root, root.flags)) {
            goto failed;
        }

        rc = 0;
        goto done;
    }

    step = "legacy-link";

    if (qsc_link(&original)) {
        goto failed;
    }

    step = "legacy-directories";

    for (unsigned i = 3; i < 5; ++i) {
        if (qsc_pin(&root, i)) {
            goto failed;
        }
    }

    if (qsc_pins(&root)) {
        goto failed;
    }

    step = "legacy-stat";

    if (qse_lstat(qsc_legacy, &source)) {
        goto failed;
    }

    if (!qsc_file(&source, 1) || source.dev != root.dirs[3].stat.dev) {
        qsc_bad();
        goto failed;
    }

    bytes = qse_malloc((QseSize)source.size);

    if (!bytes) {
        step = "allocate";
        *qse_errno() = 12;
        goto failed;
    }

    step = "legacy-snapshot";

    if (qsc_read_file(qsc_legacy, 1, bytes, &length, &source, 0, &root.dirs[3]) ||
        length != (QseSize)source.size || qse_lstat(qsc_legacy, &path) ||
        !qse_identity(&source, &path)) {
        goto failed;
    }

    step = "legacy-snapshot-recheck";

    if (qsc_read_file(qsc_legacy, 1, bytes, &length, &source, 1, &root.dirs[3]) ||
        qsc_link(&original) || qsc_flags(&root, root.flags)) {
        goto failed;
    }

    step = "temporary-name";
    int pid = qsc_getpid();

    if (pid <= 0) {
        qsc_bad();
        goto failed;
    }

    /* Even a failed remount may have changed the mount; always attempt restore. */
    restore = 1;
    step = "remount-rw";

    if (qsc_remount(&root, root.flags & ~(QseU64)QSC_RDONLY) ||
        qsc_flags(&root, root.flags & ~(QseU64)QSC_RDONLY)) {
        goto failed;
    }

    step = "temporary-create";

    /* A prior interrupted boot may leave its exclusive temp. Never delete it or
     * overwrite a collision; a bounded suffix permits a fresh transaction. */
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        int n = qse_snprintf(temp, sizeof(temp), "/etc/clsboot/.voyemu-supervisor-%d-%u", pid,
                             attempt);

        if (n <= 0 || (QseSize)n >= sizeof(temp)) {
            qsc_bad();
            goto failed;
        }

        fd = qse_open(temp, QSC_CREATE, 0600);

        if (fd >= 0) {
            break;
        }

        if (*qse_errno() != 17) {
            goto failed;
        }
    }

    if (fd < 0) {
        goto failed;
    }

    if (qse_fstat(fd, &created)) {
        goto failed;
    }

    if ((created.mode & 0170000) != 0100000 || created.uid != 0 || created.nlink != 1 ||
        created.size || created.dev != root.dirs[0].stat.dev) {
        qsc_bad();
        goto failed;
    }

    temp_owned = 1;

    if (qsc_file_mount(fd, &root.dirs[0])) {
        goto failed;
    }

    step = "temporary-mode";

    if (qsc_fchmod(fd, 0600) || qse_fstat(fd, &created) || (created.mode & 07777) != 0600) {
        goto failed;
    }

    step = "temporary-write";

    while (at < length) {
        long n = qse_write(fd, bytes + at, length - at);

        if (n < 0 && *qse_errno() == 4) {
            continue;
        }

        if (n <= 0) {
            goto failed;
        }

        at += (QseSize)n;
    }

    step = "temporary-sync";

    if (qse_fsync(fd) || qse_fstat(fd, &created) || !qsc_file(&created, 0) ||
        (created.mode & 0777) != 0600 || (QseSize)created.size != length ||
        qse_lstat(temp, &path) || !qse_identity(&created, &path)) {
        goto failed;
    }

    step = "temporary-close";

    if (qse_close(fd)) {
        fd = -1;
        goto failed;
    }

    fd = -1;
    step = "publish-recheck";

    if (qsc_read_file(qsc_legacy, 1, bytes, &length, &source, 1, &root.dirs[3]) ||
        qsc_flags(&root, root.flags & ~(QseU64)QSC_RDONLY) || qsc_link(&original) ||
        qse_lstat(temp, &path) || !qse_identity(&created, &path)) {
        goto failed;
    }

    /* ln -P installs a process-manager prefix, not a filesystem symlink.
     * Remove only that exact emulator prefix; never overwrite a hidden config. */
    step = "prefix-remove";

    if (qsc_pathmgr_unlink(qsc_target)) {
        goto failed;
    }

    prefix_removed = 1;
    step = "underlying-config";

    if (!qse_lstat(qsc_target, &path)) {
        QseSize retained = 0;

        if (path.dev != root.dirs[0].stat.dev || !qsc_file(&path, 0) ||
            (QseSize)path.size != length) {
            qsc_bad();
            goto failed;
        }

        if (qsc_read_file(qsc_target, 0, bytes, &retained, &path, 1, &root.dirs[0])) {
            goto failed;
        }

        if (retained != length) {
            qsc_bad();
            goto failed;
        }

        step = "underlying-recheck";

        if (qsc_flags(&root, root.flags & ~(QseU64)QSC_RDONLY) ||
            qse_lstat(qsc_target, &original) || !qse_identity(&path, &original)) {
            goto failed;
        }

        /* The same persistent bytes already exist. Keep their inode and mode. */
        durable = 1;
        rc = 0;
        goto done;
    }

    if (*qse_errno() != 2) {
        goto failed;
    }

    step = "publish-absence";

    if (qsc_flags(&root, root.flags & ~(QseU64)QSC_RDONLY) || qse_lstat(temp, &path) ||
        !qse_identity(&created, &path)) {
        goto failed;
    }

    if (!qse_lstat(qsc_target, &path) || *qse_errno() != 2) {
        qsc_bad();
        goto failed;
    }

    step = "publish";

    if (qsc_rename(temp, qsc_target)) {
        goto failed;
    }

    published = 1;
    temp_owned = 0;
    step = "parent-sync";

    if (qse_fsync(root.dirs[2].fd)) {
        goto failed;
    }

    step = "published-readback";

    if (qsc_matches(qsc_target, bytes, length, &created, &root.dirs[0]) ||
        qsc_flags(&root, root.flags & ~(QseU64)QSC_RDONLY)) {
        goto failed;
    }

    durable = 1;
    rc = 0;
    goto done;
failed:
    error = *qse_errno();
done:

    if (fd >= 0 && qse_close(fd)) {
        if (rc == 0) {
            step = "close";
            error = *qse_errno();
        }

        rc = 125;
    }

    if (temp_owned && !published) {
        QseStat owned;

        if (qsc_pins(&root) || qse_lstat(temp, &owned) ||
            !qsc_directory_identity(&created, &owned) || qsc_unlink(temp) ||
            qse_fsync(root.dirs[2].fd)) {
            step = "temporary-cleanup";
            error = *qse_errno();
            rc = 125;
        }
    }

    if (prefix_removed && !durable) {
        if (qsc_pathmgr_symlink(qsc_legacy, qsc_target) || qsc_link(0)) {
            step = "prefix-restore";
            error = *qse_errno();
            rc = 125;
        }
    }

    if (restore) {
        if (qsc_remount(&root, root.flags) || qsc_flags(&root, root.flags)) {
            step = "restore-flags";
            error = *qse_errno();
            rc = 125;
        }
    }

    if (rc == 0) {
        if (qse_lstat(qsc_target, &path) || !qsc_file(&path, 0) ||
            path.dev != root.dirs[0].stat.dev || qsc_flags(&root, root.flags)) {
            step = "final-regular";
            error = *qse_errno();
            rc = 125;
        } else {
            char line[160];
            int n = qse_snprintf(
                    line, sizeof(line),
                    "QSTORE SUPERVISOR ready regular=1 mode=%o uid=%u gid=%u nlink=%u root_ro=1 prefix=0\n",
                    path.mode, path.uid, path.gid, path.nlink);

            if (n > 0 && (QseSize)n < sizeof(line)) {
                qse_write(2, line, (QseSize)n);
            }
        }
    }

    if (bytes) {
        qse_free(bytes);
    }

    while (root.count) {
        --root.count;

        if (qse_close(root.dirs[root.count].fd)) {
            step = "directory-close";
            error = *qse_errno();
            rc = 125;
        }
    }

    if (rc == 125) {
        qsc_diagnostic(step, error);
    }

    return rc;
}

/* Factory setup creates only a RAM namespace prefix. A crash before migration
 * leaves the persistent target absent; existing persistent configs are refused. */
static int qsc_initialize(void) {
    QscRoot root = {0};
    QscStatvfs fs = {0};
    QseStat target;
    int rc = 125, error = 0, linked = 0;
    const char *step = "initialize-authority";

    if (qsc_geteuid() != 0) {
        goto done;
    }

    step = "initialize-directories";

    for (unsigned i = 0; i < 4; ++i) {
        if (qsc_pin(&root, i)) {
            goto failed;
        }
    }

    int fs_result = qsc_filesystem(root.dirs[0].fd, &fs);
    qsc_root_metadata(0, &fs, 0);

    if (fs_result) {
        goto failed;
    }

    root.fsid = fs.fsid;
    root.flags = fs.flag;
    step = "initialize-flags";

    if (!(root.flags & QSC_RDONLY)) {
        qsc_bad();
        goto failed;
    }

    if (qsc_flags(&root, root.flags)) {
        goto failed;
    }

    step = "initialize-absent";

    if (!qse_lstat(qsc_target, &target)) {
        qsc_bad();
        goto failed;
    }

    if (*qse_errno() != 2) {
        goto failed;
    }

    if (!qse_lstat(qsc_legacy, &target)) {
        qsc_bad();
        goto failed;
    }

    if (*qse_errno() != 2) {
        goto failed;
    }

    step = "initialize-ram-directory";

    if (qse_lstat(qsc_paths[4], &target)) {
        if (*qse_errno() != 2 || qsc_mkdir(qsc_paths[4], 0755)) {
            goto failed;
        }

        /* mkdir may add one parent directory link. Refresh only this owned change;
         * all other identity fields and the open descriptor must remain identical. */
        QseStat path, opened, previous = root.dirs[3].stat;
        step = "initialize-parent-after-mkdir";

        if (qse_lstat(qsc_paths[3], &path) || qse_fstat(root.dirs[3].fd, &opened)) {
            goto failed;
        }

        qsc_root_metadata(&path, 0, 3);

        if (path.nlink < previous.nlink || path.nlink - previous.nlink > 1 ||
            !qsc_directory_identity(&path, &opened)) {
            qsc_bad();
            goto failed;
        }

        previous.nlink = path.nlink;

        if (!qsc_directory_identity(&previous, &path)) {
            qsc_bad();
            goto failed;
        }

        root.dirs[3].stat = path;
    }

    if (qsc_pin(&root, 4) || qsc_flags(&root, root.flags)) {
        goto failed;
    }

    if (!qse_lstat(qsc_target, &target)) {
        qsc_bad();
        goto failed;
    }

    if (*qse_errno() != 2) {
        goto failed;
    }

    if (!qse_lstat(qsc_legacy, &target)) {
        qsc_bad();
        goto failed;
    }

    if (*qse_errno() != 2) {
        goto failed;
    }

    step = "initialize-prefix";

    if (qsc_pathmgr_symlink(qsc_legacy, qsc_target)) {
        goto failed;
    }

    linked = 1;
    step = "initialize-readback";

    if (qsc_link(0) || qsc_flags(&root, root.flags)) {
        goto failed;
    }

    rc = 0;
    goto done;
failed:
    error = *qse_errno();
done:

    while (root.count) {
        --root.count;

        if (qse_close(root.dirs[root.count].fd)) {
            step = "initialize-close";
            error = *qse_errno();
            rc = 125;
        }
    }

    if (linked && rc != 0) {
        if (qsc_link(0) || qsc_pathmgr_unlink(qsc_target)) {
            step = "initialize-cleanup";
            error = *qse_errno();
        }
    }

    if (rc) {
        qsc_diagnostic(step, error);
    }

    return rc;
}

/* Remove only this adapter's exact temporary prefix on failed provisioning.
 * A regular persistent override is never changed, including after publication. */
static int qsc_abort(void) {
    QscRoot root = {0};
    QscStatvfs fs = {0};
    QseStat target;
    int rc = 125, error = 0;
    const char *step = "abort-root";

    for (unsigned i = 0; i < 3; ++i) {
        if (qsc_pin(&root, i)) {
            goto failed;
        }
    }

    if (qsc_filesystem(root.dirs[0].fd, &fs)) {
        goto failed;
    }

    root.fsid = fs.fsid;
    root.flags = fs.flag;

    if (!(root.flags & QSC_RDONLY) || qsc_flags(&root, root.flags)) {
        qsc_bad();
        goto failed;
    }

    step = "abort-target";

    if (qse_lstat(qsc_target, &target)) {
        if (*qse_errno() == 2) {
            rc = 0;
            goto done;
        }

        goto failed;
    }

    if (qsc_file(&target, 0) && target.dev == root.dirs[0].stat.dev) {
        rc = 0;
        goto done;
    }

    if (qsc_link(&target) || qsc_flags(&root, root.flags) || qsc_pathmgr_unlink(qsc_target)) {
        goto failed;
    }

    if (!qse_lstat(qsc_target, &target) || *qse_errno() != 2) {
        qsc_bad();
        goto failed;
    }

    if (qsc_flags(&root, root.flags)) {
        goto failed;
    }

    rc = 0;
    goto done;
failed:
    error = *qse_errno();
done:

    while (root.count) {
        --root.count;

        if (qse_close(root.dirs[root.count].fd)) {
            error = *qse_errno();
            rc = 125;
        }
    }

    if (rc) {
        qsc_diagnostic(step, error);
    }

    return rc;
}

static void qse_supervisor_start(void) {
    const char *mode = qse_getenv("VOYEMU_SUPERVISOR_CONFIG");

    if (!mode) {
        return;
    }

    if (qsc_string(mode, "abort")) {
        qse_exit(qsc_geteuid() == 0 ? qsc_abort() : 125);
    }

    if (!qsc_string(mode, "ensure")) {
        qse_exit(125);
    }

    int rc = qsc_ensure();

    if (rc == 2) {
        rc = qsc_initialize() == 0 ? 2 : 125;
    }

    qse_exit(rc);
}
