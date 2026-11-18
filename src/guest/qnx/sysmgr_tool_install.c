/* A9-only embedded assets, activated by the existing initialized userdata helper. */
#ifdef QST_TEST
#define QST_API(name)
#else
#define QST_API(name) __asm__(#name)
#endif
extern int qst_mkdir(const char *, unsigned) QST_API(mkdir);
extern int qst_readlink(const char *, char *, QseSize) QST_API(readlink);
extern int qst_namespace_link(const char *, const char *) QST_API(pathmgr_symlink);
#undef QST_API

static void qst_trace(const char *text) {
#ifndef QST_TEST
    int saved = *qse_errno();
    QseSize n = 0;

    while (text[n] && n < 127) {
        n++;
    }

    qse_write(2, text, n);
    *qse_errno() = saved;
#else
    (void)text;
#endif
}

static int qst_equal(const unsigned char *a, const unsigned char *b, QseSize n) {
    for (QseSize i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }

    return 1;
}

static unsigned char *qst_original(void) {
    if (!QST_PACKED_LENGTH || !QST_ORIGINAL_LENGTH || QST_ORIGINAL_LENGTH > 262144) {
        return 0;
    }

    unsigned char *out = qse_malloc(QST_ORIGINAL_LENGTH + 1);

    if (!out) {
        return 0;
    }

    QseSize in = 0, at = 0;

    while (in < QST_PACKED_LENGTH) {
        unsigned token = qst_original_pack[in++];
        QseSize n = (token & 127) + 1;

        if (n > QST_ORIGINAL_LENGTH - at) {
            goto bad;
        }

        if (token & 128) {
            for (QseSize i = 0; i < n; i++) {
                out[at++] = 0;
            }
        } else {
            if (n > QST_PACKED_LENGTH - in) {
                goto bad;
            }

            for (QseSize i = 0; i < n; i++) {
                out[at++] = qst_original_pack[in++];
            }
        }
    }

    if (at == QST_ORIGINAL_LENGTH) {
        return out;
    }

bad:
    qse_free(out);

    return 0;
}

static int qst_matches(const char *path, const unsigned char *bytes, QseSize size) {
    QseStat st = {0};
    QseSize used = 0;
    unsigned char *actual = qse_malloc(size + 1);

    if (!actual) {
        return 0;
    }

    int meta = qse_lstat(path, &st);
    int ok = !meta && qse_regular(&st, size) && st.nlink == 1 && (QseSize)st.size == size &&
             qse_load(path, actual, size, &used) == 0 && used == size &&
             qst_equal(actual, bytes, size);

#ifndef QST_TEST
    if (!ok) {
        int saved = *qse_errno();
        char line[256];
        int n = qse_snprintf(
                line, sizeof(line),
                "QMC SYS match meta=%d errno=%d uid=%u mode=%o nlink=%u size=%lld want=%lu used=%lu\n",
                meta, saved, st.uid, st.mode, st.nlink, st.size, size, used);

        if (n > 0 && (QseSize)n < sizeof(line)) {
            qse_write(2, line, (QseSize)n);
        }

        *qse_errno() = saved;
    }
#endif
    qse_free(actual);

    return ok;
}

static int qst_asset(const char *path, const unsigned char *bytes, QseSize size) {
    QseStat st;

    if (!qse_lstat(path, &st)) {
        return (st.mode & 0777) == 0500 && qst_matches(path, bytes, size) ? 0 : -1;
    }

    if (*qse_errno() != 2) {
        return -1;
    }

    int fd = qse_open(path, 1 | 0400 | 02000 | 010000 | 020000, 0500);

    if (fd < 0) {
        return -1;
    }

    int rc = -1;
    QseSize at = 0;

    if (qse_fstat(fd, &st) || !qse_regular(&st, size) || st.nlink != 1 || st.size != 0 ||
        (st.mode & 0777) != 0500) {
        goto done;
    }

    while (at < size) {
        long n = qse_write(fd, bytes + at, size - at);

        if (n < 0 && *qse_errno() == 4) {
            continue;
        }

        if (n <= 0) {
            goto done;
        }

        at += (QseSize)n;
    }

    if (qse_fsync(fd)) {
        goto done;
    }

    rc = 0;
done:

    if (qse_close(fd)) {
        rc = -1;
    }

    return rc == 0 && qst_matches(path, bytes, size) ? 0 : -1;
}

static int qst_install(void) {
    static const char directory[] = "/log/voyemu-sysmgr",
                      launcher[] = "/log/voyemu-sysmgr/sysmgrtool";

    qst_trace("QMC SYS begin\n");
    unsigned char *original = qst_original();

    if (!original) {
        return -1;
    }

    qst_trace("QMC SYS decoded\n");
    char target[128];
    int n = qst_readlink("/usr/bin/sysmgrtool", target, sizeof(target));

#ifndef QST_TEST
    {
        int saved = *qse_errno();
        char line[128];
        int z = qse_snprintf(line, sizeof(line), "QMC SYS readlink n=%d errno=%d\n", n, saved);

        if (z > 0 && (QseSize)z < sizeof(line)) {
            qse_write(2, line, (QseSize)z);
        }

        *qse_errno() = saved;
    }
#endif
    qst_trace("QMC SYS readlink\n");
    int installed = n == (int)sizeof(launcher) - 1 &&
                    qst_equal((const unsigned char *)target, (const unsigned char *)launcher,
                              sizeof(launcher) - 1);

    if (n >= 0 && !installed) {
        goto bad;
    }

    if (!qst_matches(installed ? "/log/voyemu-sysmgr/sysmgrtool.oem" : "/usr/bin/sysmgrtool",
                     original, QST_ORIGINAL_LENGTH)) {
        goto bad;
    }

    qst_trace("QMC SYS original-verified\n");
    QseStat st;

    if (qse_lstat(directory, &st)) {
        if (*qse_errno() != 2 || qst_mkdir(directory, 0700) || qse_lstat(directory, &st)) {
            goto bad;
        }
    }

    if ((st.mode & 0170000) != 0040000 || st.uid != 0 || (st.mode & 0777) != 0700) {
        goto bad;
    }

    qst_trace("QMC SYS directory-verified\n");

    if (qst_asset("/log/voyemu-sysmgr/sysmgrtool.oem", original, QST_ORIGINAL_LENGTH) ||
        qst_asset(launcher, qst_launcher, sizeof(qst_launcher)) ||
        qst_asset("/log/voyemu-sysmgr/wait.so", qst_shim, sizeof(qst_shim))) {
        goto bad;
    }

    qst_trace("QMC SYS assets-verified\n");

    if (!installed && !qst_matches("/usr/bin/sysmgrtool", original, QST_ORIGINAL_LENGTH)) {
        goto bad;
    }

    qst_trace("QMC SYS namespace-begin\n");

    if (!installed && qst_namespace_link(launcher, "/usr/bin/sysmgrtool")) {
        goto bad;
    }

    qst_trace("QMC SYS namespace-returned\n");
    n = qst_readlink("/usr/bin/sysmgrtool", target, sizeof(target));

    if (n != (int)sizeof(launcher) - 1 ||
        !qst_equal((const unsigned char *)target, (const unsigned char *)launcher,
                   sizeof(launcher) - 1)) {
        goto bad;
    }

    qst_trace("QMC SYS installed\n");
    qse_free(original);

    return 0;
bad:
    qst_trace("QMC SYS failed\n");
    qse_free(original);

    return -1;
}
