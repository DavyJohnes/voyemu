#pragma once
#include "model.hpp"
namespace voyemu {
// Import the Android originals of a profile from a pinned partition snapshot archive: the raw
// system.img and vendor.img members of one ZIP. QNX inputs are not part of the snapshot and stay
// with the profile's declared QNX originals directory.
Json import_snapshot_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                    const std::filesystem::path &snapshot,
                                    std::uint64_t reserve_bytes);
} // namespace voyemu
