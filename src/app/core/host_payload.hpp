#pragma once
#include "model.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include <stdexcept>
namespace voyemu {
inline std::filesystem::path host_payload_directory(const std::filesystem::path &root) {
    return root;
}

inline std::filesystem::path host_program(const std::filesystem::path &root) {
    return root / ("voyemu" + std::string(os::executable_suffix));
}

// The program contains its own UI resources. Only the root executable is pinned.
inline Json bundled_frontend_record(const std::filesystem::path &root) {
    if (!root.is_absolute() || std::filesystem::canonical(root) != root) {
        throw VoyemuError("HOST_PAYLOAD", "canonical project root required");
    }

    const auto program = host_program(root);

    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(program)) ||
        std::filesystem::hard_link_count(program) != 1 ||
        std::filesystem::file_size(program) > 128ULL << 20) {
        throw VoyemuError("HOST_PAYLOAD", "bounded ordinary root executable required");
    }

    return {{"schema", 1},
            {"source_root", root.string()},
            {"executable", program.string()},
            {"pins",
             Json::array({{{"path", program.string()}, {"sha256", sha256_file(program)}}})}};
}
} // namespace voyemu
