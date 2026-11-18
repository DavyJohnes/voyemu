#include "can_observation.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace voyemu;

template <class F> void rejects(F f) {
    bool rejected = false;

    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
}

template <class F> void rejects_schema(F f) {
    bool rejected = false;

    try {
        f();
    } catch (const std::runtime_error &error) {
        rejected = std::string(error.what()).starts_with("CAN_ARGUMENT: ");
    }

    assert(rejected);
}

int main(int argc, char **argv) try {
    if (argc == 2) {
        std::ifstream input(argv[1]);
        Json produced;
        input >> produced;
        assert(parse_can_observation(produced) == produced);
        produced["schema"] = 2;
        rejects([&] {
            parse_can_observation(produced);
        });
        std::cout << "Java producer to C++ consumer passed\n";

        return 0;
    }

    validate_can_request({{"schema", 1}});
    validate_can_request({{"schema", 1},
                          {"states", {"IVI_SOC_MODESET", "HUM_VSP_FUNCTION_SW"}},
                          {"cursor", {{"source_instance", "service"}, {"after_seq", "0"}}}});

    for (const Json &bad :
         {Json{{"schema", 2}}, Json{{"schema", 0}}, Json{{"schema", 1.0}}, Json{{"schema", "1"}},
          Json{{"schema", nullptr}}, Json{{"schema", 1}, {"raw", 0}},
          Json{{"schema", 1}, {"limit", 33}},
          Json{{"schema", 1}, {"states", {"HUM_VSP_FUNCTION_SW", "HUM_VSP_FUNCTION_SW"}}},
          Json{{"schema", 1}, {"cursor", {{"source_instance", "service"}, {"after_seq", 0}}}}}) {
        rejects([&] {
            validate_can_request(bad);
        });
    }

    Json state = {
            {"schema", 1},
            {"source", "android:qg.canbus"},
            {"status", "ready"},
            {"boot_id", "boot"},
            {"source_instance", "service"},
            {"pid", 123},
            {"revision", "2"},
            {"service_started_elapsed_ns", "1"},
            {"clock", {{"domain", "android.elapsedRealtimeNanos"}, {"sampled_ns", "3"}}},
            {"values", {{"vehicle_state.IVI_SOC_MODESET", {{"status", "observed"}, {"raw", 0}}}}},
            {"acc", {{"status", "observed"}, {"raw", 0}}},
            {"history",
             {{"oldest_seq", "1"},
              {"latest_seq", "2"},
              {"dropped_count", "0"},
              {"next_cursor", {{"source_instance", "service"}, {"after_seq", "2"}}},
              {"has_more", false},
              {"gap", nullptr}}},
            {"events", Json::array()},
            {"transport",
             {{"registration", "registered"},
              {"binder_alive", true},
              {"client_manager_cache", "not_observed"}}}};
    assert(parse_can_observation(state)
                   .at("values")
                   .at("vehicle_state.IVI_SOC_MODESET")
                   .at("raw") == 0);
    const Json pending{{"schema", 1}, {"source", "android:qg.canbus"}, {"status", "not-ready"}};
    assert(parse_can_observation(pending) == pending);

    for (const Json &valid : {state, pending}) {
        for (const Json &schema :
             {Json(2), Json(0), Json(1.0), Json(1.9), Json(std::uint64_t{4294967297}),
              Json(std::uint64_t{18446744073709551615ULL}), Json("1"), Json(true), Json(false),
              Json(nullptr)}) {
            auto incompatible = valid;
            incompatible["schema"] = schema;
            rejects_schema([&] {
                parse_can_observation(incompatible);
            });
        }

        auto missing = valid;
        missing.erase("schema");
        rejects_schema([&] {
            parse_can_observation(missing);
        });
    }

    auto asleep = state;
    auto &sleepingFev = asleep["values"]["vehicle_state.IVI_SOC_MODESET"];
    sleepingFev = {
            {"status", "unavailable"}, {"raw", -1}, {"basis", "ICanBusService.getVehicleState"}};
    assert(parse_can_observation(asleep).at("values").at("vehicle_state.IVI_SOC_MODESET") ==
           sleepingFev);
    sleepingFev["raw"] = 5;
    rejects([&] {
        parse_can_observation(asleep);
    });
    sleepingFev["raw"] = -1;
    asleep["acc"]["status"] = "unavailable";
    rejects([&] {
        parse_can_observation(asleep);
    });
    auto transportDown = state;
    transportDown["transport"]["registration"] = "failed";
    rejects([&] {
        parse_can_observation(transportDown);
    });
    auto broken = state;
    broken["boot_id"] = nullptr;
    rejects([&] {
        parse_can_observation(broken);
    });
    broken = state;
    broken["history"]["next_cursor"]["source_instance"] = "other";
    rejects([&] {
        parse_can_observation(broken);
    });
    broken = state;
    broken["history"]["gap"] = "overflow";
    assert(parse_can_observation(broken)["history"]["gap"] == "overflow");
    broken = state;
    broken["revision"] = 2;
    rejects([&] {
        parse_can_observation(broken);
    });
    broken = state;
    broken["events"] = {{{"seq", "2"},
                         {"kind", "state_applied"},
                         {"source_instance", "service"},
                         {"elapsed_ns", "2"},
                         {"utc_ms", "1"},
                         {"write_id", "1"},
                         {"raw", 0}}};
    assert(parse_can_observation(broken)["events"].size() == 1);
    broken["events"].push_back(broken["events"][0]);
    rejects([&] {
        parse_can_observation(broken);
    });
    assert(parse_can_observation({{"schema", 1},
                                  {"source", "android:qg.canbus"},
                                  {"status", "not-ready"}})["status"] == "not-ready");
    validate_ipk_request({{"schema", 1}});
    validate_ipk_request({{"schema", 1},
                          {"limit", 256},
                          {"cursor", {{"source_instance", "service"}, {"after_seq", "4"}}}});

    for (const Json &bad :
         {Json{{"schema", 2}}, Json{{"schema", 1}, {"limit", 0}},
          Json{{"schema", 1}, {"limit", 257}}, Json{{"schema", 1}, {"states", Json::array()}},
          Json{{"schema", 1}, {"cursor", {{"source_instance", "service"}}}}}) {
        bool rejected = false;

        try {
            validate_ipk_request(bad);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).starts_with("IPK_ARGUMENT: ");
        }

        assert(rejected);
    }

    const Json event = {{"seq", "2"},       {"elapsed_ms", 15}, {"direction", "sent"},
                        {"cmd", "0x6e"},    {"kind", "media"},  {"origin", "canbus"},
                        {"frames", 2},      {"bytes", 154},     {"outcome", "submitted"},
                        {"reason", "none"}, {"generation", 1}};
    const Json ipk = {{"schema", 1},
                      {"source", "android:qg.canbus:ipk"},
                      {"status", "observed"},
                      {"boot_id", nullptr},
                      {"source_instance", "service"},
                      {"link",
                       {{"state", "connected"},
                        {"generation", 1},
                        {"reconnects", 0},
                        {"heartbeats_sent", 9},
                        {"heartbeat_replies", 9},
                        {"last_reply_age_ms", 120},
                        {"malformed", 0},
                        {"last_failure", "none"}}},
                      {"oldest_seq", "1"},
                      {"latest_seq", "3"},
                      {"gap", nullptr},
                      {"events", {event}},
                      {"next_cursor", {{"source_instance", "service"}, {"after_seq", "2"}}}};
    assert(parse_ipk_history(ipk) == ipk);
    assert(parse_ipk_history({{"schema", 1},
                              {"source", "android:qg.canbus:ipk"},
                              {"status", "not-ready"}})["status"] == "not-ready");
    auto received = ipk;
    received["events"][0].update({{"direction", "received"},
                                  {"cmd", "0x81"},
                                  {"kind", "cluster"},
                                  {"origin", "ivcd"},
                                  {"outcome", "received"}});
    assert(parse_ipk_history(received) == received);

    for (const auto &[pointer, value] : std::initializer_list<std::pair<const char *, Json>>{
                 {"/events/0/outcome", "received"},
                 {"/events/0/cmd", "0x6E"},
                 {"/events/0/seq", "4"},
                 {"/events/0/bytes", 3},
                 {"/events/0/text", "Песня"},
                 {"/events/0/kind", "Media title"},
                 {"/link/generation", -1},
                 {"/gap", "lost"},
                 {"/next_cursor/after_seq", "1"},
                 {"/next_cursor/source_instance", "other"},
                 {"/schema", 2},
                 {"/status", "ready"},
                 {"/extra", 1}}) {
        auto broken = ipk;
        broken[Json::json_pointer(pointer)] = value;
        bool rejected = false;

        try {
            parse_ipk_history(broken);
        } catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).starts_with("IPK_PROTOCOL: ");
        }

        assert(rejected);
    }

    std::cout << "CAN observation contract passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
