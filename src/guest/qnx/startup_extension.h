#ifndef VOYEMU_QNX_STARTUP_EXTENSION_H
#define VOYEMU_QNX_STARTUP_EXTENSION_H
/* Byte validation only: no OEM body, shell parser, or product-specific paths. */
typedef unsigned long QseSize;

static int qse_equal(const unsigned char *a, const unsigned char *b, QseSize n) {
    for (QseSize i = 0; i < n; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }
    }

    return 1;
}

static int qse_anchor(const unsigned char *p, QseSize n, QseSize *out) {
    static const unsigned char anchor[] = "clusterboot &";
    QseSize count = 0, at = 0;

    for (QseSize i = 0; i + sizeof(anchor) - 1 <= n; ++i) {
        if (qse_equal(p + i, anchor, sizeof(anchor) - 1)) {
            if ((i && p[i - 1] != '\n') ||
                (i + sizeof(anchor) - 1 < n && p[i + sizeof(anchor) - 1] != '\n')) {
                return -1;
            }

            ++count;
            at = i;
        }
    }

    if (count != 1) {
        return -1;
    }

    *out = at;

    return 0;
}

static int qse_extract(const unsigned char *base, QseSize bn, const unsigned char *installed,
                       QseSize in, QseSize *offset, QseSize *length) {
    if (!base || !installed || !offset || !length || !bn || bn > 65536 || in > 131072 || in < bn ||
        in - bn > 65536) {
        return -1;
    }

    for (QseSize i = 0; i < bn; ++i) {
        if (!base[i]) {
            return -1;
        }
    }

    for (QseSize i = 0; i < in; ++i) {
        if (!installed[i]) {
            return -1;
        }
    }

    QseSize before, after;

    if (qse_anchor(base, bn, &before) || qse_anchor(installed, in, &after)) {
        return -1;
    }

    QseSize added = in - bn;

    if (after != before + added || !qse_equal(base, installed, before) ||
        !qse_equal(base + before, installed + after, bn - before) ||
        (added && installed[after - 1] != '\n')) {
        return -1;
    }

    *offset = before;
    *length = added;

    return 0;
}

#endif
