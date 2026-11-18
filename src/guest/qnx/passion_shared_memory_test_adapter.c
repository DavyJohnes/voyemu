#define QSM_TEST
#include "passion_shared_memory.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
static int fail, opened, controlled, mapped, closed, unmapped;
static QsmSize allocated;
static unsigned char fixture[8192];

int qsm_open(const char *name, int flags, unsigned mode) {
    assert(name == (const char *)-1L && flags == 2 && mode == 0600);
    opened++;

    return fail == 1 ? -1 : 7;
}

int qsm_ctl(int fd, int flags, QsmU64 physical, QsmU64 size) {
    assert(fd == 7 && flags == 1 && physical == 0 && size && !(size & 4095));
    controlled++;
    allocated = size;

    return fail == 2 ? -1 : 0;
}

void *qsm_map(void *p, QsmSize size, int prot, int flags, int fd, long long off) {
    assert(!p && size == allocated && prot == 0x300 && flags == 1 && fd == 7 && !off);
    mapped++;

    return fail == 3 ? (void *)-1L : fixture;
}

int qsm_close(int fd) {
    assert(fd == 7);
    closed++;

    return fail == 4 ? -1 : 0;
}

int qsm_unmap(void *p, QsmSize size) {
    assert(p == fixture && size == allocated);
    unmapped++;

    return fail == 5 ? -1 : 0;
}

static void reset(int n) {
    fail = n;
    opened = controlled = mapped = closed = unmapped = 0;
    allocated = 0;
}

int qgfx_passion_shared_memory_test(void) {
    reset(0);
    assert(!qsm_allocate(0) && !qsm_allocate((64UL << 20) + 1) && !qsm_allocate(~0UL) && !opened);
    reset(1);
    assert(!qsm_allocate(4096) && opened == 1 && !controlled && !closed);
    reset(2);
    assert(!qsm_allocate(4096) && controlled == 1 && closed == 1 && !mapped);
    reset(3);
    assert(!qsm_allocate(4096) && mapped == 1 && closed == 1 && !unmapped);
    reset(4);
    assert(!qsm_allocate(4096) && mapped == 1 && closed == 1 && unmapped == 1);
    reset(0);
    void *p = qsm_allocate(4097);
    assert(p == fixture && allocated == 8192 && closed == 1);
    assert(!qsm_release(p, 4097) && unmapped == 1);
    reset(5);
    p = qsm_allocate(1);
    assert(p == fixture && qsm_release(p, 1) == -1 && unmapped == 1);
    reset(0);
    assert(qsm_release(0, 4096) == -1 && qsm_release((void *)-1L, 4096) == -1 &&
           qsm_release(fixture, 0) == -1 && !unmapped);
    reset(0);
    p = qsm_allocate(64UL << 20);
    assert(p == fixture && allocated == (64UL << 20) && !qsm_release(p, 64UL << 20));
    puts("PASS9 real native call contract/failure cleanup/overflow/rounding/extent checks; host only");

    return 0;
}
