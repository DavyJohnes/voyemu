#define QNET_TEST 1
#include "interguest_network.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>
static int error, ready, revents = 1, read_size = 60, write_size = -2;
static unsigned reads, writes;

int *qnet_errno(void) {
    return &error;
}

int qnet_poll(QnetPoll *p, unsigned n, int t) {
    assert(n == 1 && t == 0);
    p->revents = (short)(ready ? revents : 0);

    return ready;
}

long qnet_read(int f, void *p, QnetSize n) {
    assert(f == 7 && n == 1536);
    ++reads;
    memset(p, 42, n);

    return read_size;
}

long qnet_write(int f, const void *p, QnetSize n) {
    assert(f == 7 && p);
    ++writes;

    return write_size == -2 ? (long)n : write_size;
}

static QnetPage *observed;
static unsigned sets, gets;
static int set_error, get_error;
static QnetI64 desired_seconds, readback, readback_ns;

int qnet_clock_settime(int id, const QnetTimespec *t) {
    assert(id == 0 && t->nanoseconds == 0 && t->seconds == desired_seconds);
    assert(observed->utc_ack == 0);
    ++sets;

    return set_error;
}

int qnet_clock_gettime(int id, QnetTimespec *t) {
    assert(id == 0);
    assert(observed->utc_ack == 0);
    ++gets;
    t->seconds = readback;
    t->nanoseconds = readback_ns;

    return get_error;
}

static unsigned syncs, mounts;
static int mount_failure;

void qnet_sync(void) {
    assert(observed->flush_ack < observed->flush_request);
    ++syncs;
}

int qnet_spawn(int *pid, const char *path, const void *a, const void *b, char *const *argv,
               char *const *env) {
    assert(!a && !b && !strcmp(path, "/bin/mount") && !strcmp(argv[1], "-u") &&
           !strcmp(argv[2], "-r") && !argv[4]);
    assert(!strcmp(env[0], "PATH=/bin:/usr/bin") && !env[1]);
    assert(!strcmp(argv[3], mounts % 2 ? "/" : "/var"));
    assert(syncs);
    ++mounts;
    *pid = 123;

    return 0;
}

int qnet_waitpid(int pid, int *status, int options) {
    assert(pid == 123 && options == 0x0040);
    *status = mount_failure ? 256 : 0;

    return pid;
}

int qnet_kill(int pid, int signal) {
    assert(pid == 123 && signal == 9);

    return 0;
}

int qnet_sleep(unsigned us) {
    assert(us == 1000);

    return 0;
}

static QnetPage page;
static QnetU32 last;

static void reset(void) {
    memset(&page, 0, sizeof(page));
    page.magic = 0x54454e51;
    page.version = 1;
    observed = &page;
    last = 0;
    error = ready = reads = writes = 0;
    revents = 1;
    read_size = 60;
    write_size = -2;
    sets = gets = syncs = mounts = 0;
    mount_failure = 0;
    set_error = get_error = 0;
    desired_seconds = readback = 1792713600LL;
    readback_ns = 0;
}

