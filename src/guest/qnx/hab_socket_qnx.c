/* QNX backend of hab_socket.c: the cluster listens on its interguest address for each channel.
 * The preload serves habmm_* only in a process started with VOYEMU_HAB_SOCKET=1 and only for
 * MM_MISC; every other call goes to the OEM libuhab unchanged. */
#include "hab_socket.c"
#ifndef QHB_TEST
#define QHB_ALIAS(n) __asm__(#n)
#else
#define QHB_ALIAS(n)
#endif
typedef struct {
    int fd;
    short events, revents;
} QhbPoll;
_Static_assert(sizeof(QhbPoll) == 8, "pinned QNX pollfd");
typedef struct {
    HabU8 length, family;
    HabU8 port[2], address[4];
    HabU8 zero[8];
} QhbSockaddr;
_Static_assert(sizeof(QhbSockaddr) == 16, "pinned QNX sockaddr_in");
extern char *qhb_env(const char *) QHB_ALIAS(getenv);
extern void *qhb_dlopen(const char *, int) QHB_ALIAS(dlopen);
extern void *qhb_dlsym(void *, const char *) QHB_ALIAS(dlsym);
extern int qhb_close(int) QHB_ALIAS(close);
extern int qhb_poll(QhbPoll *, unsigned, int) QHB_ALIAS(poll);
extern long qhb_read(int, void *, HabSize) QHB_ALIAS(read);
extern long qhb_write(int, const void *, HabSize) QHB_ALIAS(write);
extern int *qhb_errno(void) QHB_ALIAS(__get_errno_ptr);
extern int qhb_sleep(unsigned) QHB_ALIAS(usleep);
#undef QHB_ALIAS

/* The car address of the cluster on the interguest link; channel minor m listens on 47200 + m. */
#define QHB_PORT_BASE 47200U
#define QHB_MINORS 256U

typedef struct {
    int (*socket)(int, int, int);
    int (*setsockopt)(int, int, int, const void *, unsigned);
    int (*bind)(int, const void *, unsigned);
    int (*listen)(int, int);
    int (*accept)(int, void *, unsigned *);
} QhbSocketApi;

static QhbSocketApi qhb_net;
static int qhb_listeners[QHB_MINORS]; /* descriptor + 1 */

static int qhb_resolve(void) {
    if (qhb_net.accept) {
        return 0;
    }

    /* QNX RTLD_NOW. */
    void *net = qhb_dlopen("libsocket.so.3", 2);

    if (!net) {
        return -1;
    }

    qhb_net.socket = (int (*)(int, int, int))qhb_dlsym(net, "socket");
    qhb_net.setsockopt = (int (*)(int, int, int, const void *, unsigned))qhb_dlsym(net,
                                                                                   "setsockopt");
    qhb_net.bind = (int (*)(int, const void *, unsigned))qhb_dlsym(net, "bind");
    qhb_net.listen = (int (*)(int, int))qhb_dlsym(net, "listen");
    int (*accept)(int, void *, unsigned *) = (int (*)(int, void *, unsigned *))qhb_dlsym(net,
                                                                                         "accept");

    if (!qhb_net.socket || !qhb_net.setsockopt || !qhb_net.bind || !qhb_net.listen || !accept) {
        return -1;
    }

    __atomic_store_n(&qhb_net.accept, accept, __ATOMIC_RELEASE);

    return 0;
}

