#include "qnx_mcu_projection.hpp"
#include <array>
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;

int main() try {
    assert(qnx_mcu_request_projection(0, 0, 0).is_null());
    const auto expected = qnx_mcu_projection(QnxGuestAdapter::sa8155_qinggan11_731);
    assert(qnx_mcu_request_projection(650, 736, 462) == expected);

    for (const auto &args : {std::array<std::uint64_t, 3>{280, 579, 579},
                             {650, 736, 579},
                             {650, 736, 461},
                             {650, 0, 462},
                             {0, 736, 462},
                             {650, 736, 0}}) {
        bool rejected = false;
        try {
            qnx_mcu_request_projection(args[0], args[1], args[2]);
        } catch (...) {
            rejected = true;
        }

        assert(rejected);
    }

    const auto legacy = qnx_mcu_delivery_scope(Json::object());
    assert(legacy && legacy->at("fields") == 579 && !legacy->contains("mcu_projection"));
    assert(!qnx_mcu_delivery_scope(Json::object(), expected));
    assert(qnx_mcu_delivery_scope(Json{{"mcu_projection", expected}}, expected));
    auto float_expected = expected;
    float_expected["projected_fields"] = 462.0;
    assert(!qnx_mcu_delivery_scope(Json{{"mcu_projection", expected}}, float_expected));
    const auto free = qnx_mcu_delivery_scope(Json{{"mcu_projection", expected}});
    assert(free && free->at("fields") == 462 && free->at("full_wire_verified") == false);

    for (const auto &patch :
         {Json{{"projected_fields", 462.0}}, Json{{"projected_fields", 579}},
          Json{{"wire_fields", 579}}, Json{{"packet_bytes", 280}}, Json{{"unsupported_fields", 0}},
          Json{{"unmapped_wire_bits", "overwritten"}}}) {
        auto wrong = expected;
        wrong.update(patch);
        assert(!qnx_mcu_delivery_scope(Json{{"mcu_projection", wrong}}));
    }

    assert(!qnx_mcu_delivery_scope(Json::array()));
    assert(!qnx_mcu_delivery_scope(Json{{"mcu_projection", nullptr}}));
    auto absent = expected;
    absent.erase("wire_fields");
    assert(!qnx_mcu_delivery_scope(Json{{"mcu_projection", absent}}));

    const auto passion = qnx_passion_mcu_projection();
    assert(qnx_mcu_request_projection(0, 0, 3, true) == passion);
    for (const auto &args : {std::array<std::uint64_t, 4>{0, 0, 3, 0},
                             {0, 0, 0, 1},
                             {650, 736, 462, 1},
                             {0, 0, 4, 1}}) {
        bool rejected = false;
        try {
            qnx_mcu_request_projection(args[0], args[1], args[2], args[3] != 0);
        } catch (const VoyemuError &) {
            rejected = true;
        }
        assert(rejected);
    }
    const Json passion_ack{{"mcu_projection", passion}};
    const auto passion_scope = qnx_mcu_delivery_scope(passion_ack, passion);
    assert(passion_scope && passion_scope->at("fields") == 3 &&
           passion_scope->at("full_wire_verified") == false);
    assert(!qnx_mcu_delivery_scope(passion_ack));
    assert(!qnx_mcu_delivery_scope(passion_ack, expected));
    assert(!qnx_mcu_delivery_scope(Json::object(), passion));
    assert(!qnx_mcu_delivery_scope(Json{{"mcu_projection", expected}}, passion));
    for (const auto &patch :
         {Json{{"projected_fields", 3.0}}, Json{{"unsupported_fields", 0}},
          Json{{"hardware_adapter", "sa8155-qinggan11-731"}}, Json{{"transport", "mcu-wire"}},
          Json{{"published_fields", nullptr}}}) {
        auto wrong = passion;
        wrong.update(patch);
        assert(!qnx_mcu_delivery_scope(Json{{"mcu_projection", wrong}}, passion));
        assert(!qnx_mcu_delivery_scope(passion_ack, wrong));
    }
    std::cout << "A9 full, Free and profile-bound Passion ACK scope guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
