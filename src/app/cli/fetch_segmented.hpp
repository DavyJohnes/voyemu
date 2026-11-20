#pragma once
#include "model.hpp"
namespace voyemu {
// The owner update of acquisition.format pinned-zip-zero-base-zstd-segments: the pins of its
// images and originals come from profile.json and profile-qnx.json.
Json segmented_acquisition_plan(const Json &definition, const Json &qnx);
Json import_segmented_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                     const std::filesystem::path &update,
                                     std::uint64_t reserve_bytes);
} // namespace voyemu
