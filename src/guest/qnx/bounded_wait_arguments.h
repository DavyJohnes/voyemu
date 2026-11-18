/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#ifndef VOYEMU_QNX_BOUNDED_WAIT_ARGUMENTS_H
#define VOYEMU_QNX_BOUNDED_WAIT_ARGUMENTS_H
#define QBW_MAX_ARGS 16
#define QBW_ARG_BYTES 4096
#define QBW_MAX_ENV 128
typedef const char *(*QbwGetEnv)(void *, const char *);
typedef struct {
    int count;
    char bytes[QBW_ARG_BYTES];
    char *values[QBW_MAX_ARGS + 1];
} QbwArguments;

static unsigned qbw_length(const char *s, unsigned cap) {
    unsigned n = 0;

    if (!s) {
        return cap;
    }

    while (n < cap && s[n]) {
        ++n;
    }

    return n;
}

static int qbw_equal(const char *a, const char *b) {
    unsigned i = 0;

    while (a[i] && a[i] == b[i]) {
        ++i;
    }

    return a[i] == b[i];
}

static void qbw_arg_key(unsigned i, char key[24]) {
    const char prefix[] = "VOYEMU_QBW_ARG_";
    unsigned j = 0;

    for (; j < sizeof(prefix) - 1; ++j) {
        key[j] = prefix[j];
    }

    key[j++] = (char)('0' + i / 10);
    key[j++] = (char)('0' + i % 10);
    key[j] = 0;
}

static int qbw_arguments(QbwGetEnv get, void *opaque, QbwArguments *out) {
    const char *count = get(opaque, "VOYEMU_QBW_ARGC");
    unsigned n = 0, at = 0;

    if (!count || !*count || count[0] == '0') {
        return 0;
    }

    for (unsigned i = 0; count[i]; ++i) {
        if (i >= 2 || count[i] < '0' || count[i] > '9') {
            return 0;
        }

        n = n * 10 + (unsigned)(count[i] - '0');
    }

    if (!n || n > QBW_MAX_ARGS) {
        return 0;
    }

    for (unsigned i = 0; i < n; ++i) {
        char key[24];
        qbw_arg_key(i, key);
        const char *v = get(opaque, key);
        unsigned len = qbw_length(v, 1025);

        if (len > 1024 || len + 1 > QBW_ARG_BYTES - at) {
            return 0;
        }

        if (!i && (!len || v[0] != '/')) {
            return 0;
        }

        out->values[i] = out->bytes + at;

        for (unsigned j = 0; j <= len; ++j) {
            out->bytes[at++] = v[j];
        }
    }

    for (unsigned i = n; i < QBW_MAX_ARGS; ++i) {
        char key[24];
        qbw_arg_key(i, key);

        if (get(opaque, key)) {
            return 0;
        }
    }

    out->values[n] = 0;
    out->count = (int)n;

    return 1;
}

/* Spawn receives this explicit environment; transport and preload never propagate. */
static int qbw_environment(char *const *source, char **out) {
    unsigned count = 0, total = 0;

    if (!source) {
        return 0;
    }

    for (unsigned i = 0; source[i]; ++i) {
        if (i >= QBW_MAX_ENV) {
            return 0;
        }

        char *entry = source[i];
        unsigned len = qbw_length(entry, 4097), key = 0;

        if (len > 4096) {
            return 0;
        }

        while (key < len && entry[key] != '=') {
            ++key;
        }

        if (!key || key == len) {
            return 0;
        }

        if (total > 65536 - len - 1) {
            return 0;
        }

        total += len + 1;

        for (unsigned j = 0; j < i; ++j) {
            unsigned k = 0;

            while (k < key && source[j][k] == entry[k]) {
                ++k;
            }

            if (k == key && source[j][k] == '=') {
                return 0;
            }
        }

        const char prefix[] = "VOYEMU_QBW_";
        unsigned k = 0;

        while (k < sizeof(prefix) - 1 && entry[k] == prefix[k]) {
            ++k;
        }

        if (k == sizeof(prefix) - 1) {
            continue;
        }

        const char preload[] = "LD_PRELOAD";
        k = 0;

        while (k < sizeof(preload) - 1 && entry[k] == preload[k]) {
            ++k;
        }

        if (k == sizeof(preload) - 1 && entry[k] == '=') {
            continue;
        }

        out[count++] = entry;
    }

    out[count] = 0;

    return 1;
}

#endif
/* NOLINTEND(modernize-use-nullptr) */
