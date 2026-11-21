#include "error.hpp"
#include "os/random.hpp"
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
namespace voyemu::os {
void random_bytes(unsigned char *data, std::size_t size) {
    int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);

    if (fd < 0) {
        throw VoyemuError("RANDOM", "system RNG failed");
    }

    std::size_t offset = 0;

    while (offset < size) {
        auto n = ::read(fd, data + offset, size - offset);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            ::close(fd);
            throw VoyemuError("RANDOM", "incomplete system RNG");
        }

        offset += n;
    }

    ::close(fd);
}
} // namespace voyemu::os
