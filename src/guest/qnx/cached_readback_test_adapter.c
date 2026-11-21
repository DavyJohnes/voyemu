#define QGFX_WRAPPER_TEST 1
#define malloc qgfx_test_malloc
#define free qgfx_test_free
#define mlock qgfx_test_mlock
#define munlock qgfx_test_munlock
#define mem_offset qgfx_test_mem_offset
#define snprintf qgfx_test_snprintf
#define write qgfx_test_write
#define usleep qgfx_test_usleep
#include "core.c"

#include "cached_readback.c"
#include "client_arrays.c"

static ReadbackCache frame;

void qgfx_test_bind_element_buffer(unsigned buffer) {
    client_element_buffer = buffer;
}

void qgfx_test_fail_transport(void) {
    transport_error = 1;
}

/* The readback step of eglSwapBuffers; a null result fails the present. */
const void *qgfx_test_present(int width, int height) {
    acquire();
    int copied = cache_read(&frame, width, height);
    release();

    return copied ? frame.memory : 0;
}
