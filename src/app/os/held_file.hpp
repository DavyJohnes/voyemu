#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace voyemu::os {
// An ordinary single-link file of the current user, not writable by group or others on POSIX,
// held open by a handle that shares writes with its producer; the object closes it.
class HeldFile {
public:
    // The outcome of open().
    enum class Opening {
        opened,
        open_failed,
        // The file is linked, foreign, writable by others or of another type.
        invalid
    };

    HeldFile() = default;
    ~HeldFile();
    HeldFile(const HeldFile &) = delete;
    HeldFile &operator=(const HeldFile &) = delete;

    Opening open(const std::filesystem::path &path, bool writable);
    bool bound() const;
    void close() noexcept;
    // The size of the held file while both the handle and its path still name the opened file and
    // it stays as open() required; none otherwise.
    std::optional<std::uint64_t> current_size() const;
    // Reads at most size bytes at offset: the byte count, or -1 on failure.
    long long read_at(std::uint64_t offset, void *data, std::size_t size) const;

    // The outcome of map().
    enum class Mapping {
        mapped,
        // The host maps at a coarser alignment than a 64 KiB page window allows.
        granularity,
        handle_failed,
        view_failed
    };
    // Maps bytes at offset for reading and writing, replacing an earlier view.
    Mapping map(std::uint64_t offset, std::size_t bytes);

    // The address of the mapped offset; null without a view.
    void *view() const {
        return address_;
    }

    void unmap() noexcept;

private:
    std::filesystem::path path_;
    // A descriptor on POSIX; a HANDLE and its mapping HANDLE on Windows.
    std::intptr_t handle_ = -1;
    [[maybe_unused]] std::intptr_t mapping_ = 0;
    void *base_ = nullptr, *address_ = nullptr;
    std::size_t length_ = 0;
    std::uint64_t volume_ = 0, index_ = 0;
};

// Whether path belongs to the current user and no other user may write it; true on Windows, whose
// callers check owners on handles.
bool sealed_to_user(const std::filesystem::path &path);
} // namespace voyemu::os
