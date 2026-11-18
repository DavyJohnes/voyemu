#include "passion_ram64.h"
_Static_assert(sizeof(void *) == 8 && sizeof(unsigned long) == 8 && sizeof(Qpr64Physical) == 8,
               "QNX AArch64 ABI");
extern char *qpr64_getenv(const char *) __asm__("getenv");
extern int *qpr64_errno(void) __asm__("__get_errno_ptr");
extern void *qpr64_mmap(void *, Qpr64Size, int, int, int, Qpr64Physical) __asm__("mmap");

/* Only the command-local preload used by the pinned supplemental devb instance.
 * No constructor, RTLD_NEXT, fabricated success, or generic physical translation. */
void *mmap_device_memory(void *address, Qpr64Size length, int protection, int flags,
                         Qpr64Physical token) {
    return qpr64_bind(qpr64_getenv("VOYEMU_PASSION_RAM64"), address, length, protection, flags,
                      token, qpr64_mmap, qpr64_errno());
}
