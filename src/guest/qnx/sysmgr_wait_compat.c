/* Loaded only by the prepared sysmgrtool launcher; the OEM ELF is retained.
 * OEM libutilite initializes a 48-byte semaphore: mutex+32, condvar+40.
 * Its wait omits mutex acquisition. Preserve other semaphore layouts/callers.
 * This corrects the immediate ownership error, not OEM's lost-signal race. */
#ifndef QSW_TEST
#define QSW_API(name) __asm__(#name)
#define QSW_INITIALIZER __attribute__((constructor))
#define QSW_EXPORT __attribute__((visibility("default")))
#else
#define QSW_API(name)
#define QSW_INITIALIZER
#define QSW_EXPORT
#endif
extern void *qsw_open(const char *, int) QSW_API(dlopen);
extern void *qsw_symbol(void *, const char *) QSW_API(dlsym);
extern int *qsw_errno(void) QSW_API(__get_errno_ptr);
#undef QSW_API
_Static_assert(sizeof(unsigned) == 4 && sizeof(void *) == 8, "QNX AArch64 semaphore ABI");
typedef int (*QswWait)(void *, unsigned);
typedef int (*QswMutex)(void *);
static QswWait qsw_original;
static QswMutex qsw_trylock, qsw_unlock;
int utl_thread_sem_wait(void *, unsigned);

QSW_INITIALIZER static void qsw_initialize(void) {
    int saved = *qsw_errno();
    void *utility = qsw_open("/lib64/libutilite.so.0.1", 2);
    void *libc = qsw_open("/lib64/libc.so.4", 2);
    QswWait original = utility ? (QswWait)qsw_symbol(utility, "utl_thread_sem_wait") : 0;

    qsw_original = original != utl_thread_sem_wait ? original : 0;
    qsw_trylock = libc ? (QswMutex)qsw_symbol(libc, "pthread_mutex_trylock") : 0;
    qsw_unlock = libc ? (QswMutex)qsw_symbol(libc, "pthread_mutex_unlock") : 0;
    *qsw_errno() = saved;
}

QSW_EXPORT int utl_thread_sem_wait(void *sem, unsigned milliseconds) {
    int entry_errno = *qsw_errno();

    if (!qsw_original) {
        return -1;
    }

    if (!sem || !qsw_trylock || !qsw_unlock) {
        return qsw_original(sem, milliseconds);
    }

    unsigned char *mutex = (unsigned char *)sem + 32;
    const volatile unsigned *words = (const volatile unsigned *)mutex;

    if (words[0] != 0x80000000U || words[1] != 0) {
        return qsw_original(sem, milliseconds);
    }

    int locked = qsw_trylock(mutex);
    *qsw_errno() = entry_errno;

    if (locked) {
        return qsw_original(sem, milliseconds);
    }

    int result = qsw_original(sem, milliseconds), native_errno = *qsw_errno();
    qsw_unlock(mutex);
    *qsw_errno() = native_errno;

    return result;
}
