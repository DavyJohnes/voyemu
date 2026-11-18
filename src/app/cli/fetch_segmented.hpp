#pragma once
#include "model.hpp"
namespace voyemu {
// The A9 6.0.3, 6.11.1 and 7.3.1 owner updates share one segmented layout; the profile defaults to
// the definition id.
Json segmented_acquisition_plan(const Json &definition, const Json &qnx,
                                std::string_view profile = {});
Json import_segmented_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                     const std::filesystem::path &update,
                                     std::uint64_t reserve_bytes);
} // namespace voyemu
