#include "embedded_data.hpp"
#include "qnx_host.hpp"

namespace voyemu {
namespace {
#include "qnx_host_catalog.inc"
} // namespace

const Json &qnx_host_catalog() {
    static const Json value = Json::parse(inflate_embedded(qnx_host_json, qnx_host_json_size));

    return value;
}
} // namespace voyemu
