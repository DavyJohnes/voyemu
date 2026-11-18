#include "library.hpp"
#include <dlfcn.h>

namespace voyemu::os {
// The dynamic loader searches its configured system directories for a bare file name.
Library::Library(const std::string &name) : handle_(dlopen(name.c_str(), RTLD_NOW | RTLD_LOCAL)) {
    if (!handle_) {
        const char *reason = dlerror();
        failure_ = reason ? reason : "dlopen failed";
    }
}

Library::Library(const std::filesystem::path &path) : Library(path.string()) {}

Library::~Library() {
    if (handle_) {
        dlclose(handle_);
    }
}

void *Library::symbol(const char *name) const {
    return handle_ ? dlsym(handle_, name) : nullptr;
}
} // namespace voyemu::os
