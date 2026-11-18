#define QGFX_WRAPPER_TEST 1
#define usleep lifecycle_sleep
#define mlock lifecycle_mlock
#define munlock lifecycle_munlock
#include "core.c"

#include "client_arrays.c"
#include <assert.h>
static int acknowledge = 1;
static unsigned sleeps, flushed, flush_result = 1;

/* Host stand-in for the guest libc page-locking pair, in the style of
 * egl_strings_test_adapter.c. On QNX mlock/munlock pin the packet and bulk pages
 * so mem_offset reports a locked physical run; the QGFX_WRAPPER_TEST fixture
 * channel below never reaches bulk_allocate/bulk_free, so the macOS host links
 * the real POSIX mlock whereas this stub only renames the symbol and returns the
 * same success. Assertions in main() are unchanged. */
int lifecycle_mlock(const void *p, usize n) {
    (void)p;
    (void)n;

    return 0;
}

int lifecycle_munlock(const void *p, usize n) {
    (void)p;
    (void)n;

    return 0;
}

int qgfx_close_clock(int id, QgfxCloseTime *t) {
    assert(id == 2);
    t->seconds = sleeps / 1000;
    t->nanoseconds = (sleeps % 1000) * 1000000ULL;

    return 0;
}

int lifecycle_sleep(unsigned delay) {
    assert(delay == 1000);
    ++sleeps;

    if (acknowledge && packet && packet->state == 3) {
        packet->state = 4;
    }

    return 0;
}

u64 test_remote(Packet *p, u32 op, const u64 *args, unsigned n, const void *input, unsigned bytes) {
    (void)p;
    (void)n;
    (void)input;
    (void)bytes;
    assert(op == 360);
    ++flushed;

    if (!flush_result) {
        return 0;
    }

    return args[0];
}

int main(void) {
    start_channel();
    Packet *fixture = packet;
    fixture->reserved[0] = 11;
    fixture->reserved[1] = 12;
    fixture->reserved[2] = 13;
    assert(!channel_identity(fixture));
    sleeps = 1;
    assert(channel_identity(fixture));
    const u64 incarnation = fixture->reserved[9];
    assert(fixture->reserved[8] == (u64)getpid() && incarnation == 1000000ULL);
    assert(fixture->reserved[0] == 11 && fixture->reserved[1] == 12 && fixture->reserved[2] == 13);
    sleeps = 2;
    assert(channel_identity(fixture) && fixture->reserved[9] != incarnation);
    sleeps = 0;
    fixture->magic = 0x58464751;
    fixture->version = 3;
    gl_batch_count = 1;
    gl_batch_bytes = 16;
    close_channel();
    assert(flushed == 1 && sleeps == 1 && fixture->state == 4 && channel_closed);
    close_channel();
    assert(sleeps == 1 && flushed == 1);
    assert(!call_raw(20, 0, 0, 0, 0));
    packet = fixture;
    fixture->state = 2;
    flush_result = 0;
    sleeps = 0;
    channel_closed = 0;
    gl_batch_count = 1;
    gl_batch_bytes = 16;
    close_channel();
    assert(flushed == 2 && sleeps == 1 && fixture->state == 4 &&
           fixture->reserved[0] == 0x3145534f4c434751ULL && channel_closed);
    flush_result = 1;
    packet = 0;
    channel_closed = 0;
    close_channel();
    assert(!channel_closed);
    packet = fixture;
    fixture->state = 2;
    acknowledge = 0;
    sleeps = 0;
    close_channel();
    assert(sleeps == 2000 && fixture->state == 3 && channel_closed);

    return 0;
}
