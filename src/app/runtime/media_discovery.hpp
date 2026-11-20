#pragma once
#include "error.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#include "../platform/wine_native_backend_windows.hpp"
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace voyemu {
// ConfigDirs::getDiscoveryDirectory uses OS-specific roots, not TMPDIR.
// This is read-only SDK IPC; our authenticated descriptor copy stays in the session.
inline std::filesystem::path
sdk_registration_directory(const std::filesystem::path &os_root,
                           const std::filesystem::path &emulator_home) {
    const auto root = os_root.empty() ? emulator_home : os_root;

    if (!root.is_absolute()) {
        throw VoyemuError("MEDIA_DISCOVERY", "absolute SDK registration root required");
    }

#ifdef __APPLE__
    return os_root.empty() ? root / "avd/running"
                           : root / "Library/Caches/TemporaryItems/avd/running";
#elif defined(_WIN32)
    return os_root.empty() ? root / "avd/running" : root / "Temp/avd/running";
#else
    return root / "avd/running";
#endif
}

enum class SdkAdvertisementWindow {
    tail,
    startup
};

// The SDK's registration directory is a read-only input; our copy stays in the private session.
inline std::optional<std::filesystem::path>
sdk_discovery(const std::filesystem::path &log, const std::filesystem::path &root, unsigned pid,
              bool diagnostic_native = false,
              SdkAdvertisementWindow window = SdkAdvertisementWindow::tail) {
#ifndef _WIN32
    (void)diagnostic_native;
#endif

    if (!root.is_absolute() || pid < 2) {
        return std::nullopt;
    }

    std::ifstream input(log, std::ios::binary);

    if (!input) {
        return std::nullopt;
    }

    input.seekg(0, std::ios::end);
    const auto extent = input.tellg();

    if (extent <= 0) {
        return std::nullopt;
    }

    constexpr std::streamoff maximum = 512 * 1024;
    const auto length = std::min<std::streamoff>(extent, maximum);
    input.seekg(window == SdkAdvertisementWindow::startup ? std::streampos{0} : extent - length);
    std::string bytes(static_cast<std::size_t>(length), '\0');

    if (!input.read(bytes.data(), length)) {
        return std::nullopt;
    }

    const std::string marker = "Advertising in: ", name = "pid_" + std::to_string(pid) + ".ini";
    std::optional<std::filesystem::path> found;

    for (std::size_t offset = 0; (offset = bytes.find(marker, offset)) != std::string::npos;) {
        offset += marker.size();
        const auto end = bytes.find_first_of("\r\n", offset);

        if (window == SdkAdvertisementWindow::startup && length < extent &&
            end == std::string::npos) {
            break;
        }

        const auto advertised = bytes.substr(offset, end == std::string::npos ? end : end - offset);
        auto path = std::filesystem::path(advertised).lexically_normal();
#ifdef _WIN32
        if (diagnostic_native && !advertised.empty() && advertised.front() == '/') {
            try {
                path = wine_native_windows_path(advertised).lexically_normal();
            } catch (const std::exception &) {
                continue;
            }
        }
#endif

        if (!path.is_absolute() || path.filename() != name) {
            continue;
        }

        const auto relative = path.lexically_relative(root.lexically_normal());

        if (relative.empty() || *relative.begin() == "..") {
            continue;
        }

        std::error_code error;
        const auto canonical = std::filesystem::canonical(path, error);

        if (error || canonical != path) {
            continue;
        }

        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path, error)) ||
            error || std::filesystem::hard_link_count(path, error) != 1 || error) {
            continue;
        }

        const auto size = std::filesystem::file_size(path, error);

        if (error || !size || size > 32768) {
            continue;
        }

        if (found && *found != path) {
            return std::nullopt;
        }

        found = std::move(path);
    }

    return found;
}

inline std::optional<std::filesystem::path>
import_sdk_descriptor(const std::filesystem::path &source, const std::filesystem::path &target,
                      unsigned pid, unsigned serial, unsigned grpc, std::string *bearer = nullptr) {
    const auto name = "pid_" + std::to_string(pid) + ".ini";

    if (source.filename() != name || target.filename() != name || !target.is_absolute()) {
        throw VoyemuError("MEDIA_DESCRIPTOR", "exact owned PID filename required");
    }

    std::string bytes;
#ifndef _WIN32
    int input = ::open(source.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    if (input < 0) {
        return std::nullopt;
    }

    struct Close {
        int fd;

        ~Close() {
            ::close(fd);
        }
    } close{input};
    struct stat info{};

    if (::fstat(input, &info) || !S_ISREG(info.st_mode) || info.st_uid != ::geteuid() ||
        info.st_nlink != 1 || info.st_size <= 0 || info.st_size > 32768) {
        throw VoyemuError("MEDIA_DESCRIPTOR", "invalid SDK input ownership/extent");
    }

    bytes.resize(info.st_size);
    std::size_t offset = 0;

    while (offset < bytes.size()) {
        const auto count = ::read(input, bytes.data() + offset, bytes.size() - offset);

        if (count <= 0) {
            return std::nullopt;
        }

        offset += count;
    }
#else
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(source)) ||
        std::filesystem::hard_link_count(source) != 1) {
        throw VoyemuError("MEDIA_DESCRIPTOR", "invalid SDK input");
    }

    const auto size = std::filesystem::file_size(source);

    if (!size || size > 32768) {
        throw VoyemuError("MEDIA_DESCRIPTOR", "invalid SDK extent");
    }

    std::ifstream input(source, std::ios::binary);
    bytes.resize(size);

    if (!input.read(bytes.data(), size)) {
        return std::nullopt;
    }
