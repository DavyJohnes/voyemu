#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint64_t u64;
struct Packet {
    unsigned out_len;
    unsigned char data[3840];
};
static struct Packet fixture, *packet = &fixture;
static int error_value, probe_result, probe_error, lock_depth;
static unsigned probes, reports, requests;
static u64 request_result = 1;
static u64 census[44], reported[9];
static unsigned census_bytes;

static void acquire(void) {
    assert(!lock_depth);
    lock_depth = 1;
}

static void release(void) {
    assert(lock_depth);
    lock_depth = 0;
}

static int *__get_errno_ptr(void) {
    return &error_value;
}

static void copy(void *d, const void *s, unsigned n) {
    memcpy(d, s, n);
}

static u64 call(unsigned op, const u64 *args, unsigned n, const void *data, unsigned bytes) {
    assert(lock_depth && !data && !bytes);

    if (op == 367) {
        assert(!args && !n);
        ++requests;
        fixture.out_len = census_bytes;
        memcpy(fixture.data, census, census_bytes);

        return request_result;
    }

    assert(op == 368 && n == 9);
    ++reports;
    memcpy(reported, args, sizeof(reported));

    return 1;
}

int stream_owner_kill(int pid, int signal) {
    assert(!lock_depth && pid > 0 && signal == 0);
    ++probes;
    error_value = probe_error;

    return probe_result;
}

#define QGFX_SCREEN_LIVENESS_TEST 1
#include "screen_stream.c"

static void reset(unsigned count) {
    stream_owner_reported_generation = stream_owner_reported_challenge = 0;
    memset(census, 0, sizeof(census));
    memset(reported, 0, sizeof(reported));
    census[0] = 1;
    census[1] = 7;
    census[2] = 123;
    census[3] = count;

    for (unsigned i = 0; i < count && i < 8; i++) {
        u64 *p = census + 4 + (u64)i * 5;
        p[0] = 100 + i;
        p[1] = 0x40002000 + i * 4096;
        p[2] = 200 + i;
        p[3] = 300 + i;
        p[4] = 400 + i;
    }

    census_bytes = 32 + (count <= 8 ? count : 8) * 40;
    probes = reports = requests = 0;
    request_result = 1;
    probe_result = -1;
    probe_error = 3;
}

int main(void) {
    reset(1);
    stream_owner_liveness_after_export(0);
    assert(!requests && !probes && !reports);
    stream_owner_liveness_after_export(1);
    assert(probes == 1 && reports == 1 && requests == 1);
    const u64 expected[] = {7, 123, 100, 0x40002000, 200, 300, 400, (u64)-1, 3};
    assert(!memcmp(reported, expected, sizeof(expected)));
    stream_owner_liveness();
    assert(probes == 1 && reports == 1 &&
           requests == 2); // Replayed challenge emits no second report.
    census[2]++;
    stream_owner_liveness();
    assert(probes == 2 && reports == 2); // A fresh challenge may probe again.

    for (unsigned i = 0; i < 4; i++) {
        reset(1);
        probe_error = (int[]){1, 4, 0, 22}[i];
        stream_owner_liveness();
        assert(probes == 1 && !reports);
    }

    reset(1);
    probe_result = 0;
    probe_error = 3;
    stream_owner_liveness();
    assert(probes == 1 && !reports); // Live/reused PID, including stale errno.
    reset(8);
    probe_result = 0;
    stream_owner_liveness();
    assert(probes == 8 && !reports);
    reset(8);
    stream_owner_liveness();
    assert(probes == 1 && reports == 1); // Never reuse a challenge for a second owner.
    reset(1);
    request_result = 0;
    stream_owner_liveness();
    assert(!probes && !reports);
    reset(0);
    stream_owner_liveness();
    assert(!probes && !reports);
    reset(9);
    stream_owner_liveness();
    assert(!probes && !reports);
    reset(2);
    census[4 + 5 + 3] = 0;
    stream_owner_liveness();
    assert(!probes && !reports); // Whole census validates before probing.
    reset(1);
    census[7] = 0x80000000ULL;
    stream_owner_liveness();
    assert(!probes && !reports);
    reset(1);
    census[2] = 0;
    stream_owner_liveness();
    assert(!probes && !reports);
    reset(1);
    census_bytes--;
    stream_owner_liveness();
    assert(!probes && !reports);
    reset(1);
    census[8] = 0;
    stream_owner_liveness();
    assert(!probes && !reports);
    assert(!lock_depth);

    return 0;
}
