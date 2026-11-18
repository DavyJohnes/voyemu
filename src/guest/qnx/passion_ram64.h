#ifndef VOYEMU_PASSION_RAM64_H
#define VOYEMU_PASSION_RAM64_H
/* Developer-only, one pinned devb-ram invocation. No allocation or policy change. */
typedef unsigned long long Qpr64Physical;
typedef unsigned long Qpr64Size;
typedef void *(*Qpr64Map)(void *, Qpr64Size, int, int, int, Qpr64Physical);

static int qpr64_equal(const char *a, const char *b) {
    if (!a || !b) {
        return 0;
    }

    for (unsigned i = 0; i < 96; ++i) {
        if (a[i] != b[i]) {
            return 0;
        }

        if (!a[i]) {
            return 1;
        }
    }

    return 0;
}

static void *qpr64_bind(const char *binding, void *address, Qpr64Size length, int protection,
                        int flags, Qpr64Physical token, Qpr64Map real_map, int *error) {
    const Qpr64Physical physical = 0x27c008000ULL, container_end = 0x31c00c400ULL,
                        ram_end = 0x31c100000ULL;

    if (!error) {
        return (void *)-1;
    }

    if (!binding) {
        if (!real_map) {
            *error = 22;

            return (void *)-1;
        }

        return real_map(address, length, protection,
                        (int)(((unsigned)flags & 0xfffefff0U) | 0x10001U), -1, token);
    }

    /* Historical RAM64 ABI binding stays stable across canonical profile renames. */
    if (!qpr64_equal(binding, "passion-20260403-2.1.4.80-system-ram64-v1")) {
        *error = 1;

        return (void *)-1;
    }

    if (address || length != 0x4b28e000UL || protection != 0xb00 || flags ||
        token != 0x7c008000ULL || !real_map || physical > container_end ||
        length > container_end - physical || container_end > ram_end) {
        *error = 22;

        return (void *)-1;
    }

    /* QNX7 documented MAP_PHYS|MAP_SHARED and NOFD; real mmap enforces abilities. */
    return real_map(address, length, protection, 0x10001, -1, physical);
}

#endif
