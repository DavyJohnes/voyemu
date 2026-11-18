#include "frame_transport.hpp"
#include "error.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voyemu::frames {
namespace {
constexpr std::uint64_t magic = 0x3152474246585156ULL;
struct alignas(64) Header {
    std::uint64_t magic_value;
    std::uint32_t version, header_bytes, width, height, stride, slot_count;
    std::uint64_t file_bytes, slot_bytes;
    alignas(64) std::uint64_t published;
};
struct alignas(64) Slot {
    std::uint64_t stamp, sequence, timestamp_ns;
    std::uint32_t width, height, stride, reserved;
};
static_assert(sizeof(Header) == 128 && sizeof(Slot) == 64);
constexpr std::size_t slot_bytes = sizeof(Slot) + pixel_bytes;
constexpr std::size_t file_bytes = sizeof(Header) + slots * slot_bytes;

std::uint64_t acquire(std::uint64_t &value) {
    return std::atomic_ref(value).load(std::memory_order_acquire);
}

void release(std::uint64_t &value, std::uint64_t replacement) {
    std::atomic_ref(value).store(replacement, std::memory_order_release);
}

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
              "Shared transport needs lock-free 64-bit atomics");
struct Mapping {
    std::byte *data = nullptr;
#ifdef _WIN32
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
#else
    int fd = -1;
#endif

    ~Mapping() {
        reset();
    }

    void reset() {
#ifdef _WIN32
        if (data) {
            UnmapViewOfFile(data);
        }

        if (mapping) {
            CloseHandle(mapping);
        }

        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
#else
        if (data) {
            munmap(data, file_bytes);
        }

        if (fd >= 0) {
            close(fd);
        }
#endif
        data = nullptr;
#ifdef _WIN32
        file = INVALID_HANDLE_VALUE;
        mapping = nullptr;
#else
        fd = -1;
#endif
    }

    bool open(const std::filesystem::path &path, bool create, bool writable = false) {
        writable = writable || create;
#ifdef _WIN32
        file = CreateFileW(path.c_str(), writable ? GENERIC_READ | GENERIC_WRITE : GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           create ? CREATE_NEW : OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (file == INVALID_HANDLE_VALUE) {
            if (!create && GetLastError() == ERROR_FILE_NOT_FOUND) {
                return false;
            }

            throw VoyemuError("FRAME_TRANSPORT", "Frame backing open failed");
        }

        BY_HANDLE_FILE_INFORMATION info{};

        if (!GetFileInformationByHandle(file, &info) ||
            (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing ownership");
        }

        LARGE_INTEGER size{};
        size.QuadPart = file_bytes;

        if (create && (!SetFilePointerEx(file, size, nullptr, FILE_BEGIN) || !SetEndOfFile(file))) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing resize");
        }

        if (!GetFileSizeEx(file, &size) || size.QuadPart != file_bytes) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing extent");
        }

        mapping = CreateFileMappingW(file, nullptr, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0,
                                     nullptr);

        if (!mapping) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame mapping failed");
        }

        data = static_cast<std::byte *>(
                MapViewOfFile(mapping, writable ? FILE_MAP_WRITE | FILE_MAP_READ : FILE_MAP_READ, 0,
                              0, file_bytes));
#else
        fd = ::open(path.c_str(),
                    (create     ? O_RDWR | O_CREAT | O_EXCL
                     : writable ? O_RDWR
                                : O_RDONLY) |
                            O_NOFOLLOW,
                    0600);

        if (fd < 0) {
            if (!create && errno == ENOENT) {
                return false;
            }

            throw VoyemuError("FRAME_TRANSPORT", "Frame backing open failed");
        }

        struct stat st{};

        if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing ownership");
        }

        if (create && ftruncate(fd, file_bytes)) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing resize");
        }

        if (fstat(fd, &st) || st.st_size != file_bytes) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing extent");
        }

        void *p = mmap(nullptr, file_bytes, writable ? PROT_READ | PROT_WRITE : PROT_READ,
                       MAP_SHARED, fd, 0);

        if (p != MAP_FAILED) {
            data = static_cast<std::byte *>(p);
        }
