#include "apk_alignment.hpp"
#include "check.hpp"
#include "little_endian.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "os/process.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string_view>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cwchar>

#include <windows.h>
#else
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace voyemu {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_ALIGNMENT", "invalid classic ZIP or unaligned stored payload");
}

void valid(bool condition, const char *) {
    if (!condition) {
        invalid();
    }
}

unsigned u16(std::string_view bytes, std::size_t at) {
    return unsigned(le_read(bytes, at, 2, valid, nullptr));
}

std::uint64_t u32(std::string_view bytes, std::size_t at) {
    return le_read(bytes, at, 4, valid, nullptr);
}
} // namespace

void verify_pinned_apk_alignment(const std::filesystem::path &file, const std::string &pin) {
    const auto inventory = pinned_zip_inventory(file, pin);
    const auto size = std::filesystem::file_size(file);

    if (size < 22 || size > (2ULL << 30) || inventory.size() > 65535) {
        invalid();
    }

    std::ifstream input(file, std::ios::binary);
    auto read = [&](std::uint64_t offset, std::size_t count) {
        if (offset > size || count > size - offset) {
            invalid();
        }

        std::string bytes(count, '\0');
        input.seekg(offset);
        input.read(bytes.data(), count);

        if (!input) {
            invalid();
        }

        return bytes;
    };
    const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, 65557));
    const auto tail = read(size - tail_size, tail_size);
    std::size_t end = tail.size() - 22;

    while (true) {
        if (u32(tail, end) == 0x06054b50 && u16(tail, end + 20) == tail.size() - end - 22) {
            break;
        }

        if (!end) {
            invalid();
        }

        --end;
    }

    if (u16(tail, end + 4) || u16(tail, end + 6) || u16(tail, end + 8) != inventory.size() ||
        u16(tail, end + 10) != inventory.size()) {
        invalid();
    }

    const auto central_size = u32(tail, end + 12), central = u32(tail, end + 16);
    const auto end_offset = size - tail_size + end;

    if (central > size || central_size > size - central || central + central_size != end_offset) {
        invalid();
    }

    std::map<std::string, ZipEntry> expected;

    for (const auto &entry : inventory) {
        expected.emplace(entry.name, entry);
    }

    std::uint64_t cursor = central;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;

    for (std::size_t index = 0; index < inventory.size(); ++index) {
        const auto header = read(cursor, 46);

        if (u32(header, 0) != 0x02014b50 || u16(header, 34)) {
            invalid();
        }

        const auto flags = u16(header, 8), method = u16(header, 10), name_length = u16(header, 28),
                   extra = u16(header, 30), comment = u16(header, 32);
        const auto compressed = u32(header, 20), uncompressed = u32(header, 24),
                   local = u32(header, 42);
        const auto record_size = 46ULL + name_length + extra + comment;

        if (cursor > central + central_size || record_size > central + central_size - cursor ||
            compressed == 0xffffffff || uncompressed == 0xffffffff || local == 0xffffffff) {
            invalid();
        }

        const auto name = read(cursor + 46, name_length);
        const auto found = expected.find(name);

        if (found == expected.end() || found->second.bytes != uncompressed) {
            invalid();
        }

        const auto local_header = read(local, 30);

        if (u32(local_header, 0) != 0x04034b50 || u16(local_header, 6) != flags ||
            u16(local_header, 8) != method || u16(local_header, 26) != name_length ||
            read(local + 30, name_length) != name) {
            invalid();
        }

        if (!(flags & 8) &&
            (u32(local_header, 14) != u32(header, 16) || u32(local_header, 18) != compressed ||
             u32(local_header, 22) != uncompressed)) {
            invalid();
        }

        const auto data = local + 30ULL + name_length + u16(local_header, 28);

        if (data > central || compressed > central - data) {
            invalid();
        }

        if (method == 0 && !found->second.directory) {
            const unsigned alignment = name.ends_with(".so") ? 4096 : 4;

            if (compressed != uncompressed || data % alignment) {
                invalid();
            }
        }

        extents.emplace_back(local, data + compressed);
        expected.erase(found);
        cursor += record_size;
    }

    if (cursor != central + central_size || !expected.empty()) {
        invalid();
    }

    std::sort(extents.begin(), extents.end());

    for (std::size_t index = 1; index < extents.size(); ++index) {
        if (extents[index].first < extents[index - 1].second) {
            invalid();
        }
    }

    // Guard against a source changing while metadata was checked.
    pinned_zip_inventory(file, pin);
}

