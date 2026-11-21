// publish_new_name of os/fs.hpp, built into voyemu_frame so that the frame transport links
// without the rest of os/.
#include "os/fs.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace voyemu::os {
Publication publish_new_name(const std::filesystem::path &from, const std::filesystem::path &to) {
    return MoveFileW(from.c_str(), to.c_str()) ? Publication::published
                                               : Publication::publish_failed;
}
} // namespace voyemu::os
