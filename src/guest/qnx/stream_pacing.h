#ifndef QGFX_STREAM_PACING_H
#define QGFX_STREAM_PACING_H
#define STREAM_FRAME_NS 16666667ULL
#define STREAM_FALLBACK_US 16667U
typedef struct {
    u64 deadline, last;
    unsigned valid, published;
} StreamPacing;
typedef struct {
    unsigned delay_us, failed;
} StreamPacingWait;

static int stream_time_ns(u64 seconds, u64 nanoseconds, u64 *result) {
    if (nanoseconds >= 1000000000ULL || seconds > (~(u64)0 - nanoseconds) / 1000000000ULL) {
        return 0;
    }

    *result = seconds * 1000000000ULL + nanoseconds;

    return 1;
}

static StreamPacingWait stream_pacing_wait(const StreamPacing *pacing, int clock_ok, u64 now) {
    StreamPacingWait wait = {STREAM_FALLBACK_US, 1};

    if (!clock_ok || now > ~(u64)0 - STREAM_FRAME_NS || (pacing->valid && now < pacing->last)) {
        return wait;
    }

    if (!pacing->valid) {
        if (!pacing->published) {
            wait.delay_us = 0;
            wait.failed = 0;
        }

        return wait;
    }

    if (now >= pacing->deadline) {
        wait.delay_us = 0;
        wait.failed = 0;

        return wait;
    }

    u64 remaining = pacing->deadline - now;

    if (remaining > STREAM_FRAME_NS) {
        return wait;
    }

    wait.delay_us = (unsigned)((remaining + 999) / 1000);
    wait.failed = 0;

    return wait;
}

static int stream_pacing_commit(StreamPacing *pacing, int clock_ok, u64 now) {
    pacing->published = 1;

    if (!clock_ok || now > ~(u64)0 - STREAM_FRAME_NS || (pacing->valid && now < pacing->last)) {
        pacing->valid = 0;

        return 0;
    }

    pacing->last = now;
    pacing->deadline = now + STREAM_FRAME_NS;
    pacing->valid = 1;

    return 1;
}

#endif
