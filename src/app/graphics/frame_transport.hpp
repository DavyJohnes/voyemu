#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace voyemu::frames {
inline constexpr std::uint32_t width = 1920, height = 720, stride = width * 4, slots = 3;
inline constexpr std::size_t pixel_bytes = std::size_t(stride) * height;
struct Frame {
    std::uint64_t sequence = 0, timestamp_ns = 0;
    std::uint32_t width = frames::width, height = frames::height, stride = frames::stride;
    std::vector<std::byte> bgra;
};
// One exclusive producer; completed frames come only from original QNX Screen.
class Writer {
public:
    explicit Writer(const std::filesystem::path &);
    ~Writer();
    Writer(const Writer &) = delete;
    Writer &operator=(const Writer &) = delete;
    void publish(std::span<const std::byte> bgra, std::uint64_t sequence,
                 std::uint64_t timestamp_ns);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Called only after the exclusive producer has terminated; fences already mapped readers.
void invalidate(const std::filesystem::path &);
// Missing backing means the producer has not exported a Screen frame yet.
class Reader {
public:
    explicit Reader(std::filesystem::path);
    ~Reader();
    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
    // A replacement backing starts a new sequence domain and resets the old cursor once.
    std::optional<Frame> read_latest(std::uint64_t after_sequence = 0);
    // Last read observed no current producer frame; ordinary unchanged frames remain available.
    bool invalidated() const;
    std::uint64_t revision() const;
    // Reuses two pixel allocations; destination changes only after a complete frame is verified.
    bool read_latest(Frame &destination, std::uint64_t after_sequence = 0);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Converts only the pinned OEM Screen BMP geometry, retaining row order/colour.
std::vector<std::byte> decode_screen_bmp(std::span<const std::byte> bmp);
} // namespace voyemu::frames
