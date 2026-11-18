#ifndef VOYEMU_CLUSTER_UI_STREAM_H
#define VOYEMU_CLUSTER_UI_STREAM_H
#include "cluster_ui_wire.h"

/* Allocation, clock and scene callbacks are the production boundary, also
 * exercised by host synthetic tests. No retained scene object or heap address
 * is placed on the wire. */
typedef struct QgfxUiBlock {
    struct QgfxUiBlock *next;
    QgfxKanziUiData *page;
    unsigned long long physical;
} QgfxUiBlock;
typedef struct {
    const void *object;
    void *control;
} QgfxUiShared;
typedef struct {
    unsigned long long (*clock)(void *);
    void *(*allocate)(void *, unsigned long long);
    void (*deallocate)(void *, void *);
    unsigned (*pin_page)(void *, QgfxKanziUiData **, unsigned long long *);
    void (*release)(void *, QgfxUiShared);
    unsigned long long (*children)(void *, const void *);
    QgfxUiShared (*child)(void *, const void *, unsigned long long);
    unsigned (*record)(void *, const void *, unsigned long long, unsigned char *);
} QgfxUiOps;
typedef struct {
    QgfxUiOps ops;
    void *context;
    QgfxUiBlock *first, *tail, *cursor;
    unsigned long long retained, transient, budget, deadline, sequence;
    QgfxKanziUiPage *header;
} QgfxUiStream;

typedef struct QgfxUiFrame {
    struct QgfxUiFrame *parent;
    QgfxUiShared node;
    unsigned long long id, next, children;
} QgfxUiFrame;

static int qgfx_ui_time(QgfxUiStream *s) {
    if (s->ops.clock(s->context) >= s->deadline) {
        s->header->reason = QGFX_KANZI_UI_TIME_BUDGET;

        return 0;
    }

    return 1;
}

static int qgfx_ui_room(QgfxUiStream *s, unsigned long long bytes) {
    if (s->retained > s->budget || s->transient > s->budget - s->retained ||
        bytes > s->budget - s->retained - s->transient) {
        s->header->reason = QGFX_KANZI_UI_BYTE_BUDGET;

        return 0;
    }

    return qgfx_ui_time(s);
}

static QgfxUiBlock *qgfx_ui_grow(QgfxUiStream *s) {
    if (!qgfx_ui_room(s, sizeof(QgfxUiBlock) + QGFX_KANZI_UI_BYTES)) {
        return 0;
    }

    QgfxUiBlock *b = s->ops.allocate(s->context, sizeof(*b));

    if (!b) {
        s->header->reason = QGFX_KANZI_UI_ALLOCATION_FAILED;

        return 0;
    }

    b->next = 0;
    const unsigned reason = s->ops.pin_page(s->context, &b->page, &b->physical);

    if (reason) {
        s->ops.deallocate(s->context, b);
        s->header->reason = reason;

        return 0;
    }

    if (s->tail) {
        s->tail->next = b;
    } else {
        s->first = b;
    }

    s->tail = b;
    s->retained += sizeof(*b) + QGFX_KANZI_UI_BYTES;

    return b;
}

/* Reserve a whole record before touching the stream. A newly allocated page
 * remains in the pool even when the deadline expires; a reader can never race
 * with reclamation of published pages. */