std::string align_pinned_apk(const std::filesystem::path &source, const std::string &source_pin,
                             const std::filesystem::path &tool, const std::string &tool_pin,
                             const std::filesystem::path &destination, std::uint64_t reserve) {
    if (!destination.is_absolute() || destination.filename().empty() ||
        destination.filename() == "." || destination.filename() == ".." ||
        std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        throw VoyemuError("APK_ALIGN_OUTPUT", "new absolute destination required");
    }

    if (std::filesystem::file_size(source) > (2ULL << 30)) {
        throw VoyemuError("APK_ALIGN_BOUND", "classic APK extent required");
    }

    const auto entries = pinned_zip_inventory(source, source_pin);

    if (entries.size() > 65535) {
        throw VoyemuError("APK_ALIGN_BOUND", "classic ZIP count required");
    }

    if (!tool.is_absolute() || std::filesystem::is_symlink(tool) ||
        !std::filesystem::is_regular_file(tool) ||
        std::filesystem::file_size(tool) > 4 * 1024 * 1024 || !sha256_text(tool_pin) ||
        sha256_file(tool) != tool_pin) {
        throw VoyemuError("APK_ALIGN_TOOL", "bounded pinned native tool required");
    }

    const auto parent = destination.parent_path();
    auto existing = parent;

    while (!std::filesystem::exists(existing) && existing != existing.parent_path()) {
        existing = existing.parent_path();
    }

    const std::uint64_t peak = std::filesystem::file_size(source) + entries.size() * 4096ULL +
                               std::filesystem::file_size(tool) + 1024 * 1024;

    if (reserve > std::numeric_limits<std::uint64_t>::max() - peak ||
        std::filesystem::space(existing).available < reserve + peak) {
        throw VoyemuError("APK_ALIGN_SPACE", "reserve plus planned output required");
    }

#ifdef _WIN32
    bool loader_override = false;

    if (const auto environment = ::GetEnvironmentStringsW()) {
        for (const wchar_t *entry = environment; *entry; entry += std::wcslen(entry) + 1) {
            const std::wstring row(entry);
            const auto equal = row.find(L'=', row.starts_with(L'=') ? 1 : 0);

            if (equal == std::wstring::npos) {
                continue;
            }

            const auto name = row.substr(0, equal);

            if ((name.starts_with(L"DYLD_") || name == L"LD_LIBRARY_PATH") &&
                equal + 1 < row.size()) {
                loader_override = true;
            }
        }

        ::FreeEnvironmentStringsW(environment);
    }

    if (loader_override) {
        throw VoyemuError("APK_ALIGN_ENV", "loader overrides are not accepted");
    }
#else
    for (char **entry = os::environment(); *entry; ++entry) {
        std::string_view value(*entry);
        const auto equal = value.find('=');

        if (equal == std::string_view::npos) {
            continue;
        }

        const auto name = value.substr(0, equal);

        if ((name.starts_with("DYLD_") || name == "LD_LIBRARY_PATH") && equal + 1 < value.size()) {
            throw VoyemuError("APK_ALIGN_ENV", "loader overrides are not accepted");
        }
    }
#endif
    ensure_private_directory(parent);
    const auto stage = parent /
                       ("." + destination.filename().string() + ".align-" + random_token());

    if (!std::filesystem::create_directory(stage)) {
        throw VoyemuError("APK_ALIGN_OUTPUT", "exclusive private stage failed");
    }

    ensure_private_directory(stage);
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{stage};
    const auto executable = stage / "zipalign", candidate = stage / "aligned.apk";
    const auto tool_size = std::filesystem::file_size(tool);

    if (tool_size > 4 * 1024 * 1024) {
        throw VoyemuError("APK_ALIGN_TOOL", "tool grew");
    }

    std::ifstream tool_input(tool, std::ios::binary);
    std::string tool_bytes(tool_size, '\0');
    tool_input.read(tool_bytes.data(), tool_bytes.size());

    if (!tool_input || tool_input.peek() != std::char_traits<char>::eof() ||
        sha256_bytes(tool_bytes) != tool_pin) {
        throw VoyemuError("APK_ALIGN_TOOL", "tool changed during staging");
    }

    std::ofstream tool_output(executable, std::ios::binary);
    tool_output.write(tool_bytes.data(), tool_bytes.size());
    tool_output.close();

    if (!tool_output) {
        throw VoyemuError("APK_ALIGN_TOOL", "tool staging failed");
    }

    std::filesystem::permissions(executable, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);

    if (sha256_file(executable) != tool_pin) {
        throw VoyemuError("APK_ALIGN_TOOL", "staged tool pin mismatch");
    }

    ProcessSpec spec;
    spec.executable = executable;
    spec.directory = stage;
    spec.output = stage / "alignment.log";
    spec.arguments = {"-p", "4", std::filesystem::absolute(source).string(), candidate.string()};
    const auto result = execute(spec, 30000, 128 * 1024);

    if (result.timed_out || result.exit_code != 0) {
        throw VoyemuError("APK_ALIGN_TOOL", "alignment failed or timed out");
    }

    std::filesystem::permissions(
            candidate, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
#ifdef _WIN32
    const auto candidate_handle = ::CreateFileW(candidate.c_str(), GENERIC_READ | GENERIC_WRITE,
                                                FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                FILE_ATTRIBUTE_NORMAL, nullptr);

    if (candidate_handle == INVALID_HANDLE_VALUE) {
        throw VoyemuError("APK_ALIGN_OUTPUT", "candidate open failed");
    }

    const auto synced = ::FlushFileBuffers(candidate_handle) != 0;
    const auto closed = ::CloseHandle(candidate_handle) != 0;

    if (!synced || !closed) {
        throw VoyemuError("APK_ALIGN_OUTPUT", "candidate sync failed");
    }
#else
    int fd = ::open(candidate.c_str(), O_RDONLY | O_NOFOLLOW);

    if (fd < 0) {
        throw VoyemuError("APK_ALIGN_OUTPUT", "candidate open failed");
    }

    const auto synced = ::fsync(fd), closed = ::close(fd);

    if (synced || closed) {
        throw VoyemuError("APK_ALIGN_OUTPUT", "candidate sync failed");
    }
#endif
    const auto digest = sha256_file(candidate);
    verify_pinned_apk_alignment(candidate, digest);
    std::map<std::string, std::pair<std::uint64_t, bool>> expected;

    for (const auto &item : entries) {
        expected.emplace(item.name, std::pair{item.bytes, item.directory});
    }

    for (const auto &item : pinned_zip_inventory(candidate, digest)) {
        const auto found = expected.find(item.name);

        if (found == expected.end() || found->second != std::pair{item.bytes, item.directory}) {
            throw VoyemuError("APK_ALIGN_DATA", "archive inventory changed");
        }

        expected.erase(found);
    }

    if (!expected.empty() || sha256_file(source) != source_pin || sha256_file(tool) != tool_pin) {
        throw VoyemuError("APK_ALIGN_DATA", "source/tool changed");
    }

    if (!os::rename_exclusive(candidate, destination)) {
        throw VoyemuError("APK_ALIGN_OUTPUT", "exclusive publication failed");
    }

    return digest;
}
} // namespace voyemu
