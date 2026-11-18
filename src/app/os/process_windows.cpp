#include "error.hpp"
#include "process.hpp"
#include <stdexcept>
#include <string>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace voyemu::os {
std::int64_t current_process() {
    return GetCurrentProcessId();
}

std::filesystem::path executable_path() {
    std::vector<wchar_t> buffer(32768);
    const auto size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

    if (!size || size >= buffer.size()) {
        throw VoyemuError("PROCESS_PATH", "cannot resolve own executable");
    }

    return std::filesystem::canonical(std::wstring(buffer.data(), size));
}

FileUse file_use(const std::filesystem::path &path) {
    // An exclusive open (share mode 0) is the Windows holder probe: success proves no foreign
    // handle, a sharing violation proves a live holder.
    const auto probe = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                   FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (probe == INVALID_HANDLE_VALUE) {
        return FileUse::held;
    }

    CloseHandle(probe);

    return FileUse::unused;
}
} // namespace voyemu::os
