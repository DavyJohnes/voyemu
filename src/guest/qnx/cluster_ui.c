/* Read-only Gauge observation for the pinned A9 and A11 Kanzi images.
 * Called after the existing successful OEM Screen post on the rendering thread.
 * Only exported OEM methods are used; there are no Kanzi object or vtable
 * offsets. */
#include "cluster_ui_stream.h"
extern void *malloc(usize);
_Static_assert(sizeof(QgfxKanziUiData) == QGFX_KANZI_UI_BYTES, "Kanzi data page");
_Static_assert(sizeof(QgfxKanziUiPage) == QGFX_KANZI_UI_BYTES, "Kanzi snapshot page");
_Static_assert(__builtin_offsetof(Packet, reserved[QGFX_KANZI_UI_DESCRIPTOR_WORD]) ==
                       QGFX_KANZI_UI_DESCRIPTOR_OFFSET,
               "Kanzi descriptor");

_Static_assert(__builtin_offsetof(Packet, reserved[8]) == QGFX_KANZI_UI_PID_OFFSET, "Kanzi PID");
_Static_assert(__builtin_offsetof(Packet, reserved[9]) == QGFX_KANZI_UI_INCARNATION_OFFSET,
               "Kanzi incarnation");

typedef QgfxUiShared KanziShared;
static QgfxUiStream kanzi_stream;
#include "cluster_ui_properties.h"

/* The [abi:cxx11] tag proves a std::string property type, read the same way as
 * Node::NameProperty. The scalar descriptors are created as PropertyType<bool> and
 * PropertyType<float> in the pinned Kanzi library. Their final values are resolved values,
 * which may be inherited or fallback values rather than local assignments. */
static const char *const kanzi_property_symbols[QGFX_KANZI_UI_PROPERTIES] = {
        [QGFX_KANZI_UI_PROPERTY_TEXT] = "_ZN5kanzi16TextBlockConcept12TextPropertyB5cxx11E",
        [QGFX_KANZI_UI_PROPERTY_VISIBLE] = "_ZN5kanzi4Node15VisiblePropertyE",
        [QGFX_KANZI_UI_PROPERTY_ENABLED] = "_ZN5kanzi4Node15EnabledPropertyE",
        [QGFX_KANZI_UI_PROPERTY_OPACITY] = "_ZN5kanzi4Node15OpacityPropertyE",
        [QGFX_KANZI_UI_PROPERTY_ACTUAL_WIDTH] = "_ZN5kanzi4Node19ActualWidthPropertyE",
        [QGFX_KANZI_UI_PROPERTY_ACTUAL_HEIGHT] = "_ZN5kanzi4Node20ActualHeightPropertyE",
};

static unsigned char kanzi_sampling;
static QgfxKanziUiPage *kanzi_page;
static u64 kanzi_physical, kanzi_next_sample;
static int kanzi_init;

/* GNU shared_ptr getters return two words indirectly via x8 on AArch64.
 * Tail-branch to the getter, preserving its link register and caller ABI. */
__attribute__((naked)) static void kanzi_get_shared(void *function, const void *self, usize index,
                                                    KanziShared *out) {
    __asm__("mov x4,x0\nmov x0,x1\nmov x1,x2\nmov x8,x3\nbr x4");
}

static int kanzi_start_observer(void) {
    const char *enabled = getenv("QGFX_CLUSTER_UI");

    if (!enabled || enabled[0] != '1' || enabled[1]) {
        return 0;
    }

    void *program = dlopen(0, 2);
    void *kanzi = dlopen("/lib64/libkzcoreui.so", 2);
    void *gnu = dlopen("/usr/lib64/libstdc++.so.6", 2);

    if (!program || !kanzi || !gnu) {
        return 0;
    }

    kanzi_read = (KanziRead){
            .application = dlsym(program, "_Z14getApplicationv"),
            .engine = dlsym(kanzi, "_ZNK5kanzi11Application9getEngineEv"),
            .root = dlsym(kanzi, "_ZNK5kanzi11Application7getRootEv"),
            .child_count = dlsym(kanzi, "_ZNK5kanzi6Node2D13getChildCountEv"),
            .child = dlsym(kanzi, "_ZNK5kanzi6Node2D8getChildEm"),
            .release = dlsym(
                    kanzi, "_ZNSt16_Sp_counted_baseILN9__gnu_cxx12_Lock_policyE2EE10_M_releaseEv"),
            .visible = dlsym(kanzi, "_ZNK5kanzi4Node20isEffectivelyVisibleEv"),
            .enabled = dlsym(kanzi, "_ZNK5kanzi4Node20isEffectivelyEnabledEv"),
            .name_property = dlsym(kanzi, "_ZN5kanzi4Node12NamePropertyB5cxx11E"),
            .value = dlsym(
                    kanzi,
                    "_ZNK5kanzi4Node23getNodeFinalValueSourceERNS_30AbstractPropertyTypeDescriptorE"),
            .data = dlsym(gnu, "_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4dataEv"),
            .size = dlsym(gnu, "_ZNKSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEE4sizeEv"),
    };

    for (unsigned i = 0; i < QGFX_KANZI_UI_PROPERTIES; ++i) {
        const void *symbol = dlsym(program, kanzi_property_symbols[i]);
        kanzi_read.properties[i] = symbol ? symbol : dlsym(kanzi, kanzi_property_symbols[i]);
    }

    return kanzi_read.application && kanzi_read.engine && kanzi_read.root &&
           kanzi_read.child_count && kanzi_read.child && kanzi_read.release && kanzi_read.visible &&
           kanzi_read.enabled && kanzi_read.name_property && kanzi_read.value && kanzi_read.data &&
           kanzi_read.size;
}

