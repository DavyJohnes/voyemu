#include "deps_metadata.hpp"
#include "error.hpp"
#include <sys/stat.h>
#include <unistd.h>
namespace voyemu {
void verify_owner_controlled_metadata(const std::filesystem::path &file) {
    struct stat status{};

    if (::lstat(file.c_str(), &status) != 0 || status.st_uid != ::geteuid() ||
        (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        throw VoyemuError("DEPS_METADATA", "generated SDK metadata must be owner-controlled");
    }
}
} // namespace voyemu
