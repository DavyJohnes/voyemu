/* One owned locked texture input per producer; pixels are refreshed before every reuse. */
typedef struct {
    void *memory;
    usize capacity;
    u64 blob;
} TextureUploadCache;
static TextureUploadCache texture_upload_cache;

static void texture_upload_dispose(void) {
    TextureUploadCache *cache = &texture_upload_cache;

    drop_blob(cache->blob);

    if (cache->memory) {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        qgfx_memory_release(cache->memory, cache->capacity);
#else
        munlock(cache->memory, cache->capacity);
        free(cache->memory);
#endif
    }

    *cache = (TextureUploadCache){0};
}

static u64 texture_upload(const void *data, usize size) {
    if (!data || !size || size > MAX_BULK) {
        return 0;
    }

    TextureUploadCache *cache = &texture_upload_cache;

    if (cache->memory && size <= cache->capacity) {
        copy(cache->memory, data, size);
        u64 args[] = {cache->blob, size};

        if (call(43, args, 2, 0, 0) == 1) {
            return cache->blob;
        }

        transport_error = 1;
        texture_upload_dispose();

        return 0;
    }

    texture_upload_dispose();
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    void *memory = qsm_allocate(size);
#else
    void *memory = malloc(size);
#endif

    if (!memory) {
        transport_error = 1;

        return 0;
    }

    copy(memory, data, size);
#ifndef QGFX_PASSION_ROOT_IMAGEFS
    if (mlock(memory, size)) {
        free(memory);
        transport_error = 1;

        return 0;
    }
#endif
    u64 blob = register_memory(memory, size, 0);

    if (!blob) {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        qgfx_memory_release(memory, size);
#else
        munlock(memory, size);
        free(memory);
#endif
        transport_error = 1;

        return 0;
    }

    *cache = (TextureUploadCache){memory, size, blob};

    return blob;
}
