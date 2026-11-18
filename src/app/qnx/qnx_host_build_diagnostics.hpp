#pragma once
#include "error.hpp"
#include "model.hpp"
#include <algorithm>
#include <fstream>
#include <string>
namespace voyemu {
inline void retain_qnx_host_build_diagnostics(const std::filesystem::path &stage,
                                              const std::filesystem::path &logs) {
    ensure_private_directory(logs);
    std::uint64_t remaining = 64ULL << 20;
    auto retain = [&](const std::filesystem::path &relative, const std::string &name) {
        auto path = stage;
        std::filesystem::file_status status;

        for (const auto &part : relative) {
            path /= part;
            std::error_code error;
            status = std::filesystem::symlink_status(path, error);

            if (error || std::filesystem::is_symlink(status)) {
                return;
            }
        }

        if (!std::filesystem::is_regular_file(status) || remaining < 128) {
            return;
        }

        const auto bytes = std::filesystem::file_size(path);
        const auto limit = std::min<std::uint64_t>(4ULL << 20, remaining);
        const auto kept = std::min<std::uint64_t>(bytes, limit - 128);
        std::string marker;

        if (kept < bytes) {
            marker = "[Voyemu: retained final " + std::to_string(kept) + " of " +
                     std::to_string(bytes) + " bytes]\n";
        }

        std::ifstream input(path, std::ios::binary);
        input.seekg(static_cast<std::streamoff>(bytes - kept));
        std::ofstream output(logs / name, std::ios::binary);
        output << marker;
        char buffer[8192];
        auto left = kept;

        while (left && input && output) {
            const auto count =
                    static_cast<std::streamsize>(std::min<std::uint64_t>(left, sizeof(buffer)));
            input.read(buffer, count);
            const auto read = input.gcount();

            if (read <= 0) {
                break;
            }

            output.write(buffer, read);
            left -= static_cast<std::uint64_t>(read);
        }

        if (left || !output) {
            throw VoyemuError("QNX_HOST", "cannot retain build diagnostic");
        }

        remaining -= kept + marker.size();
    };
    retain("build/config.log", "configure.log");
    retain("build/meson-logs/meson-log.txt", "meson-log.txt");

    for (const auto &item : std::filesystem::directory_iterator(stage)) {
        if (item.path().extension() == ".log") {
            retain(item.path().filename(), item.path().filename().string());
        }
    }
}

class QnxHostBuildStage {
    std::filesystem::path path_, logs_;

public:
    QnxHostBuildStage(std::filesystem::path path, std::filesystem::path logs)
        : path_(std::move(path)), logs_(std::move(logs)) {}

    const std::filesystem::path &logs() const noexcept {
        return logs_;
    }

    ~QnxHostBuildStage() {
        try {
            retain_qnx_host_build_diagnostics(path_, logs_);
        } catch (...) { /* Ignored: destructors must not throw. */
        }

        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
};
} // namespace voyemu
