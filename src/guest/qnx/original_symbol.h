#ifndef QGFX_ORIGINAL_SYMBOL_H
#define QGFX_ORIGINAL_SYMBOL_H

/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */

/* QNX dlopen adds a handle on every call and dlsym finds a handle by walking all of them under the
 * loader lock, so a lookup per call slows the calling OEM thread without bound. An original is
 * resolved once per process; a concurrent first call costs at most one more handle. */
static void *original_symbol(void **library, void **entry, const char *path, const char *name) {
    void *symbol = __atomic_load_n(entry, __ATOMIC_ACQUIRE);

    if (symbol) {
        return symbol;
    }

    void *handle = __atomic_load_n(library, __ATOMIC_ACQUIRE);

    if (!handle) {
        handle = dlopen(path, 2);

        if (!handle) {
            return 0;
        }

        __atomic_store_n(library, handle, __ATOMIC_RELEASE);
    }

    symbol = dlsym(handle, name);

    if (symbol) {
        __atomic_store_n(entry, symbol, __ATOMIC_RELEASE);
    }

    return symbol;
}

/* NOLINTEND(modernize-use-nullptr) */
#endif
