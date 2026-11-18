/* Observe OEM traffic without waiting for the host while inside Mcdif. */
typedef struct {
    u32 message, source, destination, bytes, word0, word1, rc, pid;
} McdTrace;
_Static_assert(sizeof(McdTrace) == 32, "Mcd trace layout");
static McdTrace mcd_records[32];
static unsigned mcd_count, mcd_dropped;
static unsigned char mcd_lock;

int Mcdif_issue_msg(unsigned message, unsigned source, unsigned destination, void *data,
                    unsigned bytes) {
    void *library = dlopen("/lib64/libmcdif.so.0", 2);

    int (*native)(unsigned, unsigned, unsigned, void *,
                  unsigned) = library ? dlsym(library, "Mcdif_issue_msg") : 0;

    if (!native || native == Mcdif_issue_msg) {
        emit("QG3 MCD original-unavailable\n");

        return -1;
    }

    const char *flag = getenv("QGFX_MCD_TRACE");
    u32 language[2] = {0, 0};

    if (message == 24 && source >= 1 && source <= 32 && destination == 8 && data && bytes == 8) {
        copy(language, data, 8);
    }

    int language_event = message == 24 && source >= 1 && source <= 32 && destination == 8 && data &&
                         bytes == 8 && language[0] == 3 && language[1] <= 1;
    int legacy_event = message >= 1 && message <= 23 &&
                       (source == 6 || destination == 6 || source == 17 || destination == 17 ||
                        source == 7 || destination == 7);
    int selected = flag && flag[0] == '1' && !flag[1] && (legacy_event || language_event);
    McdTrace record = {message, source, destination, bytes, 0, 0, 0, 0};

    if (selected && data && bytes >= 4) {
        copy(&record.word0, data, 4);
    }

    if (selected && data && bytes >= 8) {
        copy(&record.word1, (unsigned char *)data + 4, 4);
    }

    int rc = native(message, source, destination, data, bytes);

    if (selected) {
        record.rc = (u32)rc;
        record.pid = (u32)getpid();

        if (__atomic_test_and_set(&mcd_lock, __ATOMIC_ACQUIRE)) {
            __atomic_fetch_add(&mcd_dropped, 1, __ATOMIC_RELAXED);
        } else {
            if (mcd_count < 32) {
                mcd_records[mcd_count++] = record;
            } else {
                __atomic_fetch_add(&mcd_dropped, 1, __ATOMIC_RELAXED);
            }

            __atomic_clear(&mcd_lock, __ATOMIC_RELEASE);
        }
    }

    return rc;
}

static void drain_mcd_trace(void) {
    McdTrace batch[32];
    unsigned count;

    if (__atomic_test_and_set(&mcd_lock, __ATOMIC_ACQUIRE)) {
        return;
    }

    count = mcd_count;
    copy(batch, mcd_records, count * sizeof(McdTrace));
    mcd_count = 0;
    __atomic_clear(&mcd_lock, __ATOMIC_RELEASE);

    if (!count) {
        return;
    }

    acquire();
    u64 args[] = {count, __atomic_load_n(&mcd_dropped, __ATOMIC_RELAXED)};
    (void)observation_call(356, args, 2, batch, count * sizeof(McdTrace));
    release();
}
