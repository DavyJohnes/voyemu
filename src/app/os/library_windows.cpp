#include "library.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace voyemu::os {
// The library and its dependencies resolve from the system directory only.
Library::Library(const std::string &name)
    : handle_(reinterpret_cast<void *>(
              ::LoadLibraryExA(name.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))) {
    if (!handle_) {
        failure_ = "LoadLibraryExA failed with error " + std::to_string(::GetLastError());
    }
}

Library::Library(const std::filesystem::path &path)
    : handle_(reinterpret_cast<void *>(::LoadLibraryExW(
              path.c_str(), nullptr,
              LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))) {
    if (!handle_) {
        failure_ = "LoadLibraryExW failed with error " + std::to_string(::GetLastError());
    }
}

Library::~Library() {
    if (handle_) {
        ::FreeLibrary(static_cast<HMODULE>(handle_));
    }
}

void *Library::symbol(const char *name) const {
    return handle_ ? reinterpret_cast<void *>(::GetProcAddress(static_cast<HMODULE>(handle_), name))
                   : nullptr;
}
} // namespace voyemu::os