#endif

        if (!data) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame view failed");
        }

        return true;
    }

    bool current(const std::filesystem::path &path) {
#ifdef _WIN32
        HANDLE candidate =
                CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (candidate == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) {
                return false;
            }

            throw VoyemuError("FRAME_TRANSPORT", "Frame backing identity");
        }

        BY_HANDLE_FILE_INFORMATION original{}, replacement{};
        const bool valid = GetFileInformationByHandle(file, &original) &&
                           GetFileInformationByHandle(candidate, &replacement);
        CloseHandle(candidate);

        if (!valid || (replacement.dwFileAttributes &
                       (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing ownership");
        }

        return original.dwVolumeSerialNumber == replacement.dwVolumeSerialNumber &&
               original.nFileIndexHigh == replacement.nFileIndexHigh &&
               original.nFileIndexLow == replacement.nFileIndexLow;
#else
        struct stat original{}, replacement{};

        if (lstat(path.c_str(), &replacement)) {
            if (errno == ENOENT) {
                return false;
            }

            throw VoyemuError("FRAME_TRANSPORT", "Frame backing identity");
        }

        if (fstat(fd, &original) || !S_ISREG(replacement.st_mode)) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame backing ownership");
        }
        return original.st_dev == replacement.st_dev && original.st_ino == replacement.st_ino;
#endif
    }

    Header &header() {
        return *reinterpret_cast<Header *>(data);
    }

    Slot &slot(std::uint64_t seq) {
        return *reinterpret_cast<Slot *>(data + sizeof(Header) + (seq % slots) * slot_bytes);
    }

    std::byte *pixels(std::uint64_t seq) {
        return reinterpret_cast<std::byte *>(&slot(seq)) + sizeof(Slot);
    }

    void validate() {
        auto &h = header();

        if (acquire(h.magic_value) != magic || h.version != 1 || h.header_bytes != sizeof(Header) ||
            h.width != width || h.height != height || h.stride != stride || h.slot_count != slots ||
            h.file_bytes != file_bytes || h.slot_bytes != slot_bytes) {
            throw VoyemuError("FRAME_TRANSPORT", "Frame ABI geometry");
        }
    }
};

std::uint32_t u32(std::span<const std::byte> b, std::size_t at) {
    if (at + 4 > b.size()) {
        throw VoyemuError("FRAME_TRANSPORT", "Screen header extent");
    }

    const auto *p = reinterpret_cast<const unsigned char *>(b.data() + at);

    return std::uint32_t(p[0]) | std::uint32_t(p[1]) << 8 | std::uint32_t(p[2]) << 16 |
           std::uint32_t(p[3]) << 24;
}

std::uint16_t u16(std::span<const std::byte> b, std::size_t at) {
    return std::uint16_t(std::to_integer<unsigned char>(b[at]) |
                         std::to_integer<unsigned char>(b[at + 1]) << 8);
}
} // namespace
struct Writer::Impl {
    Mapping map;
    std::uint64_t last = 0;
};

Writer::Writer(const std::filesystem::path &path) : impl_(std::make_unique<Impl>()) {
    const auto pending = std::filesystem::path(path.string() + ".pending");

    if (std::filesystem::exists(path) || std::filesystem::is_symlink(path)) {
        throw VoyemuError("FRAME_TRANSPORT", "Frame producer already exists");
    }

    impl_->map.open(pending, true);
    auto &h = impl_->map.header();
    std::memset(impl_->map.data, 0, sizeof(Header));
    h.version = 1;
    h.header_bytes = sizeof(Header);
    h.width = width;
    h.height = height;
    h.stride = stride;
    h.slot_count = slots;
    h.file_bytes = file_bytes;
    h.slot_bytes = slot_bytes;
    release(h.magic_value, magic);
#ifdef _WIN32
    if (!MoveFileW(pending.c_str(), path.c_str())) {
        throw VoyemuError("FRAME_TRANSPORT", "Frame backing publish");
    }
#else
    if (link(pending.c_str(), path.c_str())) {
        throw VoyemuError("FRAME_TRANSPORT", "Frame backing publish");
    }

    if (unlink(pending.c_str())) {
        throw VoyemuError("FRAME_TRANSPORT", "Frame pending release");
    }
#endif
}

Writer::~Writer() = default;

void Writer::publish(std::span<const std::byte> bytes, std::uint64_t sequence,
                     std::uint64_t timestamp_ns) {
    if (bytes.size() != pixel_bytes || !sequence || sequence <= impl_->last ||
        sequence > std::numeric_limits<std::uint64_t>::max() / 2) {
        throw VoyemuError("FRAME_TRANSPORT", "Frame publish sequence/extent");
    }

    auto &slot = impl_->map.slot(sequence);
    // Sequential fence keeps writes to pixels after the in-progress marker.
    std::atomic_ref(slot.stamp).store(sequence * 2 + 1, std::memory_order_seq_cst);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    slot.sequence = sequence;
    slot.timestamp_ns = timestamp_ns;
    slot.width = width;
    slot.height = height;
    slot.stride = stride;
    slot.reserved = 0;
    std::memcpy(impl_->map.pixels(sequence), bytes.data(), bytes.size());
    release(slot.stamp, sequence * 2);
    release(impl_->map.header().published, sequence);
    impl_->last = sequence;
}

void invalidate(const std::filesystem::path &path) {
    Mapping map;

    if (!map.open(path, false, true)) {
        return;
    }

    map.validate();
    release(map.header().published, 0);
}

struct Reader::Impl {
    std::filesystem::path path;
    Mapping map;
    std::vector<std::byte> scratch;
    bool restarted = false, available = false;
    std::uint64_t revision = 0;

    explicit Impl(std::filesystem::path p) : path(std::move(p)) {}
};

Reader::Reader(std::filesystem::path path) : impl_(std::make_unique<Impl>(std::move(path))) {}

Reader::~Reader() = default;

bool Reader::invalidated() const {
    return !impl_->available;
}

std::uint64_t Reader::revision() const {
    return impl_->revision;
}

