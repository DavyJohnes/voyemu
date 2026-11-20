#ifndef VOYEMU_DEV_SOURCE_LEASE_H
#define VOYEMU_DEV_SOURCE_LEASE_H
#include <poll.h>
/* A lease is a quiet connection; readability, hangup, or error invalidates it. */
static inline int source_lease_broken(int fd)
{
    struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0 };
    int n = poll(&p, 1, 0);
    if (n < 0) return -1;
    return n > 0 && (p.revents & (POLLIN | POLLERR | POLLHUP | POLLNVAL)) ? 1 : 0;
}
#endif
