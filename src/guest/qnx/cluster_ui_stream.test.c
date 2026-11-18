#include "cluster_ui_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long usize;
typedef unsigned long long u64;
static QgfxUiStream kanzi_stream;

static void copy(void *out, const void *in, usize size) {
    memcpy(out, in, size);
}

#include "cluster_ui_properties.h"

typedef struct {
    unsigned long long ticks, allocations, frees, pins, releases, acquisitions, fail_allocate;
    unsigned pin_error, mode, nodes;
} Fixture;

static unsigned long long tick(void *opaque) {
    return ++((Fixture *)opaque)->ticks;
}

static void *allocate(void *opaque, unsigned long long bytes) {
    Fixture *f = opaque;

    if (++f->allocations == f->fail_allocate) {
        return 0;
    }

    return malloc((size_t)bytes);
}

static void deallocate(void *opaque, void *memory) {
    ++((Fixture *)opaque)->frees;
    free(memory);
}

static unsigned pin(void *opaque, QgfxKanziUiData **page, unsigned long long *physical) {
    Fixture *f = opaque;

    if (f->pin_error) {
        return f->pin_error;
    }

    *page = calloc(1, sizeof(**page));
    assert(*page);
    *physical = 0x40010000ULL + ++f->pins * 4096;

    return 0;
}

static void release(void *opaque, QgfxUiShared node) {
    if (node.control) {
        ++((Fixture *)opaque)->releases;
    }
}

static unsigned long long children(void *opaque, const void *node) {
    Fixture *f = opaque;
    const size_t id = (size_t)node;

    if (f->mode == 1) {
        return id < f->nodes;
    }

    if (f->mode == 2) {
        return 1;
    }

    if (f->mode == 3) {
        return id == 1 ? 2 : 0;
    }

    return id == 1 ? f->nodes - 2 : id == f->nodes - 1;
}

static QgfxUiShared child(void *opaque, const void *node, unsigned long long index) {
    Fixture *f = opaque;
    size_t id = (size_t)node;

    ++f->acquisitions;

    if (f->mode == 1) {
        ++id;
    } else if (f->mode == 2) {
        id = 1;
    } else if (f->mode == 3) {
        id = 2;
    } else {
        id = id == 1 ? (size_t)index + 2 : f->nodes;
    }

    return (QgfxUiShared){(void *)id, (void *)id};
}

static void u32(unsigned char *bytes, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[i] = (unsigned char)(value >> (8 * i));
    }
}

static unsigned record(void *opaque, const void *node, unsigned long long parent,
                       unsigned char *bytes) {
    (void)opaque;
    (void)node;
    memset(bytes, 0, 32);
    u32(bytes, 32);
    u32(bytes + 4, 3);

    for (unsigned i = 0; i < 8; ++i) {
        bytes[8 + i] = (unsigned char)(parent >> (8 * i));
    }

    u32(bytes + 16, QGFX_KANZI_UI_VALUE);

    return 32;
}

static void capture(QgfxUiStream *s) {
    s->header->used = s->header->pages = s->header->count = s->header->first_page = 0;
    s->header->reason = 0;
    s->cursor = 0;
    s->sequence += 2;
    Fixture *f = s->context;
    ++f->acquisitions;
    qgfx_ui_capture(s, (QgfxUiShared){(void *)1, (void *)1});
    assert(f->acquisitions == f->releases && s->transient == 0);
}

static unsigned long long stream_number(QgfxUiStream *s, unsigned long long offset) {
    unsigned long long value = 0;

    for (unsigned i = 0; i < 8; ++i) {
        QgfxUiBlock *block = s->first;
        unsigned long long at = offset + i;

        while (at >= QGFX_KANZI_UI_PAYLOAD) {
            block = block->next;
            at -= QGFX_KANZI_UI_PAYLOAD;
        }

        value |= (unsigned long long)block->page->payload[at] << (8 * i);
    }

    return value;
}

static void teardown(QgfxUiStream *s) {
    /* Represents process exit, after the last host reader; production never
     * reclaims these pages. */
    while (s->first) {
        QgfxUiBlock *next = s->first->next;
        free(s->first->page);
        deallocate(s->context, s->first);
        s->first = next;
    }
}

static Fixture *getter_fixture;
static unsigned value_calls, data_calls, size_calls, visible_calls, enabled_calls;

static const void *slow_value(const void *node, const void *type) {
    (void)node;
    (void)type;
    ++value_calls;
    getter_fixture->ticks += 1000;

    return (void *)1;
}

static const char *string_data(const void *value) {
    (void)value;
    ++data_calls;

    return "value";
}

static usize string_size(const void *value) {
    (void)value;
    ++size_calls;

    return 5;
}

static _Bool visible(const void *node) {
    (void)node;
    ++visible_calls;

    return 1;
}

static _Bool enabled(const void *node) {
    (void)node;
    ++enabled_calls;

    return 1;
}

