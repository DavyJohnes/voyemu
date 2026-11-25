#pragma once
#include "model.hpp"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace voyemu::session_detail {
struct ProcessIdentityReader {
    std::function<std::optional<std::string>(std::int64_t)> start;
    std::function<std::filesystem::path(std::int64_t)> executable;
    std::function<std::vector<std::string>(std::int64_t)> arguments;
};

Json read_launch_process_identity(std::int64_t pid, const ProcessIdentityReader &reader,
                                  const std::optional<std::string> &expected_start = std::nullopt);
} // namespace voyemu::session_detail
