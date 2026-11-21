#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace voyemu::os {
// A new private file of a fixed size mapped for reading and writing, shared with other processes
// by name; the object unmaps it and removes the file while the name still holds it.
class PrivateRegion {
public:
    // The step of create() that failed, or created.
    enum class Creation {
        created,
        create_failed,
        extent_failed,
        ownership_failed,
        mapping_failed
    };

    PrivateRegion() = default;
    ~PrivateRegion();
    PrivateRegion(const PrivateRegion &) = delete;
    PrivateRegion &operator=(const PrivateRegion &) = delete;

    Creation create(const std::filesystem::path &path, std::size_t bytes);
    // Whether the name still holds the created file, private to the user with its size.
    bool current() const;

    void *data() const {
        return data_;
    }

private:
    void release() noexcept;

    std::filesystem::path path_;
    std::size_t bytes_ = 0;
    void *data_ = nullptr;
    // A descriptor on POSIX; a file HANDLE and its mapping HANDLE on Windows.
    std::intptr_t file_ = -1;
    [[maybe_unused]] std::intptr_t mapping_ = 0;
    std::uint64_t volume_ = 0, index_ = 0;
};
} // namespace voyemu::os
