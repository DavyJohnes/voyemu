/* Opt-in bounded replay of the last successful original OEM notification. */
extern char *getenv(const char *);
extern int getpid(void);
extern int access(const char *, int);
static unsigned rpc_last_a, rpc_last_b, rpc_seen, rpc_swaps, rpc_replays;
static unsigned char rpc_snapshot_lock;
static int (*rpc_original_state(void))(unsigned, unsigned) {
    void *lib = dlopen("/lib64/librpcif.so.0", 2);

    return lib ? (int (*)(unsigned, unsigned))dlsym(lib, "rpcif_update_clusterstate") : 0;
}

int rpcif_update_clusterstate(unsigned a, unsigned b) {
    int (*original)(unsigned, unsigned) = rpc_original_state();

    if (!original) {
        return -1;
    }

    int rc = original(a, b);
    static unsigned captured_reports;
    const char *enabled = getenv("QGFX_REPLAY_LAST_CLUSTERSTATE");

    if (enabled && enabled[0] == '1' && !enabled[1] && captured_reports++ < 4) {
        char line[160];
        snprintf(line, sizeof(line), "QG3 MCU capture original-a=%u original-b=%u rc=%d pid=%d\n",
                 a, b, rc, getpid());
        emit(line);
    }

    if (rc == 0) {
        while (__atomic_test_and_set(&rpc_snapshot_lock, __ATOMIC_ACQUIRE)) {
            usleep(100);
        }

        rpc_last_a = a;
        rpc_last_b = b;
        rpc_seen = 1;
        __atomic_clear(&rpc_snapshot_lock, __ATOMIC_RELEASE);
    }

    return rc;
}

static void refresh_native_mcu(void) {
    const char *enabled = getenv("QGFX_REPLAY_LAST_CLUSTERSTATE");

    if (!enabled || enabled[0] != '1' || enabled[1] || rpc_replays >= 2 || ++rpc_swaps % 4) {
        return;
    }

    static int previous_marker = -1;
    static unsigned marker_reports;
    int marker_ready = access("/log/qgfx-refresh-request", 0) == 0;

    if (previous_marker != marker_ready && marker_reports < 4) {
        char line[128];
        snprintf(line, sizeof(line), "QG3 MCU marker-ready=%d pid=%d\n", marker_ready, getpid());
        emit(line);
        previous_marker = marker_ready;
        marker_reports++;
    }

    if (!marker_ready) {
        return;
    }

    if (__atomic_test_and_set(&rpc_snapshot_lock, __ATOMIC_ACQUIRE)) {
        return;
    }

    unsigned seen = rpc_seen, a = rpc_last_a, b = rpc_last_b;
    __atomic_clear(&rpc_snapshot_lock, __ATOMIC_RELEASE);

    if (!seen) {
        static unsigned unavailable;

        if (!unavailable++) {
            emit("QG3 MCU pending=no-successful-original\n");
        }

        return;
    }

    int (*original)(unsigned, unsigned) = rpc_original_state();

    if (!original) {
        return;
    }

    rpc_replays++;
    int rc = original(a, b);
    char line[160];
    snprintf(line, sizeof(line), "QG3 MCU refresh=%u original-a=%u original-b=%u rc=%d\n",
             rpc_replays, a, b, rc);
    emit(line);
}
