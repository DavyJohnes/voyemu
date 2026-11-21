#pragma once
#include <cstdint>
#include <filesystem>

namespace voyemu::os {
// A private disk image held open for durable flushes; the handle closes with the object.
class DurableDisk {
public:
    DurableDisk() = default;
    ~DurableDisk();
    DurableDisk(const DurableDisk &) = delete;
    DurableDisk &operator=(const DurableDisk &) = delete;

    // Opens path for reading and writing: true when it is an ordinary file of the current user with
    // one link, private to that user, writable and exactly bytes long.
    bool open(const std::filesystem::path &path, std::uint64_t bytes);
    // Whether both disks are the same file.
    bool same_file(const DurableDisk &other) const;
    // Whether the held file and the file at its path are still the opened one with its size.
    bool current() const;
    // Flushes data and metadata to the device; false on failure.
    bool flush();
    void close() noexcept;

    // The descriptor on POSIX or the HANDLE on Windows, for fault injection by tests.
    std::intptr_t native_handle() const {
        return handle_;
    }

private:
    std::filesystem::path path_;
    // A descriptor on POSIX, a HANDLE on Windows.
    std::intptr_t handle_ = -1;
    std::uint64_t volume_ = 0, index_ = 0, size_ = 0;
    // The creation time on Windows, which tells a recreated file apart; unused on POSIX.
    [[maybe_unused]] std::uint64_t created_ = 0;
};
} // namespace voyemu::os
