#ifndef VOYEMU_CLUSTER_UI_WIRE_H
#define VOYEMU_CLUSTER_UI_WIRE_H

#define QGFX_KANZI_UI_MAGIC 0x31444955495a4e4bULL
#define QGFX_KANZI_UI_BYTES 4096
#define QGFX_KANZI_UI_DESCRIPTOR_OFFSET 200
#define QGFX_KANZI_UI_DESCRIPTOR_WORD 3
#define QGFX_KANZI_UI_PID_OFFSET 240
#define QGFX_KANZI_UI_INCARNATION_OFFSET 248
#define QGFX_KANZI_UI_MAX_NAME 120
#define QGFX_KANZI_UI_MAX_STRING 255
#define QGFX_KANZI_UI_ROOT (~0ULL)
#define QGFX_KANZI_UI_VISIBLE 1u
#define QGFX_KANZI_UI_ENABLED 2u
#define QGFX_KANZI_UI_NAME_UNAVAILABLE 4u
#define QGFX_KANZI_UI_PROPERTIES 6
#define QGFX_KANZI_UI_PROPERTY_TEXT 0
#define QGFX_KANZI_UI_PROPERTY_VISIBLE 1
#define QGFX_KANZI_UI_PROPERTY_ENABLED 2
#define QGFX_KANZI_UI_PROPERTY_OPACITY 3
#define QGFX_KANZI_UI_PROPERTY_ACTUAL_WIDTH 4
#define QGFX_KANZI_UI_PROPERTY_ACTUAL_HEIGHT 5
/* Property kinds: a std::string, a one-byte bool or an IEEE binary32 float. */
#define QGFX_KANZI_UI_KIND_STRING 1u
#define QGFX_KANZI_UI_KIND_BOOL 2u
#define QGFX_KANZI_UI_KIND_FLOAT 3u
#define QGFX_KANZI_UI_UNSET 0u
#define QGFX_KANZI_UI_VALUE 1u
#define QGFX_KANZI_UI_TOO_LONG 2u
#define QGFX_KANZI_UI_UNAVAILABLE 3u
#define QGFX_KANZI_UI_NON_FINITE 4u
#define QGFX_KANZI_UI_COMPLETE 0u
#define QGFX_KANZI_UI_BYTE_BUDGET 1u
#define QGFX_KANZI_UI_TIME_BUDGET 2u
#define QGFX_KANZI_UI_ALLOCATION_FAILED 3u
#define QGFX_KANZI_UI_PIN_FAILED 4u
#define QGFX_KANZI_UI_PHYSICAL_FAILED 5u
#define QGFX_KANZI_UI_CYCLE 6u
#define QGFX_KANZI_UI_SOURCE_UNAVAILABLE 7u
#define QGFX_KANZI_UI_PAYLOAD (QGFX_KANZI_UI_BYTES - 32)

/* Little-endian AArch64 wire. The constant control page survives every capture;
 * the independently pinned data pages form a reusable high-water pool, never
 * unpinned while the producer lives. All metadata and payload mutations are
 * bracketed by the control page's odd/even sequence. Heartbeat and latest
 * present deliberately advance independently of that sequence. */
typedef struct {
    unsigned long long magic;
    unsigned schema, header_bytes;
    unsigned long long sequence, guest_pid, incarnation, guest_monotonic_ns, capture_finished_ns,
            heartbeat_ns, presented_ns, first_page, used, pages, count;
    unsigned reason, window_width, window_height, reserved_word;
    unsigned long long capture_budget_bytes, capture_budget_ns;
    unsigned char reserved[QGFX_KANZI_UI_BYTES - 136];
} QgfxKanziUiPage;

typedef struct {
    unsigned long long sequence, ordinal, next;
    unsigned used, reserved;
    unsigned char payload[QGFX_KANZI_UI_PAYLOAD];
} QgfxKanziUiData;
#endif
