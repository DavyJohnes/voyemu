#include "check.hpp"
#include "graphics/graphics.hpp"
#include "graphics/graphics_ram.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace voyemu::graphics::detail {
namespace {
constexpr Check need{"GRAPHICS"};
} // namespace

struct Ram::Impl {
    std::byte *data = nullptr;
    std::filesystem::path path;
    HANDLE file = INVALID_HANDLE_VALUE, mapping = nullptr;
    DWORD volume = 0, file_index_high = 0, file_index_low = 0;
    const std::uint64_t mapped_bytes;

    explicit Impl(const Options &options) : path(options.ram), mapped_bytes(options.ram_extent) {}

    ~Impl() {
        if (data) {
            UnmapViewOfFile(data);
        }

        if (mapping) {
            CloseHandle(mapping);
        }

        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
    }
};

Ram::Ram(const Options &options) : impl_(std::make_unique<Impl>(options)) {
    auto &p = *impl_;

    validate_ram_extent(ram_base, 1, p.mapped_bytes);
    p.file = CreateFileW(p.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    need(p.file != INVALID_HANDLE_VALUE, "RAM backing open");
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};
    need(GetFileInformationByHandle(p.file, &info) &&
                 !(info.dwFileAttributes &
                   (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                 GetFileSizeEx(p.file, &size) && size.QuadPart == p.mapped_bytes,
         "RAM backing geometry");
    p.volume = info.dwVolumeSerialNumber;
    p.file_index_high = info.nFileIndexHigh;
    p.file_index_low = info.nFileIndexLow;
    p.mapping = CreateFileMappingW(p.file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    need(p.mapping != nullptr, "RAM mapping");
    p.data = static_cast<std::byte *>(
            MapViewOfFile(p.mapping, FILE_MAP_ALL_ACCESS, 0, 0, p.mapped_bytes));
    need(p.data != nullptr, "RAM mapping failed");
}

Ram::~Ram() = default;

std::span<std::byte> Ram::mapped() const {
    return {impl_->data, std::size_t(impl_->mapped_bytes)};
}

void Ram::check_identity() {
    const auto &p = *impl_;
    // Re-open the backing path and require the identity and size captured by the mapping handle.
    const HANDLE current = CreateFileW(p.path.c_str(), FILE_READ_ATTRIBUTES,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT,
                                       nullptr);
    struct Cleanup {
        HANDLE value;

        ~Cleanup() {
            if (value != INVALID_HANDLE_VALUE) {
                CloseHandle(value);
            }
        }
    } cleanup{current};
    BY_HANDLE_FILE_INFORMATION info{};
    LARGE_INTEGER size{};

    need(current != INVALID_HANDLE_VALUE && GetFileInformationByHandle(current, &info) &&
                 !(info.dwFileAttributes &
                   (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) &&
                 info.dwVolumeSerialNumber == p.volume &&
                 info.nFileIndexHigh == p.file_index_high &&
                 info.nFileIndexLow == p.file_index_low && GetFileSizeEx(current, &size) &&
                 size.QuadPart == p.mapped_bytes,
         "RAM backing changed");
}

std::string read_control_file(const std::filesystem::path &path, std::size_t size) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    need(file != INVALID_HANDLE_VALUE, "Control file open");
    struct Cleanup {
        HANDLE h;

        ~Cleanup() {
            CloseHandle(h);
        }
    } cleanup{file};
    BY_HANDLE_FILE_INFORMATION info{};
    need(GetFileInformationByHandle(file, &info) &&
                 !(info.dwFileAttributes &
                   (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)),
         "Control file race");
    std::string text(size, '\0');
    DWORD read = 0;
    need(ReadFile(file, text.data(), DWORD(size), &read, nullptr) && read == size,
         "Control file read");

    return text;
}

std::filesystem::path angle_loader_file(const std::filesystem::path &angle) {
    // ANGLE requests vulkan-1.dll by its bare name; register the pinned directory under Wine.
    SetDllDirectoryW(angle.c_str());

    return {};
}
} // namespace voyemu::graphics::detail
