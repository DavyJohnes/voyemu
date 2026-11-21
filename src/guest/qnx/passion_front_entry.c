#ifndef QGFX_PASSION_ROOT_IMAGEFS
#error Passion frontend requires its explicit adapter
#endif
/* Keep generated wrappers untouched; extend only the Passion procedure lookup. */
#define eglGetProcAddress qgfx_base_get_proc_address
#include "generated.c"
#undef eglGetProcAddress

Proc eglGetProcAddress(const char *name) {
    if (qbw_equal(name, "eglGetCurrentSurface")) {
        return (Proc)eglGetCurrentSurface;
    }

    Proc proc = qgfx_base_get_proc_address(name);
    static unsigned unknown_reports;

    /* A caller may resolve an entry point without a wrapper from the OEM driver, whose state the
     * host never sees; the first names are logged to trace such draws. */
    if (!proc && name && __atomic_fetch_add(&unknown_reports, 1, __ATOMIC_RELAXED) < 64) {
        char line[160];
        int n = snprintf(line, sizeof(line), "QG3 PROC unknown=%.96s level=DEBUG\n", name);

        if (n > 0) {
            write(2, line, (usize)n < sizeof(line) ? (usize)n : sizeof(line) - 1);
        }
    }

    return proc;
}
