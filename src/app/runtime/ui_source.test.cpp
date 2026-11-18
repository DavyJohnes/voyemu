#include "ui_source.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) try {
    if (argc == 2) {
        std::ifstream input(argv[1]);
        Json capture;
        input >> capture;
        auto actual = capture.at("result");
        actual["schema"] = 1;
        actual["ok"] = true;
        actual["surfaces"] = actual.at("screens");
        actual.erase("screens");

        for (auto &surface : actual.at("surfaces")) {
            surface["surface"] = surface.at("screen") == "primary" ? "media_primary"
                                                                   : "media_secondary";
            surface.erase("screen");
        }

        bool missing_dimensions = false;
        try {
            runtime::normalize_ui_observation(actual);
        } catch (const std::exception &e) {
            missing_dimensions = std::string(e.what()).starts_with("TREE_PROTOCOL:");
        }

        assert(missing_dimensions);
        // A repaired result must supply independently observed dimensions, never infer them here.
    }

    Json tree = {{"schema", 1},
                 {"ok", true},
                 {"boot_id", "boot"},
                 {"observer_epoch", "epoch"},
                 {"observation_id", "revision"},
                 {"surfaces", Json::array()}};

    for (const char *name : {"media_primary", "media_secondary"}) {
        tree["surfaces"].push_back(
                {{"surface", name},
                 {"display_id", std::string(name) == "media_primary" ? 0 : 7},
                 {"status", "partial"},
                 {"complete", false},
                 {"width", 1920},
                 {"height", 720},
                 {"windows", Json::array({{{"frame", Json::array({0, 0, 1920, 720})}}})},
                 {"nodes", Json::array({{{"text_redacted", true},
                                         {"text", nullptr},
                                         {"bounds", Json::array({1, 2, 11, 22})}}})}});
    }

    auto result = runtime::normalize_ui_observation(tree);
    assert(result.at("screens")[1].at("screen") == "secondary");
    assert(result.at("screens")[1].at("display_id") == 7);
    assert(result.at("screens")[0].at("nodes")[0].at("bounds").at("width") == 10);
    assert(result.at("status") == "partial" && !result.at("atomic_ui_state").get<bool>());
    auto fails = [&](Json bad) {
        bool rejected = false;
        try {
            runtime::normalize_ui_observation(bad);
        } catch (...) {
            rejected = true;
        }

        assert(rejected);
    };
    // Real primary AX capture omitted dimensions; reject rather than infer from windows.
    auto missing = tree;
    missing["surfaces"][0].erase("width");
    missing["surfaces"][0].erase("height");
    bool proper = false;
    try {
        runtime::normalize_ui_observation(missing);
    } catch (const std::exception &e) {
        proper = std::string(e.what()).starts_with("TREE_PROTOCOL:");
    }

    assert(proper);
    assert(result["screens"][0]["width"] == 1920 && result["screens"][0]["height"] == 720);

    for (const Json &size :
         {Json(0), Json(-1), Json(3841), Json("1920"), Json(nullptr), Json(true)}) {
        auto invalid = tree;
        invalid["surfaces"][0]["width"] = size;
        fails(invalid);
    }

    auto bad = tree;
    bad["surfaces"][1]["surface"] = "media_primary";
    fails(bad);
    bad = tree;
    bad["surfaces"][0]["nodes"][0]["text"] = "secret";
    fails(bad);
    bad = tree;
    bad["surfaces"][0]["nodes"][0]["bounds"] = Json::array({5, 1, 3, 2});
    fails(bad);
    bad = tree;
    bad["surfaces"][1]["sdk_display_id"] = 7;
    fails(bad);
    bad = tree;
    bad["surfaces"][0]["display_id"] = 2;
    fails(bad);
    bad = tree;
    bad["surfaces"][0]["nodes"][0]["bounds"] =
            Json::array({-9223372036854775807LL, 0, 9223372036854775807LL, 1});
    fails(bad);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
