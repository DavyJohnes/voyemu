#pragma once
#include "model.hpp"
#include <span>
#include <string_view>
namespace voyemu {
// A generated SOME/IP client library of the multimedia and the VLAN its VHAL starts it on.
struct SomeipClientLibrary {
    std::string_view elf;
    unsigned vlan;
};

// The vehicle gateway contract of the Passion cluster (see SomeipGateway), read from its pinned
// binaries at preparation: services and VLANs from the generated SOME/IP clients of
// /usr/bin/ic_service (each <Service>_client_data and the VLAN name its start_<Service>_client call
// passes), instances, versions, eventgroups and event ids from its event_field_notifier_* records,
// and the payload of each event from the SOME/IP service table: its signals in order, packed
// big-endian, named as in the Passion vehicle catalogue (by position where the table disagrees).
// Field getters (field_getter_*_rpc_call) give each service its method ids and response layouts.
// The multimedia's client libraries add the services ic_service does not use, on their VLANs; their
// descriptors are reached through R_AARCH64_ABS64 and their getters call through the PLT.
Json qnx_passion_someip_contract(std::string_view ic_service, const Json &service_table,
                                 std::span<const SomeipClientLibrary> android_clients = {});
} // namespace voyemu
