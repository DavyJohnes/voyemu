/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#include "helper_dispatch.h"
/* Observation-only worker, also usable by the existing graphics frontend. */
#ifndef QGFX_SETTING_OBSERVER
#define QGFX_SETTING_OBSERVER(poll) ((void)(poll))
#endif
static int trace_thread;
static unsigned trace_created, trace_stop;

static void *trace_observe(void *unused) {
    (void)unused;

    for (unsigned i = 0; i < 6000 && !__atomic_load_n(&trace_stop, __ATOMIC_ACQUIRE); i++) {
        QGFX_SETTING_OBSERVER(i);
        drain_mcd_trace();
        usleep(200000);
    }

    return 0;
}

QGFX_INITIALIZER static void start_trace_observer(void) {
    QGFX_HELPER_DISPATCH();
    const char *v = getenv("QGFX_MCD_OBSERVE");

    if (!v || v[0] != '1' || v[1]) {
        return;
    }

    if (!pthread_create(&trace_thread, 0, trace_observe, 0)) {
        __atomic_store_n(&trace_created, 1, __ATOMIC_RELEASE);
    }
}

__attribute__((destructor)) static void stop_trace_observer(void) {
    __atomic_store_n(&trace_stop, 1, __ATOMIC_RELEASE);

    if (__atomic_exchange_n(&trace_created, 0, __ATOMIC_ACQ_REL)) {
        pthread_join(trace_thread, 0);
    }
}

/* NOLINTEND(modernize-use-nullptr) */