/* One listener per minor stays open, so a reopened channel accepts the next connection. */
static int qhb_listener(HabU32 minor) {
    int fd = __atomic_load_n(&qhb_listeners[minor], __ATOMIC_ACQUIRE) - 1;

    if (fd >= 0) {
        return fd;
    }

    HabU32 port = QHB_PORT_BASE + minor;
    QhbSockaddr sa = {16, 2, {(HabU8)(port >> 8), (HabU8)port}, {172, 30, 100, 1}, {0}};
    int one = 1;
    /* QNX AF_INET 2, SOCK_STREAM 1, SOL_SOCKET 0xffff, SO_REUSEADDR 4. */
    fd = qhb_net.socket(2, 1, 0);

    if (fd < 0) {
        return -1;
    }

    if (qhb_net.setsockopt(fd, 0xffff, 4, &one, sizeof(one)) || qhb_net.bind(fd, &sa, sizeof(sa)) ||
        qhb_net.listen(fd, 1)) {
        qhb_close(fd);

        return -1;
    }

    int empty = 0;

    if (!__atomic_compare_exchange_n(&qhb_listeners[minor], &empty, fd + 1, 0, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        qhb_close(fd);

        return empty - 1;
    }

    return fd;
}

int hab_os_open(HabU32 minor, int timeout_ms) {
    if (minor >= QHB_MINORS || qhb_resolve()) {
        return -HAB_EIO;
    }

    /* tap0 takes its address from the same runtime: retry the bind once a second. */
    for (int waited = 0;; waited += 1000) {
        int listener = qhb_listener(minor);

        if (listener >= 0) {
            int ready = hab_os_wait(listener, timeout_ms);

            if (ready <= 0) {
                return ready == 0 ? hab_os_timed_out() : ready;
            }

            int fd = qhb_net.accept(listener, 0, 0);

            return fd >= 0 ? fd : -HAB_EIO;
        }

        if (timeout_ms >= 0 && waited >= timeout_ms) {
            return hab_os_timed_out();
        }

        qhb_sleep(1000000);
    }
}

long hab_os_read(int fd, void *data, HabSize size) {
    return qhb_read(fd, data, size);
}

long hab_os_write(int fd, const void *data, HabSize size) {
    return qhb_write(fd, data, size);
}

int hab_os_wait(int fd, int timeout_ms) {
    for (;;) {
        QhbPoll p = {fd, 1, 0};
        int ready = qhb_poll(&p, 1, timeout_ms);

        if (ready < 0 && *qhb_errno() == 4) {
            continue;
        }

        return ready < 0 ? -HAB_EIO : ready;
    }
}

void hab_os_close(int fd) {
    qhb_close(fd);
}

void hab_os_log(const char *text) {
    HabSize n = 0;

    while (text[n]) {
        ++n;
    }

    (void)qhb_write(2, text, n);
}

/* QNX ETIMEDOUT 260, EINTR 4. */
int hab_os_timed_out(void) {
    return -260;
}

int hab_os_interrupted(void) {
    return *qhb_errno() == 4;
}

typedef struct {
    int (*open)(int *, HabU32, HabU32, HabU32);
    int (*close)(int);
    int (*send)(int, void *, HabU32, HabU32);
    int (*recv)(int, void *, HabU32 *, HabU32, HabU32);
} QhbUhab;

static int qhb_enabled(void) {
    const char *mode = qhb_env("VOYEMU_HAB_SOCKET");

    return mode && mode[0] == '1' && !mode[1];
}

/* The OEM libuhab of the process for every call this preload does not serve. */
static const QhbUhab *qhb_uhab(void) {
    static QhbUhab uhab;

    if (!__atomic_load_n(&uhab.recv, __ATOMIC_ACQUIRE)) {
        void *library = qhb_dlopen("libuhab.so", 2);

        if (library) {
            uhab.open = (int (*)(int *, HabU32, HabU32, HabU32))qhb_dlsym(library,
                                                                          "habmm_socket_open");
            uhab.close = (int (*)(int))qhb_dlsym(library, "habmm_socket_close");
            uhab.send = (int (*)(int, void *, HabU32, HabU32))qhb_dlsym(library,
                                                                        "habmm_socket_send");
            int (*recv)(int, void *, HabU32 *, HabU32, HabU32) = (int (*)(
                    int, void *, HabU32 *, HabU32, HabU32))qhb_dlsym(library, "habmm_socket_recv");
            __atomic_store_n(&uhab.recv, recv, __ATOMIC_RELEASE);
        }
    }

    return &uhab;
}

int habmm_socket_open(int *handle, HabU32 mmid, HabU32 timeout, HabU32 flags) {
    if (qhb_enabled() && hab_socket_serves(mmid)) {
        return hab_socket_open(handle, mmid, timeout, flags);
    }

    const QhbUhab *u = qhb_uhab();

    return u->open ? u->open(handle, mmid, timeout, flags) : -HAB_EIO;
}

int habmm_socket_close(int handle) {
    if (hab_socket_owns(handle)) {
        return hab_socket_close(handle);
    }

    const QhbUhab *u = qhb_uhab();

    return u->close ? u->close(handle) : -HAB_EIO;
}

int habmm_socket_send(int handle, void *data, HabU32 size, HabU32 flags) {
    if (hab_socket_owns(handle)) {
        return hab_socket_send(handle, data, size, flags);
    }

    const QhbUhab *u = qhb_uhab();

    return u->send ? u->send(handle, data, size, flags) : -HAB_EIO;
}

int habmm_socket_recv(int handle, void *data, HabU32 *size, HabU32 timeout, HabU32 flags) {
    if (hab_socket_owns(handle)) {
        return hab_socket_recv(handle, data, size, timeout, flags);
    }

    const QhbUhab *u = qhb_uhab();

    return u->recv ? u->recv(handle, data, size, timeout, flags) : -HAB_EIO;
}
