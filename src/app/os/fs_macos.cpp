#include "fs.hpp"
#include <fcntl.h>
#include <stdio.h>

namespace voyemu::os {
bool rename_exclusive(const std::filesystem::path &from, const std::filesystem::path &to) {
    return ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL) == 0;
}

bool exchange_paths(const std::filesystem::path &first, const std::filesystem::path &second) {
    return ::renamex_np(first.c_str(), second.c_str(), RENAME_SWAP) == 0;
}
} // namespace voyemu::os
