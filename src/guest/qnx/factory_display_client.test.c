#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static char output[4096];

static void emit(const char *s) {
    assert(strlen(output) + strlen(s) < sizeof(output));
    strcat(output, s);
}

#define dlopen factory_test_open
#define dlsym factory_test_symbol
#define nanosleep factory_test_sleep
#include "factory_display_client.c"
static int scenario, error, opens, closes, sends, receives, polls;
static long long milliseconds;

static int test_socket(int domain, int type, int protocol) {
    assert(domain == 2 && type == 1 && !protocol);
    opens++;

    return 7;
}

static int test_close(int fd) {
    assert(fd == 7);
    closes++;

    return 0;
}

static int test_control(int fd, int cmd, ...) {
    assert(fd == 7);

    if (cmd == 3) {
        return 2;
    }

    va_list args;
    va_start(args, cmd);
    assert(cmd == 4 && va_arg(args, int) == (2 | 0200));
    va_end(args);

    return scenario == 9 ? -1 : 0;
}

static int test_connect(int fd, const void *p, unsigned n) {
    const unsigned char expected[16] = {16, 2, 0x1e, 0x61, 127, 0, 0, 1};

    assert(fd == 7 && n == 16 && !memcmp(p, expected, 16));

    if (scenario == 7) {
        error = 261;

        return -1;
    }

    if (scenario == 6 || scenario == 8) {
        error = 236;

        return -1;
    }

    return 0;
}

static int test_option(int fd, int level, int name, void *p, unsigned *n) {
    assert(fd == 7 && level == 0xffff && name == 0x1007 && *n == 4);
    *(int *)p = scenario == 8 ? 261 : 0;

    return 0;
}

static long test_send(int fd, const void *p, unsigned long n, int flags) {
    assert(fd == 7 && n == 264 && flags == 0x0800);
    const unsigned char *b = p;
    assert(b[0] == 42);

    for (unsigned i = 1; i < 264; i++) {
        assert(!b[i]);
    }

    sends++;

    return scenario == 3 ? 12 : 264;
}

static long test_recv(int fd, void *p, unsigned long n, int flags) {
    assert(fd == 7 && !flags);
    receives++;

    if (scenario == 1) {
        return 0;
    }

    if (scenario == 10 && receives == 1) {
        error = 4;

        return -1;
    }

    if (scenario == 11 && receives == 1) {
        error = 11;

        return -1;
    }

    unsigned char response[264] = {42};

    if (scenario == 2) {
        response[4] = 1;
    }

    if (scenario == 4) {
        response[0] = 41;
    }

    if (scenario == 12) {
        response[7] = 128;
    }

    unsigned at = scenario == 5 ? (unsigned)(receives - 1) * 3 : 0;
    unsigned amount = scenario == 5 ? 3 : 264;
    assert(amount <= n);
    memcpy(p, response + at, amount);

    return amount;
}

static int test_poll(FactoryPoll *p, unsigned n, int timeout) {
    assert(p->fd == 7 && n == 1 && timeout > 0 && timeout <= 4000);
    polls++;

    if (scenario == 13) {
        error = 4;
        milliseconds += 500;

        return -1;
    }

    if (scenario == 14) {
        p->revents = 0x1000;

        return 1;
    }

    if (scenario == 15) {
        p->revents = 0x20;

        return 1;
    }

    if (scenario == 0 && sends) {
        milliseconds += timeout;

        return 0;
    }

    p->revents = p->events;

    if (scenario == 16 && sends) {
        p->revents |= 0x40;
    }

    return 1;
}

static int test_clock(int id, FactoryTime *t) {
    assert(id == 2);
    t->seconds = milliseconds / 1000;
    t->nanoseconds = milliseconds % 1000 * 1000000;

    return 0;
}

static int *test_error(void) {
    return &error;
}

int factory_test_sleep(const void *p, void *unused) {
    const long long *t = p;

    assert(!unused && !t[0] && t[1] == 100000000);
    milliseconds += 100;

    return 0;
}

void *factory_test_open(const char *p, int flags) {
    assert(!strcmp(p, "/lib64/libsocket.so") && flags == 2);

    return (void *)1;
}

void *factory_test_symbol(void *lib, const char *name) {
    assert(lib == (void *)1);
#define SYMBOL(n, f)                                                                               \
    if (!strcmp(name, n))                                                                          \
    return (void *)f
    SYMBOL("socket", test_socket);
    SYMBOL("connect", test_connect);
    SYMBOL("send", test_send);
    SYMBOL("recv", test_recv);
    SYMBOL("close", test_close);
    SYMBOL("fcntl", test_control);
    SYMBOL("getsockopt", test_option);
    SYMBOL("poll", test_poll);
    SYMBOL("clock_gettime", test_clock);
    SYMBOL("__get_errno_ptr", test_error);
#undef SYMBOL
    assert(0);

    return 0;
}

static void run(int mode, int expected) {
    scenario = mode;
    error = opens = closes = sends = receives = polls = 0;
    milliseconds = 0;
    output[0] = 0;
    assert(factory_display_send() == expected);
    assert(opens == closes);
    assert(milliseconds <= 4000);
    assert((strstr(output, "case42-callback-accepted=1") != 0) == (expected == 0));
    assert(!strstr(output, "case42-delivered"));
}

int main(void) {
    assert(factory_display_enabled("1") && !factory_display_enabled("10") &&
           !factory_display_enabled(0));
    run(0, -1);
    assert(milliseconds == 4000 && receives == 0); /* accepted but silent */
    run(1, -1);
    run(2, -1);
    run(3, -1);
    assert(sends == 1 && receives == 0);
    run(4, -1);
    run(5, 0);
    assert(receives == 88);
    run(6, 0);
    run(7, -1);
    assert(opens == 40 && !sends);
    run(8, -1);
    run(9, -1);
    run(10, 0);
    run(11, 0);
    run(12, -1);
    run(13, -1);
    run(14, -1);
    run(15, -1);
    run(16, 0);
    puts("factory_display_client: 17 protocol/deadline scenarios passed");

    return 0;
}
