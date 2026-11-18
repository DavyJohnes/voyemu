/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#include "helper_dispatch.h"
/* Read exported OEM getters only; never drive the manager FSM. */
static int manager_thread;
static unsigned manager_created, manager_stop;

static void *manager_observer(void *unused) {
    (void)unused;
    void *main = dlopen(0, 2);
    unsigned (*state)(void) = main ? (unsigned (*)(void))dlsym(main, "themmgr_getState") : 0;
    unsigned (*mask)(void) =
            main ? (unsigned (*)(void))dlsym(main, "themmgr_GetModuleDataPrepareFlag") : 0;

    if (!state || !mask) {
        emit("QG3 MANAGER getters-unavailable\n");

        return 0;
    }

    unsigned last_state = ~0u, last_mask = ~0u;

    for (unsigned i = 0; i < 3000 && !__atomic_load_n(&manager_stop, __ATOMIC_ACQUIRE); i++) {
        unsigned s = state(), m = mask();

        if (s == state() && m == mask() && (s != last_state || m != last_mask)) {
            acquire();
            u64 a[] = {s, m, (u64)getpid(), i};
            u64 ok = observation_call(359, a, 4, 0, 0);
            release();

            if (ok) {
                last_state = s;
                last_mask = m;
            }
        }

        drain_mcd_trace();
        usleep(200000);
    }

    return 0;
}

QGFX_INITIALIZER static void start_manager_observer(void) {
    QGFX_HELPER_DISPATCH();
    const char *v = getenv("QGFX_MANAGER_OBSERVE");

    if (!v || v[0] != '1' || v[1]) {
        return;
    }

    int rc = pthread_create(&manager_thread, 0, manager_observer, 0);

    if (rc) {
        emit("QG3 MANAGER worker-start-failed\n");

        return;
    }

    __atomic_store_n(&manager_created, 1, __ATOMIC_RELEASE);
}

__attribute__((destructor)) static void stop_manager_observer(void) {
    __atomic_store_n(&manager_stop, 1, __ATOMIC_RELEASE);

    if (__atomic_exchange_n(&manager_created, 0, __ATOMIC_ACQ_REL)) {
        pthread_join(manager_thread, 0);
    }
}

/* NOLINTEND(modernize-use-nullptr) */
