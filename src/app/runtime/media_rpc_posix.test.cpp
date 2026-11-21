#include "media_rpc_test_host.hpp"
#include <sys/stat.h>

void create_private_descriptor(const std::filesystem::path &) {}

bool assign_world_owner(const std::filesystem::path &) {
    return false;
}

bool private_frame_file(const std::filesystem::path &path, std::size_t bytes) {
    struct stat st{};

    return ::stat(path.c_str(), &st) == 0 && st.st_size == static_cast<off_t>(bytes) &&
           (st.st_mode & 0077) == 0;
}
