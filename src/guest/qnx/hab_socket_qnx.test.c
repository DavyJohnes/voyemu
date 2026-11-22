#ifdef NDEBUG
#undef NDEBUG
#endif
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define QHB_TEST 1
#define qhb_env getenv
#define qhb_close close
#define qhb_read read
#define qhb_write write
#define qhb_sleep usleep
#include "hab_socket_qnx.c"

static unsigned port_offset, forwarded_open, forwarded_close;
static HabU8 bound_length, bound_family, bound_address[4];

int qhb_poll(QhbPoll *p, unsigned n, int timeout) {
    return poll((struct pollfd *)p, n, timeout);
}

int *qhb_errno(void) {
    return &errno;
}

/* Binds the QNX sockaddr_in the backend builds on loopback with a per-run port offset. */
static int fake_bind(int fd, const void *address, unsigned size) {
    const QhbSockaddr *q = address;
    struct sockaddr_in linux_address = {0};

    assert(size == sizeof(QhbSockaddr));
    bound_length = q->length;
    bound_family = q->family;
    memcpy(bound_address, q->address, 4);
    linux_address.sin_family = AF_INET;
    linux_address.sin_port = htons((unsigned short)((q->port[0] << 8 | q->port[1]) + port_offset));
    assert(inet_pton(AF_INET, "127.0.0.1", &linux_address.sin_addr) == 1);

    return bind(fd, (struct sockaddr *)&linux_address, sizeof(linux_address));
}

static int fake_setsockopt(int fd, int level, int name, const void *value, unsigned size) {
    assert(level == 0xffff && name == 4);

    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, value, size);
}

static int fake_accept(int fd, void *address, unsigned *size) {
    assert(!address && !size);

    return accept(fd, 0, 0);
}

static int uhab_open(int *handle, HabU32 mmid, HabU32 timeout, HabU32 flags) {
    (void)mmid;
    (void)timeout;
    (void)flags;
    ++forwarded_open;
    *handle = 7;

    return 0;
}

static int uhab_close(int handle) {
    assert(handle == 7);
    ++forwarded_close;

    return 0;
}

static int uhab_send(int handle, void *data, HabU32 size, HabU32 flags) {
    (void)handle;
    (void)data;
    (void)size;
    (void)flags;

    return 0;
}

static int uhab_recv(int handle, void *data, HabU32 *size, HabU32 timeout, HabU32 flags) {
    (void)handle;
    (void)data;
    (void)size;
    (void)timeout;
    (void)flags;

    return 0;
}

void *qhb_dlopen(const char *name, int mode) {
    assert(mode == 2);

    return (void *)name;
}

void *qhb_dlsym(void *library, const char *name) {
    const char *l = library;

    struct {
        const char *library, *name;
        void *function;
    } table[] = {
            {"libsocket.so.3", "socket", (void *)socket},
            {"libsocket.so.3", "setsockopt", (void *)fake_setsockopt},
            {"libsocket.so.3", "bind", (void *)fake_bind},
            {"libsocket.so.3", "listen", (void *)listen},
            {"libsocket.so.3", "accept", (void *)fake_accept},
            {"libuhab.so", "habmm_socket_open", (void *)uhab_open},
            {"libuhab.so", "habmm_socket_close", (void *)uhab_close},
            {"libuhab.so", "habmm_socket_send", (void *)uhab_send},
            {"libuhab.so", "habmm_socket_recv", (void *)uhab_recv},
    };

    for (unsigned i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (!strcmp(table[i].library, l) && !strcmp(table[i].name, name)) {
            return table[i].function;
        }
    }

    return 0;
}

/* The multimedia end of a channel, as the Android libuhab connects it. */
static int peer(HabU32 minor) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address = {0};

    address.sin_family = AF_INET;
    address.sin_port = htons((unsigned short)(QHB_PORT_BASE + minor + port_offset));
    assert(inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1);
    assert(!connect(fd, (struct sockaddr *)&address, sizeof(address)));

    return fd;
}

