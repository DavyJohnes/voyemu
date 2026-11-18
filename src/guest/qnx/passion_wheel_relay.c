/* OEM input_service consumes changes in a six-byte steer_key bitmap.
 * ACK requires a successful exit and exact publisher receipt, never a UI effect. */
#ifndef QGFX_PASSION_WHEEL_TEST
#define QPW_ALIAS(name) __asm__(#name)
#else
#define QPW_ALIAS(name)
#endif
extern int qpw_spawn(int *, const char *, const void *, const void *, char *const *, char *const *)
        QPW_ALIAS(posix_spawn);
extern int qpw_wait(int, int *, int) QPW_ALIAS(waitpid);
extern int qpw_kill(int, int) QPW_ALIAS(kill);
extern int qpw_sleep(unsigned) QPW_ALIAS(usleep);
extern int *qpw_errno(void) QPW_ALIAS(__get_errno_ptr);
typedef unsigned long QpwFileActions;
_Static_assert(sizeof(QpwFileActions) == 8, "pinned QNX AArch64 spawn handle");
extern int qpw_pipe(int *) QPW_ALIAS(pipe);
extern int qpw_close(int) QPW_ALIAS(close);
extern long qpw_read(int, void *, unsigned long) QPW_ALIAS(read);
typedef struct {
    int fd;
    short events, revents;
} QpwPoll;
_Static_assert(sizeof(QpwPoll) == 8, "pinned QNX pollfd");
extern int qpw_poll(QpwPoll *, unsigned, int) QPW_ALIAS(poll);
/* Actual libc SHA2d08b4...1eb8: init207696 stores one8byte handle,
 * adddup2/destroy load it; heap24byte implementation stays opaque. */
extern int qpw_actions_init(QpwFileActions *) QPW_ALIAS(posix_spawn_file_actions_init);
extern int qpw_actions_destroy(QpwFileActions *) QPW_ALIAS(posix_spawn_file_actions_destroy);
extern int qpw_actions_dup(QpwFileActions *, int, int) QPW_ALIAS(posix_spawn_file_actions_adddup2);
extern int qpw_actions_close(QpwFileActions *, int) QPW_ALIAS(posix_spawn_file_actions_addclose);
#undef QPW_ALIAS

static unsigned passion_wheel_bitmap;

static unsigned passion_wheel_bit(unsigned index) {
    switch (index) {
    case 323:
        return 14;
    case 324:
        return 10;
    case 325:
        return 9;
    case 326:
        return 8;
    case 327:
        return 6;
    case 328:
        return 7;
    case 330:
        return 15;
    case 331:
        return 13;
    default:
        return 0;
    }
}

static int passion_wheel_receipt(int fd, const char *payload) {
    char text[1025], expected[80];
    unsigned used = 0;

    for (unsigned i = 0; i < 16; ++i) {
        QpwPoll p = {fd, 1, 0};
        int ready = qpw_poll(&p, 1, 0);

        if (ready < 0) {
            return 0;
        }

        if (!ready || !(p.revents & 1)) {
            break;
        }

        long n = qpw_read(fd, text + used, sizeof(text) - 1 - used);

        if (n < 0) {
            return 0;
        }

        if (!n) {
            break;
        }

        used += (unsigned)n;

        if (used == sizeof(text) - 1) {
            return 0;
        }
    }

    text[used] = 0;
    int n = snprintf(expected, sizeof(expected), "send data success: %s\n", payload);

    if (n <= 0 || (unsigned)n >= sizeof(expected)) {
        return 0;
    }

    const char *found = __builtin_strstr(text, expected);

    return found && (found == text || found[-1] == '\n') &&
           !__builtin_strstr(found + 1, expected) && !__builtin_strstr(text, "send data failed") &&
           !__builtin_strstr(text, "send byte values error");
}

static int passion_wheel_publish(unsigned index, unsigned phase) {
    unsigned bit = passion_wheel_bit(index);

    if (!bit || phase > 1) {
        return 0;
    }

    const char *tool = getenv("QGFX_PASSION_WHEEL_TOOL");

    if (!tool || (__builtin_strcmp(tool, "/bin/msg_center_test") != 0 &&
                  __builtin_strcmp(tool, "/usr/bin/msg_center_test") != 0)) {
        return 0;
    }

    unsigned mask = 1u << bit,
             next = phase ? passion_wheel_bitmap | mask : passion_wheel_bitmap & ~mask;
    char payload[40];
    int length = snprintf(payload, sizeof(payload), "~!b:0x140x000x%02x0x%02x0x%02x0x%02x",
                          next >> 24, (next >> 16) & 255, (next >> 8) & 255, next & 255);

    if (length <= 0 || (unsigned)length >= sizeof(payload)) {
        return 0;
    }

    char *argv[] = {(char *)"/bin/on",
                    (char *)"-T",
                    (char *)"ic_apps_t",
                    (char *)"-u",
                    (char *)"834:819",
                    (char *)tool,
                    (char *)"-t",
                    (char *)"uartrpc_svc/steer_key",
                    payload,
                    0};
    /* Do not inherit the compositor graphics preload or its relay environment. */
    char *env[] = {(char *)"PATH=/bin:/usr/bin", (char *)"LD_LIBRARY_PATH=/lib64:/usr/lib64",
                   (char *)"LD_PRELOAD=secpol-preload.so", 0};
    int fds[2];

    if (qpw_pipe(fds)) {
        return 0;
    }

    if (fds[0] <= 2 || fds[1] <= 2) {
        qpw_close(fds[0]);
        qpw_close(fds[1]);

        return 0;
    }

    QpwFileActions actions = 0;
    int child = 0, status = 0;

    if (qpw_actions_init(&actions)) {
        qpw_close(fds[0]);
        qpw_close(fds[1]);

        return 0;
    }

    int configured = !qpw_actions_dup(&actions, fds[1], 1) &&
                     !qpw_actions_dup(&actions, fds[1], 2) &&
                     !qpw_actions_close(&actions, fds[0]) && !qpw_actions_close(&actions, fds[1]);
    int spawned = configured && !qpw_spawn(&child, argv[0], &actions, 0, argv, env) && child > 0;
    qpw_actions_destroy(&actions);
    qpw_close(fds[1]);

    if (!spawned) {
        qpw_close(fds[0]);

        return 0;
    }

    for (unsigned i = 0; i < 100; ++i) {
        int result = qpw_wait(child, &status, 0x0040);

        if (result == child) {
            int accepted = !status && passion_wheel_receipt(fds[0], payload);
            qpw_close(fds[0]);

            if (!accepted) {
                return 0;
            }

            passion_wheel_bitmap = next;

            return 1;
        }

        if (result < 0 && *qpw_errno() != 4) {
            qpw_close(fds[0]);

            return 0;
        }

        qpw_sleep(20000);
    }

    qpw_close(fds[0]);

    /* Only this unreaped direct child can be signalled; no retry of an uncertain phase. */
    if (qpw_kill(child, 9)) {
        return 0;
    }

    for (unsigned i = 0; i < 100; ++i) {
        int result = qpw_wait(child, &status, 0x0040);

        if (result == child) {
            return 0;
        }

        if (result < 0 && *qpw_errno() != 4) {
            return 0;
        }

        qpw_sleep(1000);
    }

    return 0;
}
