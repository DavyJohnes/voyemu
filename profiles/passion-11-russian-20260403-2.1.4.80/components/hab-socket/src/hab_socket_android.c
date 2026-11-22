/* Android backend of hab_socket.c as the libuhab.so of the OEM message center service: the
 * multimedia connects each channel to the cluster on the interguest link. */
#include <android/log.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int hab_socket_owns(int handle);
int hab_socket_open(int *handle, uint32_t mmid, uint32_t timeout, uint32_t flags);
int hab_socket_close(int handle);
int hab_socket_send(int handle, const void *data, uint32_t size, uint32_t flags);
int hab_socket_recv(int handle, void *data, uint32_t *size, uint32_t timeout, uint32_t flags);

/* The car address of the cluster on the interguest link; channel minor m is port 47200 + m. */
#define HAB_CLUSTER "172.30.100.1"
#define HAB_PORT_BASE 47200

int hab_os_open(uint32_t minor, int timeout_ms) {
    if (minor > 255) {
        return -EINVAL;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)(HAB_PORT_BASE + minor));
    inet_pton(AF_INET, HAB_CLUSTER, &address.sin_addr);

    /* The peer channel opens when the cluster starts its message center: retry once a second. */
    for (int waited = 0;; waited += 1000) {
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);

        if (fd < 0) {
            return -errno;
        }

        if (!connect(fd, (struct sockaddr *)&address, sizeof(address))) {
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            return fd;
        }

        close(fd);

        if (timeout_ms >= 0 && waited >= timeout_ms) {
            return -ETIMEDOUT;
        }

        sleep(1);
    }
}

long hab_os_read(int fd, void *data, unsigned long size) {
    return read(fd, data, size);
}

long hab_os_write(int fd, const void *data, unsigned long size) {
    return send(fd, data, size, MSG_NOSIGNAL);
}

int hab_os_wait(int fd, int timeout_ms) {
    for (;;) {
        struct pollfd p = {fd, POLLIN, 0};
        int ready = poll(&p, 1, timeout_ms);

        if (ready < 0 && errno == EINTR) {
            continue;
        }

        return ready < 0 ? -errno : ready;
    }
}

void hab_os_close(int fd) {
    close(fd);
}

void hab_os_log(const char *text) {
    __android_log_write(ANDROID_LOG_INFO, "VoyemuHab", text);
}

int hab_os_timed_out(void) {
    return -ETIMEDOUT;
}

int hab_os_interrupted(void) {
    return errno == EINTR;
}

int habmm_socket_open(int32_t *handle, uint32_t mmid, uint32_t timeout, uint32_t flags) {
    return hab_socket_open(handle, mmid, timeout, flags);
}

int habmm_socket_close(int32_t handle) {
    return hab_socket_close(handle);
}

int habmm_socket_send(int32_t handle, void *data, uint32_t size, uint32_t flags) {
    return hab_socket_send(handle, data, size, flags);
}

int habmm_socket_recv(int32_t handle, void *data, uint32_t *size, uint32_t timeout,
                      uint32_t flags) {
    return hab_socket_recv(handle, data, size, timeout, flags);
}
