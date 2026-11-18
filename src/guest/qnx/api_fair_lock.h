#ifndef QGFX_API_FAIR_LOCK_H
#define QGFX_API_FAIR_LOCK_H
extern int usleep(unsigned);
typedef struct {
    unsigned next, serving;
} ApiFairLock;

static inline void api_fair_acquire(ApiFairLock *lock) {
    const unsigned ticket = __atomic_fetch_add(&lock->next, 1, __ATOMIC_RELAXED);

    while (__atomic_load_n(&lock->serving, __ATOMIC_ACQUIRE) != ticket) {
        usleep(100);
    }
}

static inline void api_fair_release(ApiFairLock *lock) {
    __atomic_fetch_add(&lock->serving, 1, __ATOMIC_RELEASE);
}

#endif
