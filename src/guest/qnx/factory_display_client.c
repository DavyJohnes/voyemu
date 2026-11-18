/* OEM case 42 uses TCP 7777. Its reply acknowledges the callback only:
 * mega_msg_send ignores publication results, so pubsub delivery is unverified. */
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
extern int nanosleep(const void *, void *);
/* QNX 7 io-pkt ABI: rust-lang/libc 0.2.189 src/unix/nto/{mod.rs,io_pkt/mod.rs}.
 * https://github.com/rust-lang/libc/tree/0.2.189/src/unix/nto
 * F_GETFL=3 F_SETFL=4 O_NONBLOCK=0200 EINPROGRESS=236 EAGAIN=11;
 * POLLRDNORM=1 POLLOUT=2 SOL_SOCKET=0xffff SO_ERROR=0x1007 MSG_NOSIGNAL=0x0800. */
typedef struct {
    int fd;
    short events, revents;
} FactoryPoll;
typedef struct {
    long long seconds, nanoseconds;
} FactoryTime;
_Static_assert(sizeof(FactoryPoll) == 8 && sizeof(FactoryTime) == 16, "QNX factory ABI");
typedef struct {
    int (*wait)(FactoryPoll *, unsigned, int);
    int (*clock)(int, FactoryTime *);
    int *(*error)(void);
} FactoryWait;

static long long factory_now(FactoryWait *o) {
    FactoryTime t;

    if (o->clock(2, &t) || t.seconds < 0 || t.seconds > 9223372036854LL || t.nanoseconds < 0 ||
        t.nanoseconds >= 1000000000) {
        return -1;
    }

    return t.seconds * 1000 + t.nanoseconds / 1000000;
}

static int factory_ready(FactoryWait *o, int fd, short events, long long end) {
    for (unsigned spins = 0; spins < 256; spins++) {
        long long now = factory_now(o);

        if (now < 0 || now >= end) {
            return -1;
        }

        FactoryPoll p = {fd, events, 0};
        int rc = o->wait(&p, 1, (int)(end - now));

        if (rc < 0 && *o->error() == 4) {
            continue;
        }

        if (rc != 1 || (p.revents & 0x1000)) {
            return -1;
        }

        if (p.revents & events) {
            return 0; /* Readable final data may also have HUP. */
        }

        return -1;
    }

    return -1;
}

static unsigned factory_u32(const unsigned char *p) {
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static int factory_display_enabled(const char *value) {
    return value && value[0] == '1' && !value[1];
}

static int factory_display_send(void) {
    void *lib = dlopen("/lib64/libsocket.so", 2);

#define FACTORY_SYMBOL(type, name) (lib ? (type)dlsym(lib, name) : 0)
    int (*sock)(int, int, int) = FACTORY_SYMBOL(int (*)(int, int, int), "socket");
    int (*conn)(int, const void *, unsigned) = FACTORY_SYMBOL(int (*)(int, const void *, unsigned),
                                                              "connect");
    long (*snd)(int, const void *, unsigned long,
                int) = FACTORY_SYMBOL(long (*)(int, const void *, unsigned long, int), "send");
    long (*rcv)(int, void *, unsigned long,
                int) = FACTORY_SYMBOL(long (*)(int, void *, unsigned long, int), "recv");
    int (*finish)(int) = FACTORY_SYMBOL(int (*)(int), "close");
    int (*control)(int, int, ...) = FACTORY_SYMBOL(int (*)(int, int, ...), "fcntl");
    int (*option)(int, int, int, void *, unsigned *) =
            FACTORY_SYMBOL(int (*)(int, int, int, void *, unsigned *), "getsockopt");
    FactoryWait o = {FACTORY_SYMBOL(int (*)(FactoryPoll *, unsigned, int), "poll"),
                     FACTORY_SYMBOL(int (*)(int, FactoryTime *), "clock_gettime"),
                     FACTORY_SYMBOL(int *(*)(void), "__get_errno_ptr")};
#undef FACTORY_SYMBOL

    if (!sock || !conn || !snd || !rcv || !finish || !control || !option || !o.wait || !o.clock ||
        !o.error) {
        emit("QFD FACTORY unavailable=socket-symbol\n");

        return -1;
    }

    unsigned char address[16] = {16, 2, 0x1e, 0x61, 127, 0, 0, 1};
    unsigned char request[264] = {42}, reply[264];
    static const long long tick_ns[] = {0, 100000000LL};
    long long start = factory_now(&o);

    if (start < 0) {
        emit("QFD FACTORY fail=clock\n");

        return -1;
    }

    const long long end = start + 4000;

    for (unsigned attempt = 0; attempt < 40; attempt++) {
        long long now = factory_now(&o);

        if (now < 0 || now >= end) {
            break;
        }

        int fd = sock(2, 1, 0);

        if (fd < 0) {
            emit("QFD FACTORY fail=socket\n");

            return -1;
        }

        int flags = control(fd, 3, 0);

        if (flags < 0 || control(fd, 4, flags | 0200)) {
            finish(fd);
            emit("QFD FACTORY fail=nonblock\n");

            return -1;
        }

        int connected = conn(fd, address, sizeof(address)) == 0;

        if (!connected && (*o.error() == 236 || *o.error() == 237 || *o.error() == 4)) {
            if (!factory_ready(&o, fd, 2, end)) {
                int error = 0;
                unsigned size = sizeof(error);
                connected = option(fd, 0xffff, 0x1007, &error, &size) == 0 &&
                            size == sizeof(error) && error == 0;
            }
        }

        if (!connected) {
            finish(fd);
            now = factory_now(&o);

            if (now < 0 || end - now < 100) {
                break;
            }

            nanosleep(tick_ns, 0);
            continue;
        }

        /* OEM requires one 264-byte read. Partial send is ambiguous: never replay. */
        if (factory_ready(&o, fd, 2, end)) {
            finish(fd);
            emit("QFD FACTORY fail=send-deadline\n");

            return -1;
        }

        long sent = snd(fd, request, sizeof(request), 0x0800);

        if (sent != 264) {
            finish(fd);
            emit("QFD FACTORY fail=send-extent\n");

            return -1;
        }

        unsigned used = 0;

        for (unsigned reads = 0; used < sizeof(reply) && reads < 1024; reads++) {
            if (factory_ready(&o, fd, 1, end)) {
                break;
            }

            long n = rcv(fd, reply + used, sizeof(reply) - used, 0);

            if (n < 0 && (*o.error() == 4 || *o.error() == 11)) {
                continue;
            }

            if (n <= 0 || n > (long)(sizeof(reply) - used)) {
                break;
            }

            used += (unsigned)n;
        }

        finish(fd);

        if (used != sizeof(reply)) {
            emit("QFD FACTORY fail=reply-extent-or-deadline\n");

            return -1;
        }

        unsigned cmd = factory_u32(reply), status = factory_u32(reply + 4);
        char line[144];
        snprintf(line, sizeof(line),
                 "QFD FACTORY callback-reply case=%u reply_bytes=%u status=%u attempts=%u\n", cmd,
                 used, status, attempt + 1);
        emit(line);

        if (cmd != 42 || status) {
            emit("QFD FACTORY fail=callback-reply\n");

            return -1;
        }

        emit("QFD FACTORY case42-callback-accepted=1 pubsub-delivery=unverified\n");

        return 0;
    }

    emit("QFD FACTORY fail=connect-retry-exhausted\n");

    return -1;
}