/* The kernel reclaims the pinned pages with the Gauge process. Each page is checked and
 * addressed separately, so the block need not be physically contiguous. */
static int kanzi_physical_page(const void *memory, u64 *physical) {
    i64 address = -1;
    usize contiguous = 0;

    if (mem_offset(memory, -1, QGFX_KANZI_UI_BYTES, &address, &contiguous) ||
        contiguous < QGFX_KANZI_UI_BYTES || address < 0x40000000LL ||
        address > 0x240000000LL - QGFX_KANZI_UI_BYTES || address % QGFX_KANZI_UI_BYTES) {
        return 0;
    }

    *physical = (u64)address;

    return 1;
}

static u64 kanzi_clock(void *context) {
    (void)context;
    QgfxCloseTime clock = {0};

    return qgfx_close_clock(2, &clock) ? ~0ULL : clock.seconds * 1000000000ULL + clock.nanoseconds;
}

static void *kanzi_allocate(void *context, u64 size) {
    (void)context;

    return malloc((usize)size);
}

static void kanzi_deallocate(void *context, void *memory) {
    (void)context;
    free(memory);
}

static unsigned kanzi_pin(void *context, QgfxKanziUiData **out, u64 *physical) {
    (void)context;
    void *memory = 0;

    if (posix_memalign(&memory, QGFX_KANZI_UI_BYTES, QGFX_KANZI_UI_BYTES) || !memory) {
        return QGFX_KANZI_UI_ALLOCATION_FAILED;
    }

    if (mlock(memory, QGFX_KANZI_UI_BYTES)) {
        free(memory);

        return QGFX_KANZI_UI_PIN_FAILED;
    }

    /* Touch before mem_offset, as in the existing observer. */
    unsigned char *bytes = memory;

    for (unsigned i = 0; i < QGFX_KANZI_UI_BYTES; ++i) {
        bytes[i] = 0;
    }

    if (!kanzi_physical_page(memory, physical)) {
        munlock(memory, QGFX_KANZI_UI_BYTES);
        free(memory);

        return QGFX_KANZI_UI_PHYSICAL_FAILED;
    }

    *out = memory;

    return QGFX_KANZI_UI_COMPLETE;
}

static QgfxKanziUiPage *kanzi_map_page(void) {
    QgfxKanziUiData *memory = 0;

    if (kanzi_pin(0, &memory, &kanzi_physical)) {
        return 0;
    }

    QgfxKanziUiPage *page = (QgfxKanziUiPage *)memory;
    kanzi_stream.header = page;
    kanzi_stream.retained = QGFX_KANZI_UI_BYTES + sizeof(kanzi_stream);
    __atomic_store_n(&kanzi_page, page, __ATOMIC_RELEASE);

    return page;
}

static void kanzi_release_shared(void *context, QgfxUiShared node) {
    (void)context;

    if (node.control) {
        kanzi_read.release(node.control);
    }
}

static u64 kanzi_children(void *context, const void *node) {
    (void)context;

    return kanzi_read.child_count(node);
}

static QgfxUiShared kanzi_child(void *context, const void *node, u64 index) {
    (void)context;
    KanziShared child = {0};
    kanzi_get_shared(kanzi_read.child, node, (usize)index, &child);

    return child;
}

/* The already-running theme worker proves this Gauge process is still alive
 * even when its scene skips presentation. It never touches a scene object. */
