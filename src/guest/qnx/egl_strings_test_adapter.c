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

#include "client_arrays.c"

void qgfx_test_reset_error(void) {
    transport_error = 0;
}

void qgfx_test_fail_transport(void) {
    transport_error = 1;
}

void qgfx_test_lose_download(void) {
    bulk_free();
}

int qgfx_test_error(void) {
    return transport_error;
}
