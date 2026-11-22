#pragma once
#include "error.hpp"
#include "model.hpp"
#include <stdexcept>
namespace voyemu {
inline Json qnx_interguest_contract() {
    return {{"schema", 1},
            {"transport", "tap-ram-v1"},
            {"address", "172.16.100.22"},
            {"peer_address", "172.16.100.21"},
            {"prefix", 24},
            {"qconn_port", 8000}};
}

// Passion uses the addresses of its car: the cluster bridge 172.30.100.1 (emac0) and the multimedia
// eth0 172.30.100.3; the SOME/IP VLANs run on the same link.
inline Json qnx_passion_interguest_contract() {
    auto contract = qnx_interguest_contract();

    contract["address"] = "172.30.100.1";
    contract["peer_address"] = "172.30.100.3";

    return contract;
}

// Explicit geometry for the Passion diagnostic runtime; not a portable adapter acceptance.
inline Json qnx_passion_interguest_ram_contract() {
    return {{"schema", 1},
            {"backing_bytes", 11713ULL << 20},
            {"mailbox_begin", 0x40000000ULL},
            {"mailbox_end_exclusive", 0x180000000ULL},
            {"discovery", "uart"}};
}

inline Json android_network_contract() {
    return {{"schema", 1}, {"transport", "slirp-user-nat-v1"}};
}

inline void validate_android_network_contract(const Json &contract) {
    if (contract != android_network_contract() || !contract.at("schema").is_number_integer()) {
        throw VoyemuError("ANDROID_NETWORK_CONTRACT", "unsupported outbound network transport");
    }
}

inline void validate_qnx_interguest_contract(const Json &contract) {
    if ((contract != qnx_interguest_contract() && contract != qnx_passion_interguest_contract()) ||
        !contract.at("schema").is_number_integer() || !contract.at("prefix").is_number_integer() ||
        !contract.at("qconn_port").is_number_integer()) {
        throw VoyemuError("INTERGUEST_CONTRACT", "unsupported Ethernet transport");
    }
}
} // namespace voyemu
