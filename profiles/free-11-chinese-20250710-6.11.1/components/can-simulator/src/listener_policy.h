#ifndef VOYEMU_DEV_LISTENER_POLICY_H
#define VOYEMU_DEV_LISTENER_POLICY_H
#include <errno.h>
#include <poll.h>
static inline int listener_poll_fatal(short events)
{
    return (events & (POLLERR | POLLHUP | POLLNVAL)) != 0;
}
static inline int listener_accept_fatal(int error)
{
    return error != EAGAIN && error != EINTR && error != ECONNABORTED;
}
#endif
