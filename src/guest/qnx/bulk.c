/* Transfer large buffers through owned locked copies and checked physical runs. */
extern void *malloc(usize);
extern void free(void *);
#define MAX_BULK ((usize)64 * 1024 * 1024)
static void *bulk_memory;
static usize bulk_size;

static int bulk_allocate(usize size) {
    if (!size || size > MAX_BULK) {
        return 0;
    }

#ifdef QGFX_PASSION_ROOT_IMAGEFS
    bulk_memory = qsm_allocate(size);
    bulk_size = size;

    if (!bulk_memory) {
        return 0;
    }
#else
    bulk_memory = malloc(size);
    bulk_size = size;

    if (!bulk_memory) {
        return 0;
    }
#endif
    unsigned char *p = bulk_memory;

    for (usize i = 0; i < size; i++) {
        p[i] = 0;
    }

#ifndef QGFX_PASSION_ROOT_IMAGEFS
    if (mlock(bulk_memory, size)) {
        free(bulk_memory);
        bulk_memory = 0;

        return 0;
    }
#endif

    return 1;
}

#ifdef QGFX_PASSION_ROOT_IMAGEFS
static void bulk_free(void) {
    if (bulk_memory) {
        qgfx_memory_release(bulk_memory, bulk_size);
        bulk_memory = 0;
        bulk_size = 0;
    }
}
#else
static void bulk_free(void) {
    if (bulk_memory) {
        munlock(bulk_memory, bulk_size);
        free(bulk_memory);
        bulk_memory = 0;
        bulk_size = 0;
    }
}
#endif

static u64 register_memory(void *memory, usize size, int direction) {
    if (!memory || !size || size > MAX_BULK || (direction != 0 && direction != 1)) {
        return 0;
    }

    u64 a[] = {size, (u64)direction};
    u64 id = call(40, a, 2, 0, 0);

    if (!id) {
        return 0;
    }

    usize offset = 0;

    while (offset < size) {
        u64 descriptors[480];
        unsigned count = 0;

        while (offset < size && count < 240) {
            i64 physical = -1;
            usize span = 0;
#ifdef QGFX_PASSION_ROOT_IMAGEFS
            if (mem_offset((unsigned char *)memory + offset, -1, size - offset, &physical, &span) ||
                !span || span > size - offset || physical < 0x40000000LL ||
                (u64)physical + span > 0x31c100000ULL) {
                transport_error = 1;
                goto failed;
            }
#else
            if (mem_offset((unsigned char *)memory + offset, -1, size - offset, &physical, &span) ||
                !span || span > size - offset || physical < 0x40000000LL ||
                (u64)physical + span > 0x240000000ULL) {
                transport_error = 1;
                goto failed;
            }
#endif
            descriptors[(u64)count * 2] = physical;
            descriptors[count * 2 + 1] = span;
            offset += span;
            count++;
        }

        u64 args[] = {id, count};

        if (!call(41, args, 2, descriptors, count * 16)) {
            goto failed;
        }
    }

    return id;
failed:
    {
        u64 args[] = {id};
        (void)call(42, args, 1, 0, 0);
    }

    return 0;
}

static u64 bulk_register(int direction) {
    return register_memory(bulk_memory, bulk_size, direction);
}

static u64 upload(const void *data, usize size) {
    if (!data || !size) {
        return 0;
    }

    if (!bulk_allocate(size)) {
        transport_error = 1;

        return 0;
    }

    copy(bulk_memory, data, size);
    u64 id = bulk_register(0);
    bulk_free();

    return id;
}

static u64 download_begin(usize size) {
    if (!bulk_allocate(size)) {
        transport_error = 1;

        return 0;
    }

    return bulk_register(1);
}

static void download_end(void *target, usize bytes) {
    if (target && bulk_memory && bytes <= bulk_size) {
        copy(target, bulk_memory, bytes);
    }

    bulk_free();
}

static void drop_blob(u64 id) {
    if (id) {
        u64 a[] = {id};
        (void)call(42, a, 1, 0, 0);
    }
}