static void getter_deadline(QgfxUiOps ops) {
    Fixture f = {.nodes = 10};

    getter_fixture = &f;
    QgfxKanziUiPage h = {0};
    const void *type = (void *)1;
    kanzi_read = (KanziRead){.name_property = &type,
                             .value = slow_value,
                             .data = string_data,
                             .size = string_size,
                             .visible = visible,
                             .enabled = enabled};
    kanzi_stream = (QgfxUiStream){
            .ops = ops, .context = &f, .budget = 1024 * 1024, .deadline = 100, .header = &h};
    kanzi_stream.ops.record = kanzi_record;
    capture(&kanzi_stream);
    assert(h.reason == QGFX_KANZI_UI_TIME_BUDGET && h.count == 0 && h.used == 0);
    assert(value_calls == 1 && !data_calls && !size_calls && !visible_calls && !enabled_calls);
    kanzi_stream.deadline = ~0ULL;
    kanzi_read.properties[0] = &type;
    f.mode = 3;
    capture(&kanzi_stream);
    /* Header, name and text records, then five unavailable scalar records. */
    assert(h.reason == 0 && h.count == 3 && h.used == 3 * 106);
    const unsigned char *bytes = kanzi_stream.first->page->payload;
    assert(bytes[0] == 106 && bytes[4] == 3 && bytes[16] == QGFX_KANZI_UI_VALUE && bytes[20] == 5);
    assert(memcmp(bytes + 24, "value", 5) == 0 && bytes[29] == QGFX_KANZI_UI_VALUE &&
           bytes[33] == QGFX_KANZI_UI_KIND_STRING && bytes[37] == 5 &&
           memcmp(bytes + 41, "value", 5) == 0 && bytes[46] == QGFX_KANZI_UI_UNAVAILABLE &&
           bytes[50] == QGFX_KANZI_UI_KIND_BOOL);
    teardown(&kanzi_stream);
}

int main(void) {
    Fixture f = {.nodes = 70000};
    QgfxKanziUiPage header = {0};
    QgfxUiStream s = {.ops = {tick, allocate, deallocate, pin, release, children, child, record},
                      .context = &f,
                      .budget = 8ULL * 1024 * 1024,
                      .deadline = ~0ULL,
                      .retained = sizeof(header),
                      .header = &header};

    capture(&s);
    assert(header.reason == 0 && header.count == 70000 && header.pages > 31);
    const unsigned long long pins = f.pins, retained = s.retained;
    /* Repeated captures reuse the pinned pool without unpin/free or per-sample
     * growth. */
    capture(&s);
    assert(header.reason == 0 && f.pins == pins && s.retained == retained);
    f.mode = 1;
    f.nodes = 128;
    capture(&s);
    assert(header.reason == 0 && header.count == 128);
    f.mode = 2;
    capture(&s);
    assert(header.reason == QGFX_KANZI_UI_CYCLE && header.count == 1);
    f.mode = 3;
    capture(&s);
    assert(header.reason == 0 && header.count == 3);
    s.deadline = f.ticks + 10;
    capture(&s);
    assert(header.reason == QGFX_KANZI_UI_TIME_BUDGET);
    s.deadline = ~0ULL;
    s.budget = s.retained;
    capture(&s);
    assert(header.reason == QGFX_KANZI_UI_BYTE_BUDGET && header.count == 0);
    s.budget += sizeof(QgfxUiFrame) * 2;
    f.fail_allocate = f.allocations + 1;
    capture(&s);
    assert(header.reason == QGFX_KANZI_UI_ALLOCATION_FAILED);
    teardown(&s);

    /* Pin failures have distinct status and do not retain unpublished allocation
     * metadata. */
    for (unsigned reason = QGFX_KANZI_UI_PIN_FAILED; reason <= QGFX_KANZI_UI_PHYSICAL_FAILED;
         ++reason) {
        Fixture failed = {.nodes = 10, .pin_error = reason};
        QgfxKanziUiPage h = {0};
        QgfxUiStream fresh = {.ops = s.ops,
                              .context = &failed,
                              .budget = 1024 * 1024,
                              .deadline = ~0ULL,
                              .header = &h};
        capture(&fresh);
        assert(h.reason == reason && !fresh.first && failed.allocations == failed.frees);
    }

    for (unsigned fail = 1; fail <= 14; ++fail) {
        Fixture failed = {.nodes = 10, .fail_allocate = fail};
        QgfxKanziUiPage h = {0};
        QgfxUiStream fresh = {.ops = s.ops,
                              .context = &failed,
                              .budget = 1024 * 1024,
                              .deadline = ~0ULL,
                              .header = &h};
        capture(&fresh);
        assert(h.reason == (failed.allocations >= fail ? QGFX_KANZI_UI_ALLOCATION_FAILED : 0));
        teardown(&fresh);
        assert(failed.allocations - (failed.allocations >= fail ? 1 : 0) == failed.frees);
    }

    getter_deadline(s.ops);
    puts("production dynamic stream/traversal: large, deep, cycle, reuse and "
         "resource failures passed");

    return 0;
}
