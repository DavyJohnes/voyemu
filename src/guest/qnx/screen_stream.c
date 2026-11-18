/* The Screen exporter probes only host challenged identities, never target RPC pages. */
#ifndef QGFX_SCREEN_LIVENESS_TEST
extern int stream_owner_kill(int, int) __asm__("kill");
#else
extern int stream_owner_kill(int, int);
#endif
static u64 stream_owner_reported_generation, stream_owner_reported_challenge;

static void stream_owner_liveness(void) {
    u64 census[44];
    unsigned bytes = 0;

    acquire();
    u64 ok = call(367, 0, 0, 0, 0);

    if (ok && packet && packet->out_len <= sizeof(census)) {
        bytes = packet->out_len;
        copy(census, packet->data, bytes);
    }

    release();

    if (bytes < 32 || bytes % 8 || census[0] != 1 || !census[1] || !census[2] || census[3] > 8 ||
        bytes != 32 + census[3] * 40) {
        return;
    }

    if (census[1] == stream_owner_reported_generation &&
        census[2] == stream_owner_reported_challenge) {
        return;
    }

    /* Validate the complete census before any PID probe or death report. */
    for (u64 i = 0; i < census[3]; i++) {
        const u64 *owner = census + 4 + i * 5;

        if (!owner[0] || !owner[1] || (owner[1] & 4095) || !owner[2] || !owner[3] ||
            owner[3] > 0x7fffffffULL || !owner[4]) {
            return;
        }
    }

    for (u64 i = 0; i < census[3]; i++) {
        const u64 *owner = census + 4 + i * 5;
        const int rc = stream_owner_kill((int)owner[3], 0), error = *__get_errno_ptr();

        if (rc != -1 || error != 3) {
            continue; /* EPERM, EINTR and a reused live PID prove no death. */
        }

        u64 args[] = {census[1], census[2], owner[0], owner[1], owner[2],
                      owner[3],  owner[4],  (u64)-1,  3};
        stream_owner_reported_generation = census[1];
        stream_owner_reported_challenge = census[2];
        acquire();
        (void)call(368, args, 9, 0, 0);
        release();

        return; // One receipt consumes this challenge.
    }
}

static void stream_owner_liveness_after_export(unsigned exported) {
    if (exported) {
        stream_owner_liveness();
    }
}

#ifndef QGFX_SCREEN_LIVENESS_TEST
#include "stream_pacing.h"
/* Verified OEM AArch64 libc: monotonic clock id 2; two 64-bit timespec fields. */
typedef struct {
    u64 seconds, nanoseconds;
} StreamTimespec;
_Static_assert(sizeof(StreamTimespec) == 16 && __builtin_offsetof(StreamTimespec, nanoseconds) == 8,
               "QNX AArch64 timespec ABI");
extern int clock_gettime(int, StreamTimespec *);

static int stream_monotonic_ns(u64 *result) {
    StreamTimespec time;

    return clock_gettime(2, &time) == 0 && stream_time_ns(time.seconds, time.nanoseconds, result);
}

/* Keep the original screenshot's pixmap alive and relay repeated OEM reads. */
static void *captured_display, *captured_buffer;
static unsigned screen_stream_exported;

static unsigned stream_limit(const char *text) {
    if (!text || !*text) {
        return 0;
    }

    unsigned n = 0;

    for (unsigned i = 0; text[i]; i++) {
        if (i >= 6 || text[i] < '0' || text[i] > '9') {
            return 0;
        }

        n = n * 10 + (unsigned)(text[i] - '0');
    }

    return n >= 1 && n <= 216000 ? n : 0;
}

static int stream_continuous(const char *text) {
    static const char expected[] = "continuous";

    if (!text) {
        return 0;
    }

    for (unsigned i = 0; i < sizeof(expected); i++) {
        if (text[i] != expected[i]) {
            return 0;
        }
    }

    return 1;
}

static int stream_frame_enabled(u64 frame, unsigned limit, int continuous) {
    return frame > 0 && frame < 0x100000000ULL && (continuous || frame <= limit);
}

static void *screen_original(const char *name) {
    static void *library;

#ifdef QGFX_PASSION_ROOT_IMAGEFS
    if (!library) {
        library = dlopen("/lib64/libscreen.so.1", 2);
    }
#else
    if (!library) {
        library = dlopen("/proc/boot/lib64/libscreen.so.1", 2);
    }
#endif

    return library ? dlsym(library, name) : 0;
}

static void factory_display_once(void);

int screen_read_display(void *display, void *buffer, int count, const int *rects, int flags) {
    int (*native)(void *, void *, int, const int *, int) = screen_original("screen_read_display");

    if (!native || native == screen_read_display) {
        return -1;
    }

    ivc_probe_once();
    factory_display_once(); /* OEM factory self-check switch-on before this frame flows. */
    int rc = native(display, buffer, count, rects, flags);

    if (!rc && (stream_limit(getenv("QGFX_SCREEN_STREAM")) ||
                stream_continuous(getenv("QGFX_SCREEN_STREAM")))) {
        captured_display = display;
        captured_buffer = buffer;
    }

    return rc;
}

