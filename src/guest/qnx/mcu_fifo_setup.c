/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#include "helper_dispatch.h"

/* QNX OEM libc owns FIFO creation; the private /log path is guarded in ksh. */
QGFX_INITIALIZER static void setup_mcu_fifo(void) {
    QGFX_HELPER_DISPATCH();
    const char *flag = getenv("QGFX_MCU_FIFO_SETUP");

    if (!flag || flag[0] != '1' || flag[1]) {
        return;
    }

    void *lib = dlopen("/lib64/libc.so.4", 2);
    int (*native)(const char *, unsigned) = lib ? dlsym(lib, "mkfifo") : 0;

    if (!native || native("/log/qgfx-mcu-input", 0600)) {
        emit("QG3 MCU fifo-setup-failed\n");
    } else {
        emit("QG3 MCU fifo-created\n");
    }
}

/* NOLINTEND(modernize-use-nullptr) */
