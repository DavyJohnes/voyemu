/* Read the original enginecore context; never write an input or UI property. */
extern int getpid(void);

static void observe_ign(void) {
    static unsigned reports, calls;

    if (reports >= 32) {
        return;
    }

    const unsigned previous_calls = calls++;

    if (previous_calls >= 8 && calls % 4) {
        return;
    }

    reports++;
    void *library = dlopen("/lib64/libenginecore.so.0", 2);
    void *(*instance)(void) =
            library ? (void *(*)(void))dlsym(library, "_ZN12CContextData11getInstanceEv") : 0;
    int (*query)(void *, unsigned, void *, unsigned) =
            library ? (int (*)(void *, unsigned, void *, unsigned))dlsym(
                              library, "_ZN12CContextData7getDataEjPvj")
                    : 0;

    if (!instance || !query) {
        emit("QG3 INPUT unavailable=enginecore-symbol\n");

        return;
    }

    void *context = instance();

    if (!context) {
        emit("QG3 INPUT unavailable=context\n");

        return;
    }

    const unsigned keys[] = {0, 302, 303, 305, 310, 351};

    for (unsigned i = 0; i < 6; i++) {
        unsigned data[4] = {0xfeed0001, 0xfeed0002, 0xfeed0003, 0xfeed0004};
        int rc = query(context, keys[i], data, 16);
        float speed = 0;

        for (unsigned j = 0; j < sizeof(speed); j++) {
            ((unsigned char *)&speed)[j] = ((unsigned char *)data)[j];
        }

        char line[192];
        snprintf(line, sizeof(line),
                 "QG3 INPUT key=%u rc=%d raw=%u float=%.3f words=%x,%x,%x,%x pid=%d\n", keys[i], rc,
                 data[0], (double)speed, data[0], data[1], data[2], data[3], getpid());
        emit(line);
    }
}
