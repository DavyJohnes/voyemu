#include <assert.h>
#include <stdio.h>
#include <string.h>
#define QSW_TEST
#include "sysmgr_wait_compat.c"
static unsigned storage[12], before[12];
static int error_value, waits, locks, unlocks, try_result, native_result, native_error,
        self_resolve, require_owner;
static unsigned wait_ms;
static void *wait_sem;
static char events[16];
static unsigned event_count;

static void event(char c) {
    events[event_count++] = c;
    events[event_count] = 0;
}

static int native_wait(void *sem, unsigned ms) {
    assert(sem == wait_sem && ms == wait_ms && error_value == 17);
    waits++;
    event('W');
    error_value = native_error;

    return require_owner && storage[9] != 19 ? -1 : native_result;
}

static int native_trylock(void *mutex) {
    assert(mutex == (unsigned char *)wait_sem + 32);
    locks++;
    event('L');
    error_value = 81;

    if (!try_result) {
        storage[8]++;
        storage[9] = 19;
    }

    return try_result;
}

static int native_unlock(void *mutex) {
    assert(mutex == (unsigned char *)wait_sem + 32 && storage[9] == 19);
    unlocks++;
    event('U');
    storage[8]--;
    storage[9] = 0;
    error_value = 82;

    return 0;
}

void *qsw_open(const char *path, int mode) {
    assert(mode == 2);
    error_value = 83;

    return !strcmp(path, "/lib64/libutilite.so.0.1") ? (void *)1
           : !strcmp(path, "/lib64/libc.so.4")       ? (void *)2
                                                     : 0;
}

void *qsw_symbol(void *handle, const char *name) {
    error_value = 84;

    if (handle == (void *)1 && !strcmp(name, "utl_thread_sem_wait")) {
        return self_resolve ? (void *)utl_thread_sem_wait : (void *)native_wait;
    }

    if (handle == (void *)2 && !strcmp(name, "pthread_mutex_trylock")) {
        return (void *)native_trylock;
    }

    if (handle == (void *)2 && !strcmp(name, "pthread_mutex_unlock")) {
        return (void *)native_unlock;
    }

    return 0;
}

int *qsw_errno(void) {
    return &error_value;
}

static void reset(void) {
    memset(storage, 0, sizeof storage);
    storage[8] = 0x80000000U;
    memcpy(before, storage, sizeof storage);
    error_value = 17;
    waits = locks = unlocks = event_count = 0;
    events[0] = 0;
    try_result = native_result = require_owner = 0;
    native_error = 73;
    wait_sem = storage;
    wait_ms = 2000;
}

static void owned_wait(int result) {
    reset();
    native_result = result;
    assert(utl_thread_sem_wait(storage, 2000) == result);
    assert(waits == 1 && locks == 1 && unlocks == 1 && !strcmp(events, "LWU"));
    assert(error_value == 73 && !memcmp(storage, before, sizeof storage));
}

static void passthrough(void) {
    memcpy(before, storage, sizeof storage);
    assert(utl_thread_sem_wait(wait_sem, wait_ms) == native_result);
    assert(waits == 1 && unlocks == 0 && error_value == native_error);
    assert(!memcmp(storage, before, sizeof storage));
}

int main(void) {
    error_value = 17;
    qsw_initialize();
    assert(error_value == 17 && qsw_original && qsw_trylock && qsw_unlock);
    reset();
    require_owner = 1;
    assert(native_wait(storage, 2000) == -1);
    reset();
    require_owner = 1;
    assert(utl_thread_sem_wait(storage, 2000) == 0 && unlocks == 1 && error_value == 73);
    owned_wait(0);  /* Reply signaled while native wait is active. */
    owned_wait(-1); /* No reply: preserve original timeout/error result. */
    reset();
    event('P');
    native_result = -1; /* OEM early signal may be lost; do not invent a successful wait. */
    assert(utl_thread_sem_wait(storage, 2000) == -1 && !strcmp(events, "PLWU") &&
           error_value == 73);
    reset();
    try_result = 16;
    passthrough();
    assert(locks == 1 && !strcmp(events, "LW"));
    reset();
    try_result = 22;
    passthrough();
    assert(locks == 1);
    reset();
    storage[8] = 0x80000001U;
    storage[9] = 19;
    passthrough();
    assert(locks == 0);
    reset();
    storage[9] = 23;
    passthrough();
    assert(locks == 0);
    reset();
    storage[8] = 0;
    passthrough();
    assert(locks == 0);
    reset();
    wait_sem = 0;
    passthrough();
    assert(locks == 0);
    reset();
    wait_ms = 77;
    assert(utl_thread_sem_wait(storage, 77) == 0 && error_value == 73);
    reset();
    qsw_trylock = 0;
    passthrough();
    qsw_initialize();
    reset();
    self_resolve = 1;
    qsw_initialize();
    assert(error_value == 17 && !qsw_original);
    assert(utl_thread_sem_wait(storage, 2000) == -1 && !waits && !locks && error_value == 17);
    puts("PASS: owned wait ordering, early/during/no reply, passthrough, exact arguments, errno and self-resolution");

    return 0;
}
