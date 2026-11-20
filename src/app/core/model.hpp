#pragma once
#include "error.hpp"
#include <cstdint>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>

namespace voyemu {
using Json = nlohmann::json;
inline constexpr const char *default_profile_name = "free-09-chinese-20260106-6.0.3";
bool valid_profile_name(std::string_view name);
struct Field {
    std::uint32_t index;
    std::string name;
    unsigned width;
    std::uint32_t initial;
};
const std::vector<Field> &catalog();
Json catalog_json();
inline constexpr const char *map_sha256 =
        "255335b86bb5fd782d23086018dcfc3874d088364ad5f15b264cfb12020d800e";
std::string utc_now();

constexpr bool qnx_theme_display_matches(unsigned requested, std::uint64_t displayed) {
    // Pinned CSettingInfo::setdisplaytheme_lcdup/lcddown; shared display4 cannot identify
    // requests1/3/4.
    switch (requested) {
    case 1:
        return displayed == 1;
    case 2:
        return displayed == 2 || displayed == 5;
    case 3:
        return displayed == 3;
    case 4:
        return displayed == 6;
    default:
        return false;
    }
}

Json normal_initial_values();
Json initial_state();
// Guest quickboot is independent of the host scenario startup policy.
// Pure selection only: never overwrites the caller's saved scenario.
enum class VehicleStartup {
    fresh,
    resume
};
Json select_vehicle_startup(VehicleStartup mode, const Json &profile_values, const Json *saved,
                            std::uint32_t guest_sequence = 0);
void validate_state(const Json &state);
Json apply_patch(const Json &state, const Json &values);
// Bounded validated JSON bytes retain formatting for exact transaction rollback.
std::string read_private_json_bytes(const std::filesystem::path &path,
                                    std::size_t maximum = 128 * 1024);
void write_private_json_bytes(const std::filesystem::path &path, const std::string &text,
                              std::size_t maximum = 128 * 1024);
Json read_private_json(const std::filesystem::path &path, std::size_t maximum = 128 * 1024);
void write_private_json(const std::filesystem::path &path, const Json &value,
                        std::size_t maximum = 128 * 1024);
void ensure_private_directory(const std::filesystem::path &path);
// Recreates the missing ones of the four .local roots of an installation as private directories.
void ensure_local_roots(const std::filesystem::path &root);
class VehicleModel {
public:
    explicit VehicleModel(std::filesystem::path path);

    const Json &state() const noexcept {
        return state_;
    }

    const Json &patch(const Json &values);
    const Json &advance_revision(std::uint32_t observed);

private:
    std::filesystem::path path_;
    Json state_;
};
} // namespace voyemu
