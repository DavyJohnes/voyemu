#include "listener_policy.h"
#include <assert.h>
int main(void)
{
    assert(!listener_poll_fatal(0));
    assert(!listener_poll_fatal(POLLIN));
    assert(listener_poll_fatal(POLLNVAL));
    assert(listener_poll_fatal(POLLERR | POLLIN));
    assert(listener_poll_fatal(POLLHUP));
    assert(!listener_accept_fatal(EAGAIN));
    assert(!listener_accept_fatal(EINTR));
    assert(!listener_accept_fatal(ECONNABORTED));
    assert(listener_accept_fatal(EBADF));
    assert(listener_accept_fatal(EMFILE));
    return 0;
}