static u32 stream_u32(const unsigned char *p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static int stream_header(const unsigned char *p) {
    return p[0] == 'B' && p[1] == 'M' && stream_u32(p + 10) == 54 && stream_u32(p + 14) == 40 &&
           stream_u32(p + 18) == 1920 && stream_u32(p + 22) == (u32)-720 && p[26] == 1 && !p[27] &&
           p[28] == 32 && !p[29] && !stream_u32(p + 30);
}

int screen_destroy_pixmap(void *pixmap) {
    int (*destroy)(void *) = screen_original("screen_destroy_pixmap");

    if (!destroy || destroy == screen_destroy_pixmap) {
        return -1;
    }

    unsigned frames = stream_limit(getenv("QGFX_SCREEN_STREAM"));
    int continuous = stream_continuous(getenv("QGFX_SCREEN_STREAM"));
    int (*get_pixmap)(void *, int, void **) = screen_original("screen_get_pixmap_property_pv");
    int (*get_pointer)(void *, int, void **) = screen_original("screen_get_buffer_property_pv");
    int (*get_integer)(void *, int, int *) = screen_original("screen_get_buffer_property_iv");
    int (*capture)(void *, void *, int, const int *, int) = screen_original("screen_read_display");
    void *buffer = 0, *pixels = 0;
    int stride = 0, format = 0;

    if ((!frames && !continuous) || !captured_display || !captured_buffer || !get_pixmap ||
        !get_pointer || !get_integer || !capture) {
        return destroy(pixmap);
    }

    if (get_pixmap(pixmap, 37, &buffer) || buffer != captured_buffer ||
        get_pointer(buffer, 34, &pixels) || !pixels || get_integer(buffer, 44, &stride) ||
        stride < 7680 || stride > 65536 || get_integer(buffer, 14, &format) || format != 8) {
        emit("QG3 STREAM fail=buffer\n");

        return destroy(pixmap);
    }

    unsigned char header[54];
    int fd = open("/log/oem-screen.bmp", 0);

    if (fd < 0) {
        return destroy(pixmap);
    }

    long file_size = lseek(fd, 0, 2);
    int valid = file_size == 5529654 && lseek(fd, 0, 0) == 0 && read(fd, header, 54) == 54;
    close(fd);

    if (!valid || !stream_header(header)) {
        emit("QG3 STREAM fail=header\n");

        return destroy(pixmap);
    }

    usize bytes = 5529654;
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    unsigned char *memory = qsm_allocate(bytes);
#else
    unsigned char *memory = malloc(bytes);
#endif

    if (!memory) {
        return destroy(pixmap);
    }

    extern void *memcpy(void *, const void *, usize);
    extern void *memset(void *, int, usize);
    memset(memory, 0, bytes);
    memcpy(memory, header, 54);
    /* Touch and lock the complete owned transfer once; never register arbitrary RAM. */
#ifndef QGFX_PASSION_ROOT_IMAGEFS
    if (mlock(memory, bytes)) {
        free(memory);

        return destroy(pixmap);
    }
#endif
    acquire();
    u64 blob = register_memory(memory, bytes, 0);
    release();
    StreamPacing pacing = {0};
    unsigned pacing_warned = 0;

    if (blob) {
        for (u64 frame = 1; stream_frame_enabled(frame, frames, continuous); frame++) {
            stream_owner_liveness_after_export(screen_stream_exported);

            if (capture(captured_display, buffer, 0, 0, 0)) {
                break;
            }

            for (unsigned y = 0; y < 720; y++) {
                memcpy(memory + 54 + (usize)y * 7680, (unsigned char *)pixels + (usize)y * stride,
                       7680);
            }

            u64 now = 0;
            int clock_ok = stream_monotonic_ns(&now);
            StreamPacingWait wait = stream_pacing_wait(&pacing, clock_ok, now);

            if (wait.failed && !pacing_warned) {
                pacing_warned = 1;
                emit("QG3 STREAM clock-fallback=1\n");
            }

            if (wait.delay_us) {
                usleep(wait.delay_us);
            }

            acquire();
            clock_ok = stream_monotonic_ns(&now);

            if (!stream_pacing_commit(&pacing, clock_ok, now) && !pacing_warned) {
                pacing_warned = 1;
                emit("QG3 STREAM clock-fallback=1\n");
            }

            u64 args[] = {blob, bytes, frame};
            u64 ok = call(354, args, 3, 0, 0);
            release();

            if (!ok) {
                break;
            }

            screen_stream_exported++;
        }
    }

    acquire();
    drop_blob(blob);
    release();
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    qgfx_memory_release(memory, bytes);
#else
    munlock(memory, bytes);
    free(memory);
#endif

    char line[96];
    snprintf(line, sizeof(line), "QG3 STREAM exported=%u\n", screen_stream_exported);
    emit(line);

    return destroy(pixmap);
}
#endif
