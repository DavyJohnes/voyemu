#pragma once
#include "error.hpp"
#include "qnx_interguest_binding.hpp"
namespace voyemu {
inline std::string qnx_owned_ram_geometry(const QnxInterguestBinding &binding) {
    return binding.passion ? "Passion11713MiB" : "legacy8GiB";
}

inline std::uint64_t qnx_owned_ram_bytes(const Json &owner) {
    const auto geometry = owner.value("ram_geometry", std::string("legacy8GiB"));

    if (geometry == "legacy8GiB") {
        return QnxInterguestBinding{}.ram_bytes();
    }

    if (geometry == "Passion11713MiB") {
        return QnxInterguestBinding{InterguestLink::RamGeometry::passion21480, true}.ram_bytes();
    }

    throw VoyemuError("QNX_CLEANUP_OWNER", "unsupported RAM geometry");
}
} // namespace voyemu
