#pragma once
#include "model.hpp"
#include <set>
#include <string>

namespace voyemu {
// Guests of the running machine that receive vehicle fields: the cluster through the MCU
// projection of its QNX adapter, the multimedia through its Android vehicle adapter. A field in
// neither set stays in the model.
struct FieldDelivery {
    std::set<std::string> cluster, multimedia;
};
// cluster is the prepared cluster launch and android the prepared Android launch; each is empty
// when the machine has no such guest.
FieldDelivery field_delivery(const Json &cluster, const Json &android);
// The field_delivery of a session status, {"cluster": [names], "multimedia": [names]}, and back.
Json field_delivery_status(const FieldDelivery &delivery);
FieldDelivery field_delivery_from_status(const Json &status);
// The guests of one field as a list of "cluster" and "multimedia"; empty for a field that stays in
// the model.
Json field_delivery_json(const FieldDelivery &delivery, const std::string &name);
// The model fields that the canbus component of the multimedia reads from its full snapshot.
const std::set<std::string> &canbus_multimedia_fields();
} // namespace voyemu