#endif
    std::map<std::string, std::string> fields;
    std::istringstream lines(bytes);
    std::string line;

    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const auto at = line.find('=');

        if (at != std::string::npos &&
            !fields.emplace(line.substr(0, at), line.substr(at + 1)).second) {
            throw VoyemuError("MEDIA_DESCRIPTOR", "duplicate SDK field");
        }
    }

    if (!fields.contains("grpc.port") || !fields.contains("port.serial") ||
        !fields.contains("grpc.token")) {
        return std::nullopt;
    }

    if (fields["grpc.port"] != std::to_string(grpc) ||
        fields["port.serial"] != std::to_string(serial) || fields["grpc.token"].empty() ||
        fields["grpc.token"].size() > 4096) {
        throw VoyemuError("MEDIA_DESCRIPTOR", "SDK endpoint binding differs");
    }

    if (bearer) {
        *bearer = fields.at("grpc.token");
    }

#ifndef _WIN32
    const int output = ::open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                              0600);

    if (output < 0) {
        if (errno != EEXIST) {
            throw VoyemuError("MEDIA_DESCRIPTOR", "private destination creation failed");
        }

        const int existing = ::open(target.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

        if (existing < 0) {
            throw VoyemuError("MEDIA_DESCRIPTOR", "existing private destination rejected");
        }

        Close existing_close{existing};
        struct stat saved{};

        if (::fstat(existing, &saved) || !S_ISREG(saved.st_mode) || saved.st_uid != ::geteuid() ||
            saved.st_nlink != 1 || (saved.st_mode & 0077) != 0 ||
            saved.st_size != static_cast<off_t>(bytes.size())) {
            throw VoyemuError("MEDIA_DESCRIPTOR",
                              "existing private destination ownership/extent differs");
        }

        std::string previous(bytes.size(), char(0));
        std::size_t read = 0;

        while (read < previous.size()) {
            const auto count = ::read(existing, previous.data() + read, previous.size() - read);

            if (count <= 0) {
                throw VoyemuError("MEDIA_DESCRIPTOR", "existing private destination unreadable");
            }

            read += count;
        }

        if (previous != bytes) {
            throw VoyemuError("MEDIA_DESCRIPTOR", "existing private destination identity differs");
        }

        return target;
    }

    bool ok = true;
    std::size_t written = 0;

    while (written < bytes.size()) {
        const auto count = ::write(output, bytes.data() + written, bytes.size() - written);

        if (count <= 0) {
            ok = false;
            break;
        }

        written += count;
    }

    if (::fsync(output)) {
        ok = false;
    }

    if (::close(output)) {
        ok = false;
    }

    if (!ok) {
        ::unlink(target.c_str());
        throw VoyemuError("MEDIA_DESCRIPTOR", "private copy failed");
    }
#else
    if (std::filesystem::exists(target) || std::filesystem::is_symlink(target)) {
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(target)) ||
            std::filesystem::hard_link_count(target) != 1 ||
            std::filesystem::file_size(target) != bytes.size()) {
            throw VoyemuError("MEDIA_DESCRIPTOR", "existing private destination rejected");
        }

        std::ifstream existing(target, std::ios::binary);
        std::string previous(bytes.size(), char(0));

        if (!existing.read(previous.data(), previous.size()) || previous != bytes) {
            throw VoyemuError("MEDIA_DESCRIPTOR", "existing private destination identity differs");
        }

        return target;
    }

    std::ofstream output(target, std::ios::binary);
    output.write(bytes.data(), bytes.size());

    if (!output) {
        throw VoyemuError("MEDIA_DESCRIPTOR", "private copy failed");
    }
#endif

    return target;
}

// Only a descriptor authenticated during this owned SDK launch may survive a guest reboot.
// The caller keeps the ChildProcess lifetime and listener fences; a new SDK launch resets this
// cache.
class SdkDescriptorCache {
public:
    bool authenticated() const {
        return source_.has_value();
    }

    const std::string &bearer() const {
        return bearer_;
    }

    void reset() {
        source_.reset();
        bearer_.clear();
    }

    std::optional<std::filesystem::path>
    resolve(const std::filesystem::path &log, const std::filesystem::path &root,
            const std::filesystem::path &directory, unsigned pid, unsigned serial, unsigned grpc,
            bool guest_identity_verified, bool diagnostic_native = false) {
        if (source_ && !guest_identity_verified) {
            return std::nullopt;
        }

        const auto source = source_ ? source_ : sdk_discovery(log, root, pid, diagnostic_native);

        if (!source) {
            return std::nullopt;
        }

        const auto target = directory / source->filename();

        // Revalidation compares the original SDK input with the existing private authenticated
        // copy. Losing that copy must not turn a retained path into authority to import new
        // credentials.
        if (source_ && !std::filesystem::is_regular_file(std::filesystem::symlink_status(target))) {
            throw VoyemuError("MEDIA_DESCRIPTOR", "authenticated private destination missing");
        }

        std::string bearer;
        const auto copied = import_sdk_descriptor(*source, target, pid, serial, grpc, &bearer);

        if (copied) {
            source_ = source;
            bearer_ = std::move(bearer);
        }

        return copied;
    }

private:
    std::optional<std::filesystem::path> source_;
    std::string bearer_;
};
} // namespace voyemu
