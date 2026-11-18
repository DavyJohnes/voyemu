#pragma once
#include "error.hpp"
#ifdef __APPLE__
#include <cerrno>
#include <filesystem>
#include <stdexcept>
#include <sys/clonefile.h>
namespace voyemu {
inline bool clone_image(const std::filesystem::path &source, const std::filesystem::path &target,
                        int (*clone)(const char *, const char *, uint32_t) = ::clonefile) {
    if (clone(source.c_str(), target.c_str(), CLONE_NOFOLLOW) == 0) {
        return true;
    }

    const auto error = errno;

    if (error == ENOTSUP || error == EXDEV) {
        return false;
    }

    throw VoyemuError("PORTABLE_RUNTIME",
                      "exclusive image clone failed errno=" + std::to_string(error));
}
} // namespace voyemu
#endif
