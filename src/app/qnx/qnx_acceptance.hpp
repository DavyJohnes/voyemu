#pragma once
#include "error.hpp"
#include "model.hpp"
#include "qnx_mcu_projection.hpp"
#include <cstddef>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>
namespace voyemu {
inline constexpr std::uint64_t qnx_acceptance_disk_reserve = 8ULL << 30;

inline bool qnx_acceptance_disk_reserve_available(std::uint64_t available) {
    return available >= qnx_acceptance_disk_reserve;
}

inline std::uint64_t qnx_acceptance_disk_required(const std::vector<std::uint64_t> &copies,
                                                  unsigned memory_mib = 8192) {
    // Full ephemeral RAM, planned ancillary allowance, and remaining operational reserve.
    if (memory_mib != 8192 && memory_mib != 11713) {
        throw VoyemuError("QNX_PACKAGE_ACCEPTANCE", "unsupported guest RAM extent");
    }

    std::uint64_t required = (std::uint64_t(memory_mib) << 20) + (1ULL << 30) +
                             qnx_acceptance_disk_reserve;

    for (const auto bytes : copies) {
        if (bytes > std::numeric_limits<std::uint64_t>::max() - required) {
            throw VoyemuError("QNX_PACKAGE_ACCEPTANCE", "disk budget overflow");
        }

        required += bytes;
    }

    return required;
}

inline void qnx_acceptance_require_disk(std::uint64_t available,
                                        const std::vector<std::uint64_t> &copies,
                                        unsigned memory_mib = 8192) {
    if (available < qnx_acceptance_disk_required(copies, memory_mib)) {
        throw VoyemuError(
                "QNX_PACKAGE_ACCEPTANCE",
                "image copies, full guest RAM, ancillary allowance and operational reserve required");
    }
}

inline bool qnx_native_frame_nonblank(std::span<const std::byte> data, std::size_t width,
                                      std::size_t height) {
    if (!width || !height || width > 1920 || height > 720 || data.size() != width * height * 4) {
        return false;
    }

    // Sparse OEM dashboards are legitimate. Reject blank/flat/logo frames, not dark backgrounds.
    std::size_t bright = 0, minx = width, miny = height, maxx = 0, maxy = 0, colors = 0;
    unsigned palette[8]{};

    for (std::size_t p = 0; p < width * height; ++p) {
        const auto i = p * 4;
        const unsigned b = std::to_integer<unsigned char>(data[i]),
                       g = std::to_integer<unsigned char>(data[i + 1]),
                       r = std::to_integer<unsigned char>(data[i + 2]);

        if (b <= 64 && g <= 64 && r <= 64) {
            continue;
        }

        ++bright;
        const auto x = p % width, y = p / width;

        if (x < minx) {
            minx = x;
        }

        if (x > maxx) {
            maxx = x;
        }

        if (y < miny) {
            miny = y;
        }

        if (y > maxy) {
            maxy = y;
        }

        if (colors < 8) {
            const unsigned c = b | (g << 8) | (r << 16);
            bool have = false;

            for (std::size_t n = 0; n < colors; ++n) {
                if (palette[n] == c) {
                    have = true;
                }
            }

            if (!have) {
                palette[colors++] = c;
            }
        }
    }

    return bright >= 500 && colors >= 8 && maxx >= minx && maxy >= miny &&
           maxx - minx >= width / 2 && maxy - miny >= height / 3;
}

// Free acceptance is typed ACK plus an owned native frame after that revision.
inline bool
qnx_qinggan11_post_ack_frame(const Json &qnx, const Json &owner, const Json &stable_owner,
                             std::uint64_t timestamp_ns, std::uint64_t minimum_source_revision = 0,
                             const Json &expected_projection = qnx_qinggan11_mcu_projection()) {
    if (!owner.is_object() || owner != stable_owner ||
        owner.value("source", std::string{}) != "original-OEM-Screen-BMP" ||
        !qnx.contains("vehicle") || !qnx.at("vehicle").is_object() ||
        !qnx.contains("presentation") || !qnx.at("presentation").is_object()) {
        return false;
    }

    const auto &vehicle = qnx.at("vehicle");
    const auto &presentation = qnx.at("presentation");

    if (vehicle.value("status", std::string{}) != "guest_acknowledged" ||
        !vehicle.contains("fields") || !vehicle.at("fields").is_number_integer() ||
        (!expected_projection.is_object() || !expected_projection.contains("projected_fields") ||
         vehicle.at("fields") != expected_projection.at("projected_fields")) ||
        !vehicle.contains("full_wire_verified") || vehicle.at("full_wire_verified") != false ||
        !qnx_mcu_delivery_scope(vehicle, expected_projection)) {
        return false;
    }

    if (!vehicle.contains("source_sequence") ||
        !vehicle.at("source_sequence").is_number_integer()) {
        return false;
    }

    const auto revision = vehicle.at("source_sequence").get<std::uint64_t>();

    if (!revision || revision > UINT32_MAX || revision < minimum_source_revision ||
        !presentation.contains("after_timestamp_ns") ||
        !presentation.at("after_timestamp_ns").is_number_integer()) {
        return false;
    }

    const auto after = presentation.at("after_timestamp_ns").get<std::uint64_t>();

    return after > 0 && timestamp_ns > after;
}

// Bounded cold acceptance: original OEM Screen frame, guest input ACK, shutdown.
// The request is generated from the current source build and local host bindings.
Json accept_qnx_images(const std::filesystem::path &root, const std::string &profile,
                       const std::filesystem::path &request,
                       const std::filesystem::path &fresh_output,
                       std::function<bool()> cancelled = {});
} // namespace voyemu
