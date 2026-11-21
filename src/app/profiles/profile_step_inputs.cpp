#include "profile_step_inputs.hpp"
#include "ordinary_path.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include <algorithm>

namespace voyemu {
void ProfileStepInputs::ordinary(const std::filesystem::path &path) const {
    ordinary_path(path, require, PathForm::Canonical, "canonical path required",
                  "linked ancestor rejected");
}

void ProfileStepInputs::file(const std::filesystem::path &path, std::uint64_t bound) const {
    ordinary(path);
    require(std::filesystem::is_regular_file(path) && std::filesystem::hard_link_count(path) == 1 &&
                    std::filesystem::file_size(path) <= bound,
            "ordinary bounded file required");
}

Json ProfileStepInputs::inventory(const std::filesystem::path &source) const {
    ordinary(source);
    Json out = Json::array();
    std::uint64_t total = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(source)) {
        require(!entry.is_symlink() && (entry.is_regular_file() || entry.is_directory()),
                "ordinary source tree required");

        if (entry.is_directory()) {
            continue;
        }

        file(entry.path(), 32ULL << 20);
        const auto size = entry.file_size();
        require(out.size() < 4096 && size <= (32ULL << 20) - total, "source inventory bounds");
        total += size;
        const auto epoch = cache_file_identity(entry.path());
        const auto hash = sha256_file(entry.path());
        require(cache_file_identity(entry.path()) == epoch, "source changed during hashing");
        out.push_back({{"path", entry.path().lexically_relative(source).generic_string()},
                       {"sha256", hash},
                       {"bytes", size}});
    }

    std::sort(out.begin(), out.end(), [](const Json &a, const Json &b) {
        return a.at("path").get<std::string>() < b.at("path").get<std::string>();
    });

    return out;
}
} // namespace voyemu
