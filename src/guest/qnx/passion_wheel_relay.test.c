#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const char *tool = "/bin/msg_center_test";

static char *getenv(const char *name) {
    assert(!strcmp(name, "QGFX_PASSION_WHEEL_TOOL"));

    return (char *)tool;
}

#define QGFX_PASSION_WHEEL_TEST 1
#include "passion_wheel_relay.c"
static char observed[40], output[2000];
static int mode, polled, killed, error_value, read_done, closed_read, closed_write, destroyed;

int qpw_spawn(int *pid, const char *path, const void *a, const void *b, char *const *argv,
              char *const *env) {
    (void)b;
    assert(a && *(const QpwFileActions *)a == 123);
    assert(!strcmp(path, "/bin/on"));
    assert(!strcmp(argv[5], "/bin/msg_center_test"));
    assert(!strcmp(argv[7], "uartrpc_svc/steer_key"));
    assert(!strcmp(env[2], "LD_PRELOAD=secpol-preload.so") && !env[3]);
    strcpy(observed, argv[8]);
    *pid = 77;
    polled = 0;
    read_done = 0;
    snprintf(output, sizeof(output), "send data success: %s\n", argv[8]);

    if (mode == 5) {
        snprintf(output, sizeof(output), "send data failed: %s\n", argv[8]);
    }

    if (mode == 6) {
        output[0] = 0;
    }

    if (mode == 7) {
        strcpy(output, "send data success: ~!b:");
    }

    if (mode == 8) {
        unsigned n = (unsigned)strlen(output);
        memcpy(output + n, output, n + 1);
    }

    if (mode == 13) {
        memset(output, 'a', sizeof(output) - 1);
        output[sizeof(output) - 1] = 0;
    }

    return mode == 1 ? 1 : 0;
}

int qpw_wait(int pid, int *status, int flags) {
    assert(pid == 77 && flags == 0x40);
    polled++;

    if (mode == 3 && !killed) {
        return 0;
    }

    if (mode == 4) {
        error_value = 10;

        return -1;
    }

    *status = mode == 2 ? 256 : 0;

    return pid;
}

int qpw_kill(int pid, int signal) {
    assert(pid == 77 && signal == 9);
    killed++;

    return 0;
}

int qpw_sleep(unsigned delay) {
    assert(delay == 20000 || delay == 1000);

    return 0;
}

int *qpw_errno(void) {
    return &error_value;
}

int qpw_pipe(int *fds) {
    fds[0] = 8;
    fds[1] = 9;

    return mode == 9 ? -1 : 0;
}

int qpw_close(int fd) {
    assert(fd == 8 || fd == 9);

    if (fd == 8) {
        closed_read++;
    } else {
        closed_write++;
    }

    return 0;
}

long qpw_read(int fd, void *data, unsigned long size) {
    assert(fd == 8 && !read_done);

    if (mode == 14) {
        return -1;
    }

    unsigned long n = strlen(output);

    if (n > size) {
        n = size;
    }

    memcpy(data, output, n);
    read_done = 1;

    return (long)n;
}

int qpw_poll(QpwPoll *p, unsigned n, int timeout) {
    assert(n == 1 && !timeout && p->fd == 8);

    if (mode == 15) {
        return -1;
    }

    p->revents = read_done ? 16 : 1;

    return 1;
}

int qpw_actions_init(QpwFileActions *a) {
    if (mode == 10) {
        return 12;
    }

    *a = 123;

    return 0;
}

int qpw_actions_destroy(QpwFileActions *a) {
    assert(*a == 123);
    destroyed++;
    *a = 0;

    return 0;
}

int qpw_actions_dup(QpwFileActions *a, int fd, int dest) {
    assert(*a == 123 && fd == 9 && (dest == 1 || dest == 2));

    return mode == 11 ? 9 : 0;
}

int qpw_actions_close(QpwFileActions *a, int fd) {
    assert(*a == 123 && (fd == 8 || fd == 9));

    return mode == 12 ? 9 : 0;
}

int main(void) {
    tool = 0;
    assert(!passion_wheel_publish(325, 1));
    tool = "/tmp/tool";
    assert(!passion_wheel_publish(325, 1));
    tool = "/bin/msg_center_test";
    assert(!passion_wheel_publish(322, 1) && !passion_wheel_publish(329, 1) &&
           !passion_wheel_publish(325, 2));
    assert(passion_wheel_publish(325, 1));
    assert(!strcmp(observed, "~!b:0x140x000x000x000x020x00"));
    assert(passion_wheel_publish(330, 1));
    assert(!strcmp(observed, "~!b:0x140x000x000x000x820x00"));
    assert(passion_wheel_publish(325, 0));
    assert(!strcmp(observed, "~!b:0x140x000x000x000x800x00"));
    assert(passion_wheel_publish(330, 0) && passion_wheel_bitmap == 0);

    for (mode = 1; mode <= 8; mode++) {
        killed = 0;
        closed_read = closed_write = destroyed = 0;
        assert(!passion_wheel_publish(325, 1) && passion_wheel_bitmap == 0);
        assert(killed == (mode == 3));
        assert(closed_read == 1 && closed_write == 1 && destroyed == 1);
    }

    for (mode = 9; mode <= 12; mode++) {
        closed_read = closed_write = destroyed = 0;
        assert(!passion_wheel_publish(325, 1) && passion_wheel_bitmap == 0);
        assert(closed_read == (mode != 9) && closed_write == (mode != 9) &&
               destroyed == (mode >= 11));
    }

    for (mode = 13; mode <= 15; mode++) {
        closed_read = closed_write = destroyed = 0;
        assert(!passion_wheel_publish(325, 1) && passion_wheel_bitmap == 0);
        assert(closed_read == 1 && closed_write == 1 && destroyed == 1);
    }

    const unsigned indices[] = {323, 324, 325, 326, 327, 328, 330, 331},
                   bits[] = {14, 10, 9, 8, 6, 7, 15, 13};
    mode = 0;

    for (unsigned i = 0; i < 8; i++) {
        assert(passion_wheel_bit(indices[i]) == bits[i]);
        assert(passion_wheel_publish(indices[i], 1));
        assert(passion_wheel_bitmap == 1u << bits[i]);
        assert(passion_wheel_publish(indices[i], 0) && !passion_wheel_bitmap);
    }

    return 0;
}
