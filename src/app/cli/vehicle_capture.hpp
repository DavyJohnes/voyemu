#pragma once
#include "model.hpp"
namespace voyemu {
// Materializes only missing profile-pinned IMG files from a local owner update.zip.
Json materialize_qnx_originals(const std::filesystem::path &root, std::string_view profile,
                               const std::filesystem::path &jdk = {}, bool offline = false,
                               std::uint64_t reserve_bytes = 20ULL << 30);
// Explicit physical serial; captures draft owner inputs and SHA without changing the vehicle.
Json capture_vehicle_inputs(const std::filesystem::path &root, std::string_view profile,
                            const std::string &serial, const std::filesystem::path &jdk = {},
                            bool offline = false);
} // namespace voyemu
