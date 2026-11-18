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

    return qgfx_base_get_proc_address(name);
}
