#include "capture_png.hpp"
#include "model.hpp"
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
namespace voyemu::output {
void write_artifact(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    if (!path.is_absolute() || bytes.empty() || bytes.size() > 40 * 1024 * 1024) {
        throw VoyemuError("TEST_ARTIFACT", "bounded absolute artifact required");
    }

    ensure_private_directory(path.parent_path());
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NOFOLLOW | O_CLOEXEC, 0600);

    if (fd < 0) {
        throw VoyemuError("TEST_ARTIFACT_EXISTS", "artifact must be new");
    }

    bool complete = true;
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);

        if (n < 0 && errno == EINTR) {
            continue;
        }

        if (n <= 0) {
            complete = false;
            break;
        }

        offset += n;
    }

    if (::fsync(fd)) {
        complete = false;
    }

    ::close(fd);

    if (!complete) {
        ::unlink(path.c_str());
        throw VoyemuError("TEST_ARTIFACT_IO", "artifact write failed");
    }
}
} // namespace voyemu::output
