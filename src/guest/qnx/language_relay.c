/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */

/* QNX cluster language relay.
 *
 * Evidence: mcd_trace.c records the OEM cluster language change as mcd message
 * 24, source 1..32, destination 8, bytes 8, word0 == 3, word1 <= 1; theme_relay.c
 * issues the same message with word0 == 4, proving the
 * engnCore_issue2mcd(24,{setting,value},8,8) argument ABI. This relay reuses that
 * verified route via op 365 (poll command) / op 366 (report submission).
 *
 * Opcode discipline: 353 (glShaderSource bulk) and 354 (OEM Screen stream)
 * are already owned. 365/366 are unused and are dispatched as explicit
 * opcodes, never by argument count.
 *
 * Truthfulness: rc == 0 proves only that the original setting request was
 * submitted. No OEM language getter is proven on this image, so this relay never
 * claims the applied/observed effect; the host records request_issued and the
 * visible cluster change is established separately from captured pixels. */
static void relay_language(void) {
    static u64 acknowledged;
    const char *flag = getenv("QGFX_LANGUAGE_RELAY");

    if (!flag || flag[0] != '1' || flag[1]) {
        return;
    }

    acquire();
    u64 args[] = {acknowledged};
    u64 seq = call(365, args, 1, 0, 0);
    const unsigned valid = packet && packet->out_len == 1;
    unsigned value = valid ? packet->data[0] : 0;
    release();

    if (!seq || !valid || seq <= acknowledged || value > 1) {
        return;
    }

    void *lib = dlopen("/lib64/libenginecore.so.0", 2);
    int (*issue)(unsigned, void *, unsigned,
                 unsigned) = lib ? (int (*)(unsigned, void *, unsigned,
                                            unsigned))dlsym(lib, "_Z18engnCore_issue2mcdjPvjj")
                                 : 0;

    if (!issue) {
        emit("QG3 LANGUAGE unavailable=original-setting-api\n");

        return;
    }

    unsigned request[] = {3, value};
    int rc = issue(24, request, 8, 8);
    char line[160];
    snprintf(line, sizeof(line), "QG3 LANGUAGE seq=%llu value=%u native-setting-rc=%d pid=%d\n",
             seq, value, rc, getpid());
    emit(line);
    acquire();
    u64 report[] = {seq, (u64)(rc == 0)};
    call(366, report, 2, 0, 0);
    release();

    if (rc == 0) {
        acknowledged = seq;
    }
}

/* QNX pthread_t is _Int32t in the pinned OEM DWARF. */
extern int pthread_create(int *, const void *, void *(*)(void *), void *);
extern int pthread_join(int, void **);
extern int usleep(unsigned);
static int language_thread;
static unsigned char language_started;
static unsigned language_created, language_stop;

static void *language_worker(void *unused) {
    (void)unused;
    unsigned iteration = 0;

    while (!__atomic_load_n(&language_stop, __ATOMIC_ACQUIRE)) {
        relay_language();

        // The host command is rare; poll slowly and never compete with OEM asset loading.
        if (iteration % 10 == 0) {
            usleep(20000);
        } else {
            usleep(200000);
        }

        iteration++;
    }

    return 0;
}

static void start_language_worker(void) {
    const char *flag = getenv("QGFX_LANGUAGE_RELAY");

    if (!flag || flag[0] != '1' || flag[1] ||
        __atomic_test_and_set(&language_started, __ATOMIC_ACQUIRE)) {
        return;
    }

    int rc = pthread_create(&language_thread, 0, language_worker, 0);

    if (rc) {
        __atomic_clear(&language_started, __ATOMIC_RELEASE);
        emit("QG3 LANGUAGE worker-start-failed\n");

        return;
    }

    __atomic_store_n(&language_created, 1, __ATOMIC_RELEASE);
}

__attribute__((destructor)) static void stop_language_worker(void) {
    __atomic_store_n(&language_stop, 1, __ATOMIC_RELEASE);

    if (__atomic_exchange_n(&language_created, 0, __ATOMIC_ACQ_REL)) {
        pthread_join(language_thread, 0);
    }
}

/* NOLINTEND(modernize-use-nullptr) */
