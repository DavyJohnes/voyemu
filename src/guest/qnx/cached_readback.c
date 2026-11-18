/* Each EGL window owns one locked readback registration until destruction. */
typedef struct {
    void *memory;
    usize size;
    u64 blob;
} ReadbackCache;

static void cache_dispose(ReadbackCache *cache) {
    drop_blob(cache->blob);

    if (cache->memory) {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        qgfx_memory_release(cache->memory, cache->size);
#else
        munlock(cache->memory, cache->size);
        free(cache->memory);
#endif
    }

    *cache = (ReadbackCache){0};
}

static int cache_prepare(ReadbackCache *cache, usize bytes) {
    if (!bytes || bytes > MAX_BULK) {
        return 0;
    }

    if (cache->memory) {
        return cache->size == bytes && cache->blob != 0;
    }

#ifdef QGFX_PASSION_ROOT_IMAGEFS
    void *memory = qsm_allocate(bytes);
#else
    void *memory = malloc(bytes);
#endif

    if (!memory) {
        return 0;
    }

    unsigned char *p = memory;

    for (usize i = 0; i < bytes; i++) {
        p[i] = 0;
    }

#ifndef QGFX_PASSION_ROOT_IMAGEFS
    if (mlock(memory, bytes)) {
        free(memory);

        return 0;
    }
#endif
    u64 blob = register_memory(memory, bytes, 1);

    if (!blob) {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        qgfx_memory_release(memory, bytes);
#else
        munlock(memory, bytes);
        free(memory);
#endif

        return 0;
    }

    *cache = (ReadbackCache){memory, bytes, blob};

    return 1;
}
