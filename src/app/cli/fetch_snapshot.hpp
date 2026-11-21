#pragma once
#include "model.hpp"
namespace voyemu {
// Import the Android originals of a profile from a pinned partition snapshot archive: the raw
// system.img and vendor.img members of one ZIP. A snapshot holds no QNX images; a profile without
// profile-qnx.json builds without the cluster.
Json import_snapshot_profile_inputs(const std::filesystem::path &root, std::string_view profile,
                                    const std::filesystem::path &snapshot,
                                    std::uint64_t reserve_bytes);
} // namespace voyemu
