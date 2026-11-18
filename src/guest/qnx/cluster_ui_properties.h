#ifndef VOYEMU_CLUSTER_UI_PROPERTIES_H
#define VOYEMU_CLUSTER_UI_PROPERTIES_H
typedef struct {
    const void *(*application)(void);
    const void *(*engine)(const void *);
    void *root;
    usize (*child_count)(const void *);
    void *child;
    void (*release)(void *);
    _Bool (*visible)(const void *);
    _Bool (*enabled)(const void *);
    const void *const *name_property;
    const void *(*value)(const void *, const void *);
    const char *(*data)(const void *);
    usize (*size)(const void *);
    /* Optional: a property type this image does not export is reported unavailable per node. */
    const void *const *properties[QGFX_KANZI_UI_PROPERTIES];
} KanziRead;
static KanziRead kanzi_read;
/* Each property record carries its kind; the host accepts only this fixed order. */
static const unsigned kanzi_property_kinds[QGFX_KANZI_UI_PROPERTIES] = {
        [QGFX_KANZI_UI_PROPERTY_TEXT] = QGFX_KANZI_UI_KIND_STRING,
        [QGFX_KANZI_UI_PROPERTY_VISIBLE] = QGFX_KANZI_UI_KIND_BOOL,
        [QGFX_KANZI_UI_PROPERTY_ENABLED] = QGFX_KANZI_UI_KIND_BOOL,
        [QGFX_KANZI_UI_PROPERTY_OPACITY] = QGFX_KANZI_UI_KIND_FLOAT,
        [QGFX_KANZI_UI_PROPERTY_ACTUAL_WIDTH] = QGFX_KANZI_UI_KIND_FLOAT,
        [QGFX_KANZI_UI_PROPERTY_ACTUAL_HEIGHT] = QGFX_KANZI_UI_KIND_FLOAT,
};

static void kanzi_u32(unsigned char *out, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) {
        out[i] = (unsigned char)(value >> (8 * i));
    }
}

static void kanzi_u64(unsigned char *out, u64 value) {
    for (unsigned i = 0; i < 8; ++i) {
        out[i] = (unsigned char)(value >> (8 * i));
    }
}

static const void *kanzi_value(const void *node, const void *descriptor) {
    if (!qgfx_ui_time(&kanzi_stream)) {
        return 0;
    }

    const void *value = descriptor ? kanzi_read.value(node, descriptor) : 0;

    return qgfx_ui_time(&kanzi_stream) ? value : 0;
}

/* Size of a std::string value within limit, or (usize)-1 when it is absent or longer. */
static usize kanzi_string_size(const void *value, const char **text, usize limit) {
    *text = value && qgfx_ui_time(&kanzi_stream) ? kanzi_read.data(value) : 0;

    if (!qgfx_ui_time(&kanzi_stream)) {
        return (usize)-1;
    }

    const usize size = *text ? kanzi_read.size(value) : 0;

    return qgfx_ui_time(&kanzi_stream) && *text && size <= limit ? size : (usize)-1;
}

static unsigned kanzi_record(void *context, const void *node, u64 parent, unsigned char *out) {
    (void)context;
    const void *descriptor = *kanzi_read.name_property;
    const char *name = 0;
    usize size = kanzi_string_size(descriptor ? kanzi_value(node, descriptor) : 0, &name,
                                   QGFX_KANZI_UI_MAX_NAME);
    unsigned flags = 0;

    if (!qgfx_ui_time(&kanzi_stream)) {
        return 0;
    }

    if (kanzi_read.visible(node)) {
        flags |= QGFX_KANZI_UI_VISIBLE;
    }

    if (!qgfx_ui_time(&kanzi_stream)) {
        return 0;
    }

    if (kanzi_read.enabled(node)) {
        flags |= QGFX_KANZI_UI_ENABLED;
    }

    if (!qgfx_ui_time(&kanzi_stream)) {
        return 0;
    }

    if (size == (usize)-1) {
        size = 0;
        flags |= QGFX_KANZI_UI_NAME_UNAVAILABLE;
    }

    unsigned char status[QGFX_KANZI_UI_PROPERTIES], scalars[QGFX_KANZI_UI_PROPERTIES][4];
    const char *values[QGFX_KANZI_UI_PROPERTIES];
    usize sizes[QGFX_KANZI_UI_PROPERTIES], total = 24 + size;

    for (unsigned i = 0; i < QGFX_KANZI_UI_PROPERTIES; ++i) {
        const void *type = kanzi_read.properties[i] ? *kanzi_read.properties[i] : 0;
        const void *value = type ? kanzi_value(node, type) : 0;
        values[i] = 0;
        sizes[i] = 0;
        status[i] = !type    ? QGFX_KANZI_UI_UNAVAILABLE
                    : !value ? QGFX_KANZI_UI_UNSET
                             : QGFX_KANZI_UI_VALUE;

        if (value && kanzi_property_kinds[i] == QGFX_KANZI_UI_KIND_STRING) {
            sizes[i] = kanzi_string_size(value, &values[i], QGFX_KANZI_UI_MAX_STRING);

            if (sizes[i] == (usize)-1) {
                status[i] = QGFX_KANZI_UI_TOO_LONG;
            }
        } else if (value) {
            /* The final value source holds the scalar at byte zero; copy it before the next
             * Kanzi call. */
            sizes[i] = kanzi_property_kinds[i] == QGFX_KANZI_UI_KIND_BOOL ? 1 : 4;
            copy(scalars[i], value, sizes[i]);
            values[i] = (const char *)scalars[i];

            if (sizes[i] == 1) {
                scalars[i][0] = scalars[i][0] != 0;
            } else if ((scalars[i][3] & 0x7f) == 0x7f && (scalars[i][2] & 0x80)) {
                /* An all-ones binary32 exponent denotes NaN or infinity. */
                status[i] = QGFX_KANZI_UI_NON_FINITE;
            }
        }

        if (!qgfx_ui_time(&kanzi_stream)) {
            return 0;
        }

        total += 12 + (status[i] == QGFX_KANZI_UI_VALUE ? sizes[i] : 0);
    }

    kanzi_u32(out, (unsigned)total);
    kanzi_u32(out + 4, flags);
    kanzi_u64(out + 8, parent);
    kanzi_u32(out + 16, flags & QGFX_KANZI_UI_NAME_UNAVAILABLE ? QGFX_KANZI_UI_UNAVAILABLE
                                                               : QGFX_KANZI_UI_VALUE);
    kanzi_u32(out + 20, (unsigned)size);

    if (size) {
        copy(out + 24, name, size);
    }

    usize at = 24 + size;

    for (unsigned i = 0; i < QGFX_KANZI_UI_PROPERTIES; ++i) {
        const unsigned bytes = status[i] == QGFX_KANZI_UI_VALUE ? (unsigned)sizes[i] : 0;
        kanzi_u32(out + at, status[i]);
        kanzi_u32(out + at + 4, kanzi_property_kinds[i]);
        kanzi_u32(out + at + 8, bytes);
        at += 12;

        if (bytes) {
            copy(out + at, values[i], bytes);
            at += bytes;
        }
    }

    return (unsigned)at;
}

#endif
