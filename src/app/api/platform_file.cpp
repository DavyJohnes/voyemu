#include "platform_file.hpp"
#include "model.hpp"
#include "platform_file_internal.hpp"
#include <limits>

namespace voyemu::api {
struct FileLock::Impl {
    platform::File file;
};

FileLock::FileLock(const std::filesystem::path &path) : impl_(std::make_unique<Impl>()) {
    ensure_private_directory(path.parent_path());

    if (!platform::lock(impl_->file, path, true)) {
        throw VoyemuError("TEST_BUSY", "another automation writer or capture is active");
    }
}

FileLock::~FileLock() = default;

bool file_lock_released(const std::filesystem::path &path) {
    platform::File probe;

    return platform::lock(probe, path, false);
}

struct LogFile::Impl {
    platform::File file;
    std::string identity;
    std::uint64_t size{};

    explicit Impl(const std::filesystem::path &path) {
        const auto opening = platform::open_log(file, path);

        if (!opening.opened) {
            throw VoyemuError("TEST_LOGS_UNAVAILABLE", "supervisor log unavailable");
        }

        if (!opening.ordinary) {
            throw VoyemuError("TEST_LOGS", "ordinary owned log required");
        }

        identity = opening.identity;
        size = opening.size;
    }
};

LogFile::LogFile(const std::filesystem::path &path) : impl_(std::make_unique<Impl>(path)) {}

LogFile::~LogFile() = default;

std::string LogFile::identity() const {
    return impl_->identity;
}

std::uint64_t LogFile::size() const {
    return impl_->size;
}

std::string LogFile::read(std::uint64_t offset) const {
    std::string bytes(65536, '\0');

    if (offset > std::uint64_t(std::numeric_limits<std::int64_t>::max())) {
        throw VoyemuError("TEST_LOGS", "bounded log read failed");
    }

    const auto count = platform::read_at(impl_->file, offset, bytes.data(), bytes.size());

    if (count < 0) {
        throw VoyemuError("TEST_LOGS", "bounded log read failed");
    }

    bytes.resize(static_cast<std::size_t>(count));

    return bytes;
}
} // namespace voyemu::api
