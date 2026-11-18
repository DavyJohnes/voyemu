#include "source_lease.h"
#include <assert.h>
#include <sys/socket.h>
#include <unistd.h>
int main(void)
{
    int fds[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    assert(source_lease_broken(fds[0]) == 0);
    assert(write(fds[1], "x", 1) == 1);
    assert(source_lease_broken(fds[0]) == 1);
    close(fds[0]); close(fds[1]);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) == 0);
    close(fds[1]);
    assert(source_lease_broken(fds[0]) == 1);
    close(fds[0]);
    return 0;
}
