#ifndef QGFX_NATIVE_MCU_ASYNC_H
#define QGFX_NATIVE_MCU_ASYNC_H
#include "helper_dispatch.h"

/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */

/* Opt-in transport of the MCU adapters: an own worker fetches host revisions under the RPC fair
 * lock, while the original MCU_COM and RPCD_MAIN threads take only the cache lock, whose owners
 * copy or pack bounded memory. Only consumer-applied revisions are acknowledged on the next poll.
 * Each adapter defines native_async_poll. */
static unsigned native_async_lock, native_async_stop, native_async_created;
static int native_async_thread;
static u64 native_async_mcu_staged, native_async_wheel_staged;

static int native_mcu_enabled(void) {
    const char *flag = getenv("QGFX_NATIVE_MCU");

    return flag && flag[0] == '1' && !flag[1];
}

static int native_mcu_async_enabled(void) {
    const char *flag = getenv("QGFX_NATIVE_MCU_ASYNC");

    return flag && flag[0] == '1' && !flag[1];
}

static int native_cache_try(void) {
    unsigned expected = 0;

    return __atomic_compare_exchange_n(&native_async_lock, &expected, 1, 0, __ATOMIC_ACQUIRE,
                                       __ATOMIC_RELAXED);
}

static void native_cache_release(void) {
    __atomic_store_n(&native_async_lock, 0, __ATOMIC_RELEASE);
}

/* Cache owners only copy/pack bounded memory; no host RPC or RPC lock is held. */
static void native_cache_wait(void) {
    while (!native_cache_try()) {
        usleep(100);
    }
}

/* Fetches the host revision after the consumed one once the consumer applied every staged one,
 * and while a nonzero gate allows it; the RPC fair lock is held only around the host call. Returns
 * the new sequence with its payload in output, or 0. */
static u64 native_async_fetch(u32 op, u64 *args, unsigned count, const u64 *consumed,
                              const u64 *staged, const u64 *gate, void *output, unsigned bytes) {
    if (__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE) || !native_cache_try()) {
        return 0;
    }

    const u64 ack = *consumed;
    const unsigned fetch = *staged == ack && (!gate || *gate);
    native_cache_release();

    if (!fetch) {
        return 0;
    }

    args[0] = ack;
    acquire();
    u64 seq = call(op, args, count, 0, 0);
    unsigned valid = seq && seq > ack && seq < 0x100000000ULL && packet && !packet->fault &&
                     packet->out_len == bytes;

    if (valid) {
        copy(output, packet->data, bytes);
    }

    release();

    return valid ? seq : 0;
}

static void native_async_poll(void);

extern int pthread_create(int *, const void *, void *(*)(void *), void *),
        pthread_join(int, void **);

static void *native_async_worker(void *unused) {
    (void)unused;

    while (!__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE)) {
        native_async_poll();

        if (!__atomic_load_n(&native_async_stop, __ATOMIC_ACQUIRE)) {
            usleep(20000);
        }
    }

    return 0;
}

QGFX_INITIALIZER static void native_async_start(void) {
    QGFX_HELPER_DISPATCH();

    if (!native_mcu_enabled() || !native_mcu_async_enabled()) {
        return;
    }

    if (!pthread_create(&native_async_thread, 0, native_async_worker, 0)) {
        __atomic_store_n(&native_async_created, 1, __ATOMIC_RELEASE);
    }
}

#ifndef QGFX_NATIVE_MCU_DESTRUCTOR
#define QGFX_NATIVE_MCU_DESTRUCTOR __attribute__((destructor))
#endif

QGFX_NATIVE_MCU_DESTRUCTOR static void native_async_shutdown(void) {
    __atomic_store_n(&native_async_stop, 1, __ATOMIC_RELEASE);

    if (__atomic_exchange_n(&native_async_created, 0, __ATOMIC_ACQ_REL)) {
        pthread_join(native_async_thread, 0);
    }
}

/* NOLINTEND(modernize-use-nullptr) */
#endif
