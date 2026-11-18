#include "check.hpp"
#include "graphics/graphics.hpp"
#include "graphics/graphics_ram.hpp"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace voyemu::graphics::detail {
namespace {
constexpr Check need{"GRAPHICS"};
} // namespace

struct Ram::Impl {
    std::byte *data = nullptr;
    std::filesystem::path path;
    int fd = -1;
    dev_t device{};
    ino_t inode{};
    const std::uint64_t mapped_bytes;

    explicit Impl(const Options &options) : path(options.ram), mapped_bytes(options.ram_extent) {}

    ~Impl() {
        if (data) {
            munmap(data, mapped_bytes);
        }

        if (fd >= 0) {
            close(fd);
        }
    }
};

Ram::Ram(const Options &options) : impl_(std::make_unique<Impl>(options)) {
    auto &p = *impl_;

    validate_ram_extent(ram_base, 1, p.mapped_bytes);
    p.fd = open(p.path.c_str(), O_RDWR | O_NOFOLLOW);
    need(p.fd >= 0, "RAM backing open");
    struct stat s{};
    need(!fstat(p.fd, &s) && S_ISREG(s.st_mode) && std::uint64_t(s.st_size) == p.mapped_bytes,
         "RAM backing geometry");
    p.device = s.st_dev;
    p.inode = s.st_ino;
    void *mapped = mmap(nullptr, p.mapped_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, p.fd, 0);

    if (mapped != MAP_FAILED) {
        p.data = static_cast<std::byte *>(mapped);
    }

    need(p.data != nullptr, "RAM mapping failed");
}

Ram::~Ram() = default;

std::span<std::byte> Ram::mapped() const {
    return {impl_->data, std::size_t(impl_->mapped_bytes)};
}

void Ram::check_identity() {
    const auto &p = *impl_;
    struct stat s{};

    need(!lstat(p.path.c_str(), &s) && S_ISREG(s.st_mode) && s.st_dev == p.device &&
                 s.st_ino == p.inode && std::uint64_t(s.st_size) == p.mapped_bytes,
         "RAM backing changed");
}

std::string read_control_file(const std::filesystem::path &path, std::size_t size) {
    const int fd = open(path.c_str(), O_RDONLY | O_NOFOLLOW);

    need(fd >= 0, "Control file open");
    struct Cleanup {
        int fd;

        ~Cleanup() {
            close(fd);
        }
    } cleanup{fd};
    struct stat st{};
    need(!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_size == std::int64_t(size),
         "Control file race");
    std::string text(size, '\0');
    std::size_t at = 0;

    while (at < size) {
        const auto n = read(fd, text.data() + at, size - at);
        need(n > 0, "Control file read");
        at += std::size_t(n);
    }

    return text;
}

std::filesystem::path angle_loader_file(const std::filesystem::path &angle) {
#ifdef __linux__
    // Preload the pinned loader before ANGLE requests libvulkan.so.1 by its bare name.
    return angle / "libvulkan.so.1";
#else
    (void)angle;

    return {};
#endif
}
} // namespace voyemu::graphics::detail
