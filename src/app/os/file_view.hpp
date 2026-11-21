#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace voyemu::os {
// A shared memory view of a whole file of a fixed size, released with the object or reset().
class FileView {
public:
    // The outcome of open(); every failure leaves the view closed after reset().
    enum class Opening {
        opened,
        // The file does not exist and create was not set.
        absent,
        open_failed,
        // The path names a link, a directory or another non-regular file.
        not_ordinary,
        resize_failed,
        // The file has another size than the view.
        extent_differs,
        mapping_failed,
        view_failed
    };
    // Whether the path still names the viewed file.
    enum class Identity {
        same,
        replaced,
        absent,
        identity_failed,
        not_ordinary
    };

    FileView() = default;
    ~FileView();
    FileView(const FileView &) = delete;
    FileView &operator=(const FileView &) = delete;

    // Maps path with bytes, creating it exclusively at that size when create is set.
    Opening open(const std::filesystem::path &path, std::size_t bytes, bool create, bool writable);
    Identity current(const std::filesystem::path &path) const;
    void reset();

    std::byte *data() const {
        return data_;
    }

private:
    std::byte *data_ = nullptr;
    std::size_t bytes_ = 0;
    // A descriptor on POSIX; a file HANDLE and its mapping HANDLE on Windows.
    std::intptr_t file_ = -1;
    [[maybe_unused]] std::intptr_t mapping_ = 0;
};
} // namespace voyemu::os
