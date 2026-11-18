/* One loopback heartbeat using OEM QNX socket routines; no UI writes. */
typedef struct {
    int fd;
    short events, revents;
} IvcPoll;
_Static_assert(sizeof(IvcPoll) == 8, "pollfd layout");

static int ivc_heartbeat_probe(void) {
    void *lib = dlopen("/lib64/libsocket.so", 2);

    int (*sock)(int, int, int) = lib ? (int (*)(int, int, int))dlsym(lib, "socket") : 0;
    int (*conn)(int, const void *,
                unsigned) = lib ? (int (*)(int, const void *, unsigned))dlsym(lib, "connect") : 0;
    long (*snd)(int, const void *, unsigned long, int) =
            lib ? (long (*)(int, const void *, unsigned long, int))dlsym(lib, "send") : 0;
    long (*rcv)(int, void *, unsigned long,
                int) = lib ? (long (*)(int, void *, unsigned long, int))dlsym(lib, "recv") : 0;
    int (*wait)(IvcPoll *, unsigned long,
                int) = lib ? (int (*)(IvcPoll *, unsigned long, int))dlsym(lib, "poll") : 0;
    int (*finish)(int) = lib ? (int (*)(int))dlsym(lib, "close") : 0;

    if (!sock || !conn || !snd || !rcv || !wait || !finish) {
        emit("QG3 IVC unavailable=socket-symbol\n");

        return -1;
    }

    int fd = sock(2, 1, 0);

    if (fd < 0) {
        emit("QG3 IVC fail=socket\n");

        return -1;
    }

    unsigned char address[16] = {16, 2, 0x19, 0x38, 127, 0, 0, 1};

    if (conn(fd, address, sizeof(address))) {
        finish(fd);
        emit("QG3 IVC fail=connect\n");

        return -1;
    }

    const unsigned char request[] = {10, 16, 0, 0, 4, 0x67, 0, 0, 0, 0, 13};

    if (snd(fd, request, sizeof(request), 0) != (long)sizeof(request)) {
        finish(fd);
        emit("QG3 IVC fail=send\n");

        return -1;
    }

    emit("QG3 IVC heartbeat-written bytes=11\n");
    unsigned char reply[256];
    unsigned used = 0, target = 0;

    for (unsigned attempt = 0; attempt < 4; attempt++) {
        IvcPoll ready = {fd, 1, 0};

        if (wait(&ready, 1, 500) <= 0 || !(ready.revents & 1)) {
            break;
        }

        long n = rcv(fd, reply + used, sizeof(reply) - used, 0);

        if (n <= 0 || n > (long)(sizeof(reply) - used)) {
            break;
        }

        used += (unsigned)n;

        if (used >= 5) {
            target = ((unsigned)reply[3] << 8 | reply[4]) + 7;

            if (target < 8 || target > 255) {
                break;
            }
        }

        if (target && used >= target) {
            break;
        }
    }

    finish(fd);
    char line[160];
    snprintf(line, sizeof(line), "QG3 IVC reply bytes=%u target=%u\n", used, target);
    emit(line);

    if (target < 8 || target > 255 || used < target || reply[0] != 10 || reply[1] != 32 ||
        reply[5] != 0x87 || reply[target - 1] != 13) {
        emit("QG3 IVC fail=reply-frame\n");

        return -1;
    }

    char hex[600];
    unsigned pos = (unsigned)snprintf(hex, sizeof(hex), "QG3 IVC replyhex=");
    const char *digits = "0123456789abcdef";

    for (unsigned i = 0; i < target; i++) {
        hex[pos++] = digits[reply[i] >> 4];
        hex[pos++] = digits[reply[i] & 15];
    }

    hex[pos++] = '\n';
    hex[pos] = 0;
    emit(hex);
    emit("QG3 IVC heartbeat-reply command=0x87\n");

    return 0;
}

static void ivc_probe_once(void) {
    static unsigned done;
    const char *flag = getenv("QGFX_IVC_PROBE");

    if (done || !flag || flag[0] != '1' || flag[1]) {
        return;
    }

    done = 1;
    ivc_heartbeat_probe();
}