std::optional<Frame> Reader::read_latest(std::uint64_t after) {
    Frame frame;

    if (!read_latest(frame, after)) {
        return std::nullopt;
    }

    return frame;
}

bool Reader::read_latest(Frame &destination, std::uint64_t after) {
    if (impl_->map.data && !impl_->map.current(impl_->path)) {
        impl_->map.reset();
        impl_->restarted = true;
        impl_->available = false;
        ++impl_->revision;
    }

    if (!impl_->map.data && !impl_->map.open(impl_->path, false)) {
        return false;
    }

    impl_->map.validate();

    for (unsigned attempt = 0; attempt < 3; ++attempt) {
        const auto seq = acquire(impl_->map.header().published);

        if (!seq) {
            if (impl_->available) {
                ++impl_->revision;
            }

            impl_->available = false;

            return false;
        }

        if (seq <= (impl_->restarted ? 0 : after) ||
            seq > std::numeric_limits<std::uint64_t>::max() / 2) {
            return false;
        }

        auto &s = impl_->map.slot(seq);

        if (acquire(s.stamp) != seq * 2) {
            continue;
        }

        Frame frame;
        frame.sequence = s.sequence;
        frame.timestamp_ns = s.timestamp_ns;
        frame.width = s.width;
        frame.height = s.height;
        frame.stride = s.stride;

        if (frame.sequence != seq || frame.width != width || frame.height != height ||
            frame.stride != stride || s.reserved) {
            // A writer may have reused this slot after the first stamp read.
            if (acquire(s.stamp) != seq * 2 || acquire(impl_->map.header().published) != seq) {
                continue;
            }

            throw VoyemuError("FRAME_TRANSPORT", "Frame slot geometry");
        }

        impl_->scratch.resize(pixel_bytes);
        std::memcpy(impl_->scratch.data(), impl_->map.pixels(seq), pixel_bytes);
        std::atomic_thread_fence(std::memory_order_acquire);

        if (!impl_->map.current(impl_->path)) {
            impl_->map.reset();
            impl_->restarted = true;
            impl_->available = false;
            ++impl_->revision;

            return false;
        }

        const auto completed_stamp = acquire(s.stamp),
                   published = acquire(impl_->map.header().published);

        if (!published) {
            if (impl_->available) {
                ++impl_->revision;
            }

            impl_->available = false;

            return false;
        }

        if (completed_stamp == seq * 2 && published == seq) {
            destination.bgra.swap(impl_->scratch);
            destination.sequence = frame.sequence;
            destination.timestamp_ns = frame.timestamp_ns;
            destination.width = frame.width;
            destination.height = frame.height;
            destination.stride = frame.stride;
            impl_->restarted = false;
            impl_->available = true;

            // The previous completed allocation is retained as the next scratch buffer.
            return true;
        }
    }

    return false;
}

std::vector<std::byte> decode_screen_bmp(std::span<const std::byte> bmp) {
    if (bmp.size() < 54 || bmp.size() > pixel_bytes + 4096 || bmp[0] != std::byte{'B'} ||
        bmp[1] != std::byte{'M'}) {
        throw VoyemuError("FRAME_TRANSPORT", "Screen BMP signature/extent");
    }

    auto offset = u32(bmp, 10), dib = u32(bmp, 14), w = u32(bmp, 18);
    auto signed_height = std::int32_t(u32(bmp, 22));
    auto bpp = u16(bmp, 28);

    if (dib != 40 || w != width ||
        (signed_height != std::int32_t(height) && signed_height != -std::int32_t(height)) ||
        u16(bmp, 26) != 1 || (bpp != 24 && bpp != 32) || u32(bmp, 30) != 0) {
        throw VoyemuError("FRAME_TRANSPORT", "Screen BMP geometry/format");
    }

    const std::size_t row_bytes = ((std::size_t(width) * bpp + 31) / 32) * 4;

    if (offset < 54 || offset > 4096 || offset + row_bytes * height != bmp.size()) {
        throw VoyemuError("FRAME_TRANSPORT", "Screen BMP pixel extent");
    }

    std::vector<std::byte> pixels(pixel_bytes);

    for (std::size_t y = 0; y < height; ++y) {
        const auto *src = bmp.data() + offset +
                          (signed_height < 0 ? y : height - 1 - y) * row_bytes;
        auto *dst = pixels.data() + y * stride;

        if (bpp == 32) {
            // Fixed-size words let the Release compiler vectorize this hot Screen path.
            // memcpy tolerates the BMP's unaligned offset54; no uint32 pointer aliasing.
            for (std::size_t x = 0; x < width; ++x) {
                std::array<std::byte, 4> pixel;
                std::memcpy(pixel.data(), src + x * 4, 4);
                pixel[3] = std::byte{255};
                std::memcpy(dst + x * 4, pixel.data(), 4);
            }
        } else {
            for (std::size_t x = 0; x < width; ++x) {
                dst[x * 4] = src[x * 3];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = std::byte{255};
            }
        }
    }

    return pixels;
}
} // namespace voyemu::frames
