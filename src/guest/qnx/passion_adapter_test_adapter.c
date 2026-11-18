/* Exercise the real Passion packet allocation and pixel extent gates. */
#define QGFX_PASSION_ROOT_IMAGEFS 1
#define QSM_TEST 1
#define write qgfx_test_write
#define usleep qgfx_test_usleep
#include "core.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
static unsigned char memory[4096];
static i64 physical = 0x31c0ff000LL;
static usize span = 4096;
static int released, offset_error, error_value, clock_calls;

int qgfx_close_clock(int id, QgfxCloseTime *time) {
    assert(id == 2);
    time->seconds = 1;
    time->nanoseconds = (u64)++clock_calls;

    return 0;
}

long qgfx_test_write(int fd, const void *p, usize bytes) {
    (void)fd;
    (void)p;

    return (long)bytes;
}

int qgfx_test_usleep(unsigned delay) {
    assert(delay == 1000 && packet->state == 3);
    packet->state = 4;

    return 0;
}

int qsm_open(const char *name, int flags, unsigned mode) {
    assert(name == (const char *)-1L && flags == 2 && mode == 0600);

    return 7;
}

int qsm_ctl(int fd, int flags, QsmU64 p, QsmU64 bytes) {
    assert(fd == 7 && flags == 1 && !p && bytes == 4096);

    return 0;
}

void *qsm_map(void *p, QsmSize bytes, int prot, int flags, int fd, long long off) {
    assert(!p && bytes == 4096 && prot == 0x300 && flags == 1 && fd == 7 && !off);

    return memory;
}

int qsm_close(int fd) {
    assert(fd == 7);

    return 0;
}

int qsm_unmap(void *p, QsmSize bytes) {
    assert(p == memory && bytes == 4096);
    released++;

    return 0;
}

int mem_offset(const void *p, int fd, usize bytes, i64 *address, usize *length) {
    assert(p == memory && fd == -1 && bytes == 4096);
    *address = physical;
    *length = span;

    return offset_error;
}

int *__get_errno_ptr(void) {
    return &error_value;
}

static int prepare_client_arrays(usize vertices) {
    (void)vertices;

    return 0;
}

static void rejected(i64 address, usize contiguous, int error) {
    packet = 0;
    physical = address;
    span = contiguous;
    offset_error = error;
    int prior = released;
    assert(!start_channel() && !packet && released == prior + 1);
}

int qgfx_passion_adapter_test(void) {
    assert(start_channel() && packet == (Packet *)memory && packet->magic == 0x58464751 &&
           packet->version == 3);
    assert(packet->reserved[8] == (u64)(unsigned)getpid() && packet->reserved[9] == 1000000001ULL &&
           clock_calls == 1);
    rejected(0x31c100000LL, 4096, 0);
    rejected(0x3ffff000LL, 4096, 0);
    rejected(0x40000001LL, 4096, 0);
    rejected(0x40000000LL, 4095, 0);
    rejected(0x40000000LL, 4096, -1);
    packet = 0;
    physical = 0x40000000LL;
    span = 4096;
    offset_error = 0;
    assert(start_channel());
    assert(packet->reserved[8] == (u64)(unsigned)getpid() && packet->reserved[9] == 1000000002ULL);
    assert(pixel_extent(3, 2, 0x80e1, 0x1401) == 24);
    assert(!pixel_extent(3, 2, 0x80e1, 0x1406));
    assert(!pixel_extent(0, 2, 0x80e1, 0x1401));
    assert(pixel_extent(4096, 4096, 0x80e1, 0x1401) == MAX_BULK);
    assert(!pixel_extent(4096, 4097, 0x80e1, 0x1401));
    unpack_row_length = 0x7fffffff;
    unpack_alignment = 8;
    /* This stride is 2^33 even when usize is 32-bit; an unused row must remain valid. */
    assert(pixel_extent(1, 1, 0x80e1, 0x1401) == 4);
    unpack_skip_rows = 2;
    assert(!pixel_extent(1, 0x7fffffff, 0x80e1, 0x1401));
    unpack_row_length = 0;
    unpack_alignment = 4;
    unpack_skip_rows = 1;
    unpack_skip_pixels = 1;
    assert(pixel_extent(3, 2, 0x80e1, 0x1401) == 40);
    unpack_skip_rows = unpack_skip_pixels = 0;
    unpack_buffer = 1;
    assert(!pixel_extent(3, 2, 0x80e1, 0x1401));
    const int clocks_before_close = clock_calls;
    assert(clocks_before_close == 2);
    close_channel();
    assert(channel_closed && packet->state == 4 && clock_calls == clocks_before_close + 2);

    return 0;
}
