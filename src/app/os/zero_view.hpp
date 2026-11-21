#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace voyemu::os {
// A read-only view of zero bytes, released with the object: an anonymous mapping on POSIX, a
// sparse temporary file of directory deleted on close on Windows.
class ZeroView {
public:
    // The step of open() that failed, or mapped.
    enum class Opening {
        mapped,
        file_failed,
        sparse_failed,
        extent_failed,
        mapping_failed,
        view_failed
    };

    ZeroView() = default;
    ~ZeroView();
    ZeroView(const ZeroView &) = delete;
    ZeroView &operator=(const ZeroView &) = delete;

    Opening open(std::size_t bytes, const std::filesystem::path &directory);

    const void *data() const {
        return data_;
    }

private:
    void *data_ = nullptr;
    std::size_t bytes_ = 0;
    // The temporary file and its mapping on Windows.
    [[maybe_unused]] std::intptr_t file_ = -1, mapping_ = 0;
};
} // namespace voyemu::os
