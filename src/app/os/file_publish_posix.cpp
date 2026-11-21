// publish_new_name of os/fs.hpp, built into voyemu_frame so that the frame transport links
// without the rest of os/.
#include "os/fs.hpp"
#include <unistd.h>

namespace voyemu::os {
Publication publish_new_name(const std::filesystem::path &from, const std::filesystem::path &to) {
    if (::link(from.c_str(), to.c_str())) {
        return Publication::publish_failed;
    }

    return ::unlink(from.c_str()) ? Publication::release_failed : Publication::published;
}
} // namespace voyemu::os