static void frame(int fd, const void *data, HabU32 size) {
    HabU8 head[4] = {(HabU8)(size >> 24), (HabU8)(size >> 16), (HabU8)(size >> 8), (HabU8)size};

    assert(write(fd, head, 4) == 4 && write(fd, data, size) == (long)size);
}

static HabU32 receive(int fd, void *data, HabU32 capacity) {
    HabU8 head[4];

    assert(!hab_read_all(fd, head, 4));
    HabU32 size = (HabU32)head[0] << 24 | (HabU32)head[1] << 16 | (HabU32)head[2] << 8 | head[3];
    assert(size <= capacity && !hab_read_all(fd, data, size));

    return size;
}

int main(void) {
    const HabU32 input = 601U | 16U << 16;
    char buffer[512];
    HabU32 size;
    int handle = 0;

    port_offset = (unsigned)(getpid() % 1000) * 4;

    /* Without the opt-in every call reaches the OEM libuhab. */
    unsetenv("VOYEMU_HAB_SOCKET");
    assert(!habmm_socket_open(&handle, input, 0xffffffffU, 0) && handle == 7 &&
           forwarded_open == 1);
    assert(!habmm_socket_close(handle) && forwarded_close == 1);

    setenv("VOYEMU_HAB_SOCKET", "1", 1);
    /* Other HAB clients of the process keep the OEM channels. */
    assert(!habmm_socket_open(&handle, 502U | 16U << 16, 0xffffffffU, 0) && forwarded_open == 2);

    /* A first open without a peer times out but leaves the listener of the minor bound. */
    assert(habmm_socket_open(&handle, input, 1, 0) == -260);
    assert(bound_length == 16 && bound_family == 2 && bound_address[0] == 172 &&
           bound_address[1] == 30 && bound_address[2] == 100 && bound_address[3] == 1);
    int fd = peer(16);
    assert(!habmm_socket_open(&handle, input, 0xffffffffU, 0) && hab_socket_owns(handle));

    /* libmrpc receives the 8-byte header and the body as two whole messages. */
    frame(fd, "HEADER08", 8);
    frame(fd, "{\"value\":6}", 11);
    size = 8;
    assert(!habmm_socket_recv(handle, buffer, &size, 0xffffffffU, 0) && size == 8 &&
           !memcmp(buffer, "HEADER08", 8));
    size = sizeof(buffer);
    assert(!habmm_socket_recv(handle, buffer, &size, 0xffffffffU, 0) && size == 11 &&
           !memcmp(buffer, "{\"value\":6}", 11));

    /* A message longer than the buffer is consumed and refused; the next one stays whole. */
    memset(buffer, 'x', 300);
    frame(fd, buffer, 300);
    frame(fd, "next", 4);
    size = 8;
    assert(habmm_socket_recv(handle, buffer, &size, 0xffffffffU, 0) == -HAB_EINVAL && size == 0);
    size = 8;
    assert(!habmm_socket_recv(handle, buffer, &size, 0xffffffffU, 0) && size == 4 &&
           !memcmp(buffer, "next", 4));

    /* A bounded wait without data reports the QNX timeout. */
    size = 8;
    assert(habmm_socket_recv(handle, buffer, &size, 50, 0) == -260);

    assert(!habmm_socket_send(handle, "abc", 3, 0) && receive(fd, buffer, sizeof(buffer)) == 3 &&
           !memcmp(buffer, "abc", 3));
    assert(habmm_socket_send(handle, "abc", 0, 0) == -HAB_EINVAL);

    /* A closed peer fails the channel; libmrpc closes and reopens it on the kept listener. */
    close(fd);
    size = 8;
    assert(habmm_socket_recv(handle, buffer, &size, 0xffffffffU, 0) == -HAB_EIO);
    assert(!habmm_socket_close(handle) && habmm_socket_send(handle, "a", 1, 0) == -HAB_EINVAL);
    fd = peer(16);
    assert(!habmm_socket_open(&handle, input, 0xffffffffU, 0));
    frame(fd, "again", 5);
    size = 8;
    assert(!habmm_socket_recv(handle, buffer, &size, 0xffffffffU, 0) && size == 5);
    assert(!habmm_socket_close(handle));
    close(fd);
    puts("hab_socket ok");

    return 0;
}