int main(void) {
    assert(qnet_page_valid(0x40000000LL, 4096));
    assert(!qnet_page_valid(0x3ffff000LL, 4096) && !qnet_page_valid(0x40000001LL, 4096) &&
           !qnet_page_valid(0x40000000LL, 4095));
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    assert(qnet_page_valid(0x17ffff000LL, 4096) && !qnet_page_valid(0x180000000LL, 4096));
#else
    assert(qnet_page_valid(0x23ffff000LL, 4096) && !qnet_page_valid(0x240000000LL, 4096));
#endif

    reset();
    assert(qnet_step(&page, 7, &last) == 0);
    reset();
    qnet_diagnostics = 1;
    ready = 1;
    assert(qnet_step(&page, 7, &last) == 1);
    QnetDiagnostic *d = (QnetDiagnostic *)page.padding;
    assert(d->magic == 0x31444e51U && d->phase == 7 && d->heartbeat == 1 && d->result == 60 &&
           !d->error);
    assert(!page.utc_ack && !page.flush_ack);
    reset();
    page.rxlen = 60;
    page.rxseq = 1;
    write_size = -1;
    error = 4;
    assert(qnet_step(&page, 7, &last) == 0);
    d = (QnetDiagnostic *)page.padding;
    assert(d->phase == 3 && d->result == ~0U && d->error == 4 && page.rxlen == 60 && !page.fault);
    qnet_diagnostics = 0;
    reset();
    ready = 1;
    assert(qnet_step(&page, 7, &last) == 1 && page.txlen == 60 && page.txseq == 1 &&
           page.txpackets == 1 && reads == 1);
    assert(qnet_step(&page, 7, &last) == 0 && reads == 1);
    reset();
    page.rxlen = 60;
    page.rxseq = 1;
    assert(qnet_step(&page, 7, &last) == 1 && page.rxlen == 0 && last == 1 && writes == 1 &&
           page.rxpackets == 1);
    reset();
    page.rxlen = 13;
    page.rxseq = 1;
    assert(qnet_step(&page, 7, &last) == -1 && page.fault == 2 && writes == 0);
    reset();
    page.rxlen = 1519;
    page.rxseq = 1;
    assert(qnet_step(&page, 7, &last) == -1 && writes == 0);
    reset();
    page.rxlen = 60;
    page.rxseq = last = 1;
    assert(qnet_step(&page, 7, &last) == -1 && writes == 0);
    reset();
    page.rxlen = 60;
    page.rxseq = 1;
    write_size = 30;
    assert(qnet_step(&page, 7, &last) == -1 && page.fault == 3 && page.rxlen == 60);
    reset();
    page.rxlen = 60;
    page.rxseq = 1;
    write_size = -1;
    error = 4;
    assert(qnet_step(&page, 7, &last) == 0 && page.rxlen == 60 && !page.fault);
    reset();
    ready = 1;
    read_size = 1536;
    assert(qnet_step(&page, 7, &last) == -1 && page.fault == 7 && !page.txlen);
    reset();
    ready = 1;
    revents = 32;
    assert(qnet_step(&page, 7, &last) == -1 && page.fault == 6 && reads == 0);
    reset();
    page.txlen = 1600;
    assert(qnet_step(&page, 7, &last) == -1 && page.fault == 4);
    reset();
    page.stop = 1;
    assert(qnet_step(&page, 7, &last) == 2 && !reads && !writes);
    reset();
    page.magic = 0;
    assert(qnet_step(&page, 7, &last) == -1 && page.fault == 1);
    reset();
    ready = 1;
    page.txseq = ~0U;
    assert(qnet_step(&page, 7, &last) == 1 && page.txseq == 1);
    reset();
    page.utc_epoch = 1792713600ULL;
    assert(qnet_step(&page, 7, &last) == 0 && sets == 0);
    page.utc_request = 1;
    assert(qnet_step(&page, 7, &last) == 1 && sets == 1 && gets == 1 && page.utc_ack == 1 &&
           page.utc_status == 0 && page.utc_actual == 1792713600U);
    assert(qnet_step(&page, 7, &last) == 0 && sets == 1);
    page.utc_request = 2;
    assert(qnet_step(&page, 7, &last) == -1 && page.fault == 9 && sets == 1);
    const QnetU64 valid[] = {946684800ULL, 2147483648ULL, 4102444799ULL};

    for (unsigned i = 0; i < sizeof(valid) / sizeof(valid[0]); ++i) {
        reset();
        desired_seconds = readback = (QnetI64)valid[i];
        page.utc_epoch = valid[i];
        page.utc_request = 1;
        assert(qnet_step(&page, 7, &last) == 1 && sets == 1 && gets == 1 && page.utc_status == 0 &&
               page.utc_actual == valid[i]);
    }

    const QnetU64 invalid[] = {0, 946684799ULL, 4102444800ULL, 0x8000000000000000ULL, ~0ULL};

    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        reset();
        page.utc_epoch = invalid[i];
        page.utc_request = 1;
        assert(qnet_step(&page, 7, &last) == 1 && page.utc_ack == 1 && page.utc_status == -1 &&
               !sets && !gets);
    }

    reset();
    page.utc_epoch = 1792713600ULL;
    page.utc_request = 2;
    assert(qnet_step(&page, 7, &last) == 1 && page.utc_status == -1 && !sets);
    reset();
    page.utc_epoch = 1792713600ULL;
    page.utc_request = 1;
    set_error = 1;
    assert(qnet_step(&page, 7, &last) == 1 && page.utc_status == -2 && sets == 1 && !gets);
    reset();
    page.utc_epoch = 1792713600ULL;
    page.utc_request = 1;
    get_error = 1;
    assert(qnet_step(&page, 7, &last) == 1 && page.utc_status == -3 && gets == 1);
    reset();
    page.utc_epoch = 1792713600ULL;
    page.utc_request = 1;
    readback += 3;
    assert(qnet_step(&page, 7, &last) == 1 && page.utc_status == -4);
    reset();
    page.utc_epoch = 1792713600ULL;
    page.utc_request = 1;
    readback_ns = 1000000000LL;
    assert(qnet_step(&page, 7, &last) == 1 && page.utc_status == -4);
    reset();
    page.utc_epoch = 1792713600ULL;
    page.utc_request = 1;
    readback -= 1;
    assert(qnet_step(&page, 7, &last) == 1 && page.utc_status == -4);
    reset();
    page.utc_epoch = 1792713600ULL;
    page.utc_request = 1;
    readback_ns = -1;
    assert(qnet_step(&page, 7, &last) == 1 && page.utc_status == -4);
    reset();
    page.flush_request = 1;
    assert(qnet_step(&page, 7, &last) == 1 && syncs == 2 && mounts == 2 && page.flush_ack == 1 &&
           page.flush_status == 0);
    assert(qnet_step(&page, 7, &last) == 0 && syncs == 2);
    page.flush_request = 2;
    assert(qnet_step(&page, 7, &last) == 1 && syncs == 4 && mounts == 4 && page.flush_ack == 2);
    reset();
    page.flush_request = 1;
    mount_failure = 1;
    assert(qnet_step(&page, 7, &last) == 1 && syncs == 1 && page.flush_ack == 1 &&
           page.flush_status == -2);
    mount_failure = 0;
    mounts = 0;
    page.flush_request = 2;
    assert(qnet_step(&page, 7, &last) == 1 && syncs == 3 && page.flush_ack == 2 &&
           page.flush_status == 0);
    reset();
    page.flush_ack = 2;
    page.flush_request = 1;
    assert(qnet_step(&page, 7, &last) == -1 && !syncs && page.fault == 10);

    return 0;
}