static int qgfx_ui_append(QgfxUiStream *s, const unsigned char *record, unsigned bytes) {
    QgfxUiBlock *b = s->cursor;
    unsigned available = b ? QGFX_KANZI_UI_PAYLOAD - b->page->used : 0;

    if (!qgfx_ui_time(s)) {
        return 0;
    }

    if (bytes > available) {
        QgfxUiBlock *next = b ? b->next : s->first;
        unsigned left = bytes - available;

        while (left) {
            if (!next) {
                next = qgfx_ui_grow(s);
            }

            if (!next) {
                return 0;
            }

            left = left > QGFX_KANZI_UI_PAYLOAD ? left - QGFX_KANZI_UI_PAYLOAD : 0;
            next = next->next;
        }
    }

    if (!qgfx_ui_time(s)) {
        return 0;
    }

    while (bytes) {
        if (!b || b->page->used == QGFX_KANZI_UI_PAYLOAD) {
            QgfxUiBlock *previous = b;
            b = b ? b->next : s->first;
            b->page->sequence = s->sequence;
            b->page->ordinal = s->header->pages++;
            b->page->used = b->page->reserved = 0;
            b->page->next = 0;

            if (previous) {
                previous->page->next = b->physical;
            } else {
                s->header->first_page = b->physical;
            }
        }

        unsigned amount = QGFX_KANZI_UI_PAYLOAD - b->page->used;

        if (amount > bytes) {
            amount = bytes;
        }

        for (unsigned i = 0; i < amount; ++i) {
            b->page->payload[b->page->used + i] = record[i];
        }

        b->page->used += amount;
        s->header->used += amount;
        record += amount;
        bytes -= amount;
    }

    s->cursor = b;

    return 1;
}

/* Iterative preorder DFS: dynamic frame memory is charged to the same byte
 * budget as the pinned pool. Only active ancestors define a cycle, so repeated
 * objects in separate branches are valid. */
static void qgfx_ui_capture(QgfxUiStream *s, QgfxUiShared root) {
    QgfxUiFrame *top = 0;
    QgfxUiShared pending = root;
    unsigned long long parent = QGFX_KANZI_UI_ROOT;

    for (;;) {
        if (!pending.object) {
            if (!s->header->reason) {
                s->header->reason = QGFX_KANZI_UI_SOURCE_UNAVAILABLE;
            }

            s->ops.release(s->context, pending);
            break;
        }

        for (QgfxUiFrame *a = top; a; a = a->parent) {
            if (!qgfx_ui_time(s)) {
                break;
            }

            if (a->node.object == pending.object) {
                s->header->reason = QGFX_KANZI_UI_CYCLE;
                break;
            }
        }

        if (s->header->reason || !qgfx_ui_room(s, sizeof(QgfxUiFrame))) {
            s->ops.release(s->context, pending);
            break;
        }

        QgfxUiFrame *frame = s->ops.allocate(s->context, sizeof(*frame));

        if (!frame) {
            s->header->reason = QGFX_KANZI_UI_ALLOCATION_FAILED;
            s->ops.release(s->context, pending);
            break;
        }

        s->transient += sizeof(*frame);
        *frame = (QgfxUiFrame){.parent = top, .node = pending, .id = s->header->count};
        top = frame;
        unsigned char record[24 + QGFX_KANZI_UI_MAX_NAME +
                             QGFX_KANZI_UI_PROPERTIES * (12 + QGFX_KANZI_UI_MAX_STRING)];

        if (!qgfx_ui_time(s)) {
            break;
        }

        const unsigned bytes = s->ops.record(s->context, pending.object, parent, record);

        if (!bytes || bytes > sizeof(record)) {
            if (!s->header->reason) {
                s->header->reason = QGFX_KANZI_UI_SOURCE_UNAVAILABLE;
            }

            break;
        }

        if (!qgfx_ui_append(s, record, bytes)) {
            break;
        }

        ++s->header->count;

        if (!qgfx_ui_time(s)) {
            break;
        }

        frame->children = s->ops.children(s->context, pending.object);

        while (top && top->next == top->children) {
            QgfxUiFrame *finished = top;
            top = top->parent;
            s->ops.release(s->context, finished->node);
            s->ops.deallocate(s->context, finished);
            s->transient -= sizeof(*finished);
        }

        if (!top || !qgfx_ui_time(s)) {
            break;
        }

        parent = top->id;
        pending = s->ops.child(s->context, top->node.object, top->next++);
    }

    while (top) {
        QgfxUiFrame *finished = top;
        top = top->parent;
        s->ops.release(s->context, finished->node);
        s->ops.deallocate(s->context, finished);
        s->transient -= sizeof(*finished);
    }
}

#endif
