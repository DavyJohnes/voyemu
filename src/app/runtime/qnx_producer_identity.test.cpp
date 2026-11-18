#include "qnx_producer_identity.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;

int main() try {
    std::map<std::uint64_t, Json> origins;

    qnx_producer_origin(origins, 0x40003000, "checkpoint");
    qnx_producer_origin(origins, 0x40003000, "retained_collector");
    qnx_producer_origin(origins, 0x40004000, "current_ring");
    qnx_producer_origin(origins, 0x40003000, "current_ring");
    qnx_producer_origin(origins, 0x40003000, "retained_collector");
    assert(origins.at(0x40003000) == Json({"checkpoint", "retained_collector", "current_ring"}));
    assert(origins.at(0x40004000) == Json({"current_ring"}));
    const Json context = {{"origins", origins.at(0x40003000)},
                          {"generation", 7},
                          {"session_id", "test-sid"},
                          {"session_directory", "/private/session"},
                          {"qnx_pid", 123}};
    unsigned reports = 0;
    auto report = [&](const Json &) {
        ++reports;
    };
    qnx_producer_identity(0x40003000, {0x58464751, 3}, 8, context, report);
    assert(reports == 0);

    for (const auto &sample :
         std::vector<std::pair<std::array<std::uint32_t, 2>, std::size_t>>{{{0xdeadbeef, 3}, 8},
                                                                           {{0x58464751, 2}, 8},
                                                                           {{0x58464751, 0}, 4},
                                                                           {{0, 0}, 0},
                                                                           {{0x00514751, 0}, 3}}) {
        Json witness;
        bool failed = false;
        try {
            qnx_producer_identity(0x40003000, sample.first, sample.second, context,
                                  [&](const Json &value) {
                                      witness = value;
                                  });
        } catch (const std::runtime_error &error) {
            failed = true;
            const std::string message = error.what();
            assert(message.starts_with("QNX_PRODUCER: page identity "));
            assert(Json::parse(message.substr(
                           std::string("QNX_PRODUCER: page identity ").size())) == witness);
        }

        assert(failed && witness.at("producer_address") == 0x40003000 &&
               witness.at("read_count") == sample.second);

        for (const auto &item : context.items()) {
            assert(witness.at(item.key()) == item.value());
        }

        assert(witness.at("raw_hex").get<std::string>().size() == sample.second * 2);
        assert(witness.at("magic").is_null() == (sample.second < 4));
        assert(witness.at("version").is_null() == (sample.second < 8));

        if (sample.second == 8 && sample.first[0] == 0xdeadbeef) {
            assert(witness.at("raw_hex") == "efbeadde03000000");
        }
    }

    bool preserved = false;
    try {
        qnx_producer_identity(0x40003000, {0, 0}, 8, context, [](const Json &) {
            throw std::runtime_error("write failed");
        });
    } catch (const std::runtime_error &error) {
        preserved = std::string_view(error.what()).starts_with("QNX_PRODUCER: page identity ");
    }

    assert(preserved);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