static void kanzi_observer_heartbeat(void) {
    QgfxKanziUiPage *page = __atomic_load_n(&kanzi_page, __ATOMIC_ACQUIRE);

    if (!page) {
        return;
    }

    int *error = __get_errno_ptr();
    const int saved = *error;
    QgfxCloseTime clock = {0};

    if (!qgfx_close_clock(2, &clock)) {
        const u64 now = clock.seconds * 1000000000ULL + clock.nanoseconds;
        __atomic_store_n(&page->heartbeat_ns, now, __ATOMIC_RELEASE);
    }

    *error = saved;
}

static void kanzi_snapshot_locked(Packet *channel, u64 now, int width, int height) {
    if (!kanzi_init) {
        kanzi_init = kanzi_start_observer() ? 1 : -1;
    }

    if (kanzi_init < 0 || !channel || now < kanzi_next_sample) {
        return;
    }

    kanzi_next_sample = now + 250000000ULL;
    QgfxKanziUiPage *page = __atomic_load_n(&kanzi_page, __ATOMIC_ACQUIRE);

    if (!page) {
        page = kanzi_map_page();
    }

    if (!page) {
        kanzi_init = -1;

        return;
    }

    const u64 sequence = __atomic_load_n(&page->sequence, __ATOMIC_RELAXED);

    if (sequence > ~0ULL - 2) {
        kanzi_init = -1;

        return;
    }

    __atomic_store_n(&page->sequence, sequence + 1, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    page->magic = QGFX_KANZI_UI_MAGIC;
    page->schema = 1;
    page->header_bytes = 136;
    page->reason = QGFX_KANZI_UI_COMPLETE;
    page->first_page = page->pages = 0;
    page->capture_budget_bytes = 8ULL * 1024 * 1024;
    page->capture_budget_ns = 2000000ULL;
    kanzi_stream.ops = (QgfxUiOps){kanzi_clock, kanzi_allocate,       kanzi_deallocate,
                                   kanzi_pin,   kanzi_release_shared, kanzi_children,
                                   kanzi_child, kanzi_record};
    kanzi_stream.cursor = 0;
    kanzi_stream.budget = page->capture_budget_bytes;
    kanzi_stream.deadline = now + page->capture_budget_ns;
    kanzi_stream.sequence = sequence + 2;
    page->guest_pid = channel->reserved[8];
    page->incarnation = channel->reserved[9];
    page->guest_monotonic_ns = now;
    page->window_width = width > 0 ? (unsigned)width : 0;
    page->window_height = height > 0 ? (unsigned)height : 0;
    page->count = page->used = 0;
    KanziShared root = {0};
    const void *application = qgfx_ui_time(&kanzi_stream) ? kanzi_read.application() : 0;
    const void *engine = application && qgfx_ui_time(&kanzi_stream) ? kanzi_read.engine(application)
                                                                    : 0;

    if (engine && qgfx_ui_time(&kanzi_stream)) {
        kanzi_get_shared(kanzi_read.root, application, 0, &root);
    }

    (void)qgfx_ui_time(&kanzi_stream);
    qgfx_ui_capture(&kanzi_stream, root);
    page->capture_finished_ns = kanzi_clock(0);
    __atomic_store_n(&page->sequence, sequence + 2, __ATOMIC_RELEASE);
    __atomic_store_n(&channel->reserved[QGFX_KANZI_UI_DESCRIPTOR_WORD], kanzi_physical,
                     __ATOMIC_RELEASE);
}

/* Every successful post advances presented_ns, including posts the sampling interval skips, so
 * the host can tell whether the snapshot reflects the latest present. */
static void kanzi_snapshot(Packet *channel, int width, int height) {
    int *error = __get_errno_ptr();
    const int saved = *error;
    QgfxCloseTime clock = {0};

    if (qgfx_close_clock(2, &clock)) {
        *error = saved;

        return;
    }

    const u64 now = clock.seconds * 1000000000ULL + clock.nanoseconds;
    QgfxKanziUiPage *page = __atomic_load_n(&kanzi_page, __ATOMIC_ACQUIRE);

    if (page) {
        __atomic_store_n(&page->presented_ns, now, __ATOMIC_RELEASE);
    }

    if (!__atomic_test_and_set(&kanzi_sampling, __ATOMIC_ACQUIRE)) {
        kanzi_snapshot_locked(channel, now, width, height);
        __atomic_clear(&kanzi_sampling, __ATOMIC_RELEASE);
    }

    *error = saved;
}
