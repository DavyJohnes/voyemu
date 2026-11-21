#include "os/zero_view.hpp"
#include <sys/mman.h>

namespace voyemu::os {
ZeroView::~ZeroView() {
    if (data_) {
        munmap(data_, bytes_);
    }
}

ZeroView::Opening ZeroView::open(std::size_t bytes, const std::filesystem::path &) {
    bytes_ = bytes;
    void *data = mmap(nullptr, bytes, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);

    if (data == MAP_FAILED) {
        return Opening::view_failed;
    }

    data_ = data;

    return Opening::mapped;
}
} // namespace voyemu::os
