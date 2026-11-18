/* The production property encoder with injected OEM descriptors and final-value getter. The host
 * decoder test in src/app/api/cluster_tree.test.cpp consumes these records. */
#include "cluster_ui_stream.h"
#include <assert.h>
#include <string.h>

typedef unsigned long usize;
typedef unsigned long long u64;
static QgfxUiStream kanzi_stream;

static void copy(void *out, const void *in, usize size) {
    memcpy(out, in, size);
}

#include "cluster_ui_properties.h"

/* Descriptor n is the pointer value n: 1 names, 2 text, 3 and 4 bools, 5 to 7 floats. Node n
 * reads the bool and float slot n - 1. */
static const void *descriptors[7];
static unsigned getter_calls;
static int null_value;
static unsigned char bools[2];
static float floats[2];
static const unsigned special_floats[] = {0x7fc00000u, 0x7f800000u, 0xff800000u};

static unsigned long long tick(void *context) {
    (void)context;

    return 1;
}

static const void *final_value(const void *node, const void *descriptor) {
    const usize slot = (usize)node - 1, type = (usize)descriptor;

    ++getter_calls;

    if (null_value) {
        return 0;
    }

    if (type <= 2) {
        return "node";
    }

    if (type <= 4) {
        return &bools[slot];
    }

    assert(type <= 7);

    return &floats[slot];
}

static const char *string_data(const void *value) {
    return value;
}

static usize string_size(const void *value) {
    return strlen(value);
}

static _Bool effectively_false(const void *node) {
    (void)node;

    return 0;
}

static unsigned read32(const unsigned char *bytes) {
    return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | (unsigned)bytes[3] << 24;
}

static unsigned property_at(const unsigned char *record, unsigned property) {
    unsigned at = 24 + read32(record + 20);

    while (property--) {
        at += 12 + read32(record + at + 8);
    }

    return at;
}

static float float_at(const unsigned char *record, unsigned property) {
    float value;

    memcpy(&value, record + property_at(record, property) + 12, 4);

    return value;
}

static void initialize(void) {
    getter_calls = 0;
    null_value = 0;
    bools[0] = 0;
    bools[1] = 1;
    floats[0] = 0;
    floats[1] = 0.5f;
    (void)qgfx_ui_capture;

    for (unsigned i = 0; i < 7; ++i) {
        descriptors[i] = (const void *)(usize)(i + 1);
    }

    kanzi_stream.deadline = ~0ULL;
    kanzi_stream.ops.clock = tick;
    kanzi_read = (KanziRead){.name_property = &descriptors[0],
                             .value = final_value,
                             .data = string_data,
                             .size = string_size,
                             .visible = effectively_false,
                             .enabled = effectively_false};

    for (unsigned i = 0; i < QGFX_KANZI_UI_PROPERTIES; ++i) {
        kanzi_read.properties[i] = &descriptors[i + 1];
    }
}

/* Observation 0 is node 1 with false and 0; 1 is node 2 with true and 0.5; 2 has null final
 * values; 3 lacks the Opacity descriptor; 4 to 6 hold NaN, +infinity and -infinity. */
unsigned qgfx_test_scalar_record(unsigned observation, unsigned char *out) {
    initialize();
    assert(observation < 7);

    if (observation == 2) {
        null_value = 1;
    }

    if (observation == 3) {
        kanzi_read.properties[QGFX_KANZI_UI_PROPERTY_OPACITY] = 0;
    }

    if (observation >= 4) {
        memcpy(&floats[0], &special_floats[observation - 4], 4);
    }

    return kanzi_record(0, (const void *)(usize)(observation == 1 ? 2 : 1), ~0ULL, out);
}

void qgfx_test_scalar_assertions(void) {
    unsigned char out[2048], other[2048];

    initialize();
    assert(kanzi_record(0, (const void *)1, ~0ULL, out));
    /* One name and six property reads. */
    assert(getter_calls == 7);
    unsigned visible = property_at(out, QGFX_KANZI_UI_PROPERTY_VISIBLE),
             opacity = property_at(out, QGFX_KANZI_UI_PROPERTY_OPACITY);
    assert(read32(out + visible) == QGFX_KANZI_UI_VALUE &&
           read32(out + visible + 4) == QGFX_KANZI_UI_KIND_BOOL && read32(out + visible + 8) == 1 &&
           out[visible + 12] == 0);
    assert(read32(out + opacity) == QGFX_KANZI_UI_VALUE &&
           read32(out + opacity + 4) == QGFX_KANZI_UI_KIND_FLOAT &&
           read32(out + opacity + 8) == 4 && float_at(out, QGFX_KANZI_UI_PROPERTY_OPACITY) == 0);
    /* A second node reads its own values, and the effective flags stay independent. */
    kanzi_record(0, (const void *)2, 0, other);
    visible = property_at(other, QGFX_KANZI_UI_PROPERTY_VISIBLE);
    assert(read32(other + 4) == 0 && other[visible + 12] == 1);
    assert(float_at(other, QGFX_KANZI_UI_PROPERTY_OPACITY) == 0.5f);
    floats[1] = 1920;
    kanzi_record(0, (const void *)2, 0, other);
    assert(float_at(other, QGFX_KANZI_UI_PROPERTY_ACTUAL_WIDTH) == 1920);
    floats[1] = 720;
    kanzi_record(0, (const void *)2, 0, other);
    assert(float_at(other, QGFX_KANZI_UI_PROPERTY_ACTUAL_HEIGHT) == 720);

    for (unsigned i = 0; i < 3; ++i) {
        memcpy(&floats[1], &special_floats[i], 4);
        kanzi_record(0, (const void *)2, 0, other);
        opacity = property_at(other, QGFX_KANZI_UI_PROPERTY_OPACITY);
        assert(read32(other + opacity) == QGFX_KANZI_UI_NON_FINITE &&
               read32(other + opacity + 8) == 0);
    }

    null_value = 1;
    kanzi_record(0, (const void *)1, ~0ULL, other);
    visible = property_at(other, QGFX_KANZI_UI_PROPERTY_VISIBLE);
    assert(read32(other + visible) == QGFX_KANZI_UI_UNSET && read32(other + visible + 8) == 0);
    /* A missing descriptor symbol or a null descriptor skips the getter. */
    null_value = 0;
    unsigned before = getter_calls;
    kanzi_read.properties[QGFX_KANZI_UI_PROPERTY_VISIBLE] = 0;
    kanzi_record(0, (const void *)1, ~0ULL, other);
    assert(getter_calls - before == 6);
    visible = property_at(other, QGFX_KANZI_UI_PROPERTY_VISIBLE);
    assert(read32(other + visible) == QGFX_KANZI_UI_UNAVAILABLE);
    descriptors[1 + QGFX_KANZI_UI_PROPERTY_ENABLED] = 0;
    before = getter_calls;
    kanzi_record(0, (const void *)1, ~0ULL, other);
    assert(getter_calls - before == 5);
}
