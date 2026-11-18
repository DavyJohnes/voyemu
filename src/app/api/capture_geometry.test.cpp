#include "capture_geometry.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;
using namespace voyemu::api;

int main() try {
    const Json b = {{"x", 10}, {"y", 20}, {"width", 100}, {"height", 40}};
    Json window = {
            {"id", 1},   {"type", 1}, {"bounds", b}, {"title", "Settings"}, {"package", "settings"},
            {"layer", 2}};
    Json node = {{"window_id", 1},        {"resource_id", "settings:id/label"},
                 {"package", "settings"}, {"class", "TextView"},
                 {"bounds", b},           {"clipped_bounds", b},
                 {"visible", true},       {"enabled", false},
                 {"clickable", false}};
    Json request = {{"screens", Json::array({"primary"})},
                    {"selector", {{"resource_id", "settings:id/label"}}}};
    Json observation = {{"boot_id", "boot"},
                        {"source_instance", "observer"},
                        {"revision", "1"},
                        {"screens", Json::array({{{"screen", "primary"},
                                                  {"status", "ok"},
                                                  {"complete", true},
                                                  {"width", 1920},
                                                  {"height", 720},
                                                  {"sdk_display_id", 0},
                                                  {"display_id", 0},
                                                  {"windows", Json::array({window})},
                                                  {"nodes", Json::array({node})}}})}};
    {
        auto o = observation;
        o["screens"][0]["scope"] = "api30_interactive";
        o["screens"][0]["selection_coverage"] = "interactive_windows_only";
        o["screens"][0]["windows"][0]["display_id"] = 0;
        const auto result = capture_geometry(request, o);
        assert(result.at("selection_coverage") == "interactive_windows_only");
    }
    const auto geometry = capture_geometry(request, observation);

    assert(geometry.at("rect") == b);
    {
        auto r = request;
        auto o = observation;
        r["screens"][0] = "secondary";
        auto &screen = o["screens"][0];
        screen["screen"] = "secondary";
        screen["sdk_display_id"] = 1;
        screen["display_id"] = 7;
        screen["scope"] = "api30_interactive";
        screen["selection_coverage"] = "interactive_windows_only";
        screen["windows"][0]["display_id"] = 7;
        const auto dynamic = capture_geometry(r, o);
        assert(dynamic.at("rect") == b &&
               dynamic.at("display_mapping").at("guest_display_id") == 7 &&
               dynamic.at("display_mapping").at("sdk_display_id") == 1);
        auto rejects_dynamic = [&](const Json &value) {
            bool rejected = false;
            try {
                capture_geometry(r, value);
            } catch (const std::exception &) {
                rejected = true;
            }

            assert(rejected);
        };

        for (const char *key : {"scope", "selection_coverage"}) {
            auto bad = o;
            bad["screens"][0].erase(key);
            rejects_dynamic(bad);
        }

        {
            auto bad = o;
            bad["screens"][0]["sdk_display_id"] = 0;
            rejects_dynamic(bad);
        }
        {
            auto bad = o;
            bad["screens"][0]["windows"][0]["display_id"] = 8;
            rejects_dynamic(bad);
        }
        {
            auto bad = o;
            bad["screens"][0]["windows"][0].erase("display_id");
            rejects_dynamic(bad);
        }
        {
            auto bad = o;
            bad["screens"].push_back(screen);
            rejects_dynamic(bad);
        }
        auto moved = o;
        moved["screens"][0]["display_id"] = 8;
        moved["screens"][0]["windows"][0]["display_id"] = 8;
        const auto changed = capture_geometry(r, moved);
        bool stale = false;
        try {
            verify_capture_geometry(dynamic, changed);
        } catch (const std::exception &) {
            stale = true;
        }

        assert(stale);
        auto coverage = dynamic;
        coverage.erase("selection_coverage");
        stale = false;
        try {
            verify_capture_geometry(dynamic, coverage);
        } catch (const std::exception &) {
            stale = true;
        }

        assert(stale);
    }

    auto rejects = [&](Json r, Json o) {
        bool rejected = false;
        try {
            capture_geometry(r, o);
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    };
    {
        auto o = observation;
        o["screens"][0]["nodes"].push_back(node);
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"][0]["visible"] = false;
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"][0]["clipped_bounds"]["width"] = 90;
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"][0]["bounds"]["width"] = 0;
        o["screens"][0]["nodes"][0]["clipped_bounds"]["width"] = 0;
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["complete"] = false;
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["display_id"] = 3;
        rejects(request, o);
    }
    {
        auto o = observation;
        auto other = window;
        other["id"] = 2;
        other["layer"] = 3;
        other["bounds"] = {{"x", 109}, {"y", 20}, {"width", 10}, {"height", 40}};
        o["screens"][0]["windows"].push_back(other);
        rejects(request, o);
        o["screens"][0]["windows"][1]["layer"] = 1;
        assert(capture_geometry(request, o).at("rect") == b);
        o["screens"][0]["windows"][1].erase("layer");
        rejects(request, o);
    }
    {
        auto r = request;
        r["selector"]["text"] = "label";
        rejects(r, observation);
    }
    {
        auto o = observation;
        o["revision"] = "2";
        verify_capture_geometry(geometry, capture_geometry(request, o));
    }

    for (const char *key : {"boot_id", "source_instance", "window", "target", "rect"}) {
        auto after = geometry;
        after[key] = nullptr;
        bool rejected = false;
        try {
            verify_capture_geometry(geometry, after);
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected);
    }

    {
        auto r = request;
        r["expected"] = geometry;
        r["expected"].erase("rect");
        assert(capture_geometry(r, observation).at("rect") == b);
        r["expected"]["boot_id"] = "other";
        rejects(r, observation);
    }
    {
        auto r = request;
        auto o = observation;
        r["screens"][0] = "secondary";
        o["screens"][0]["screen"] = "secondary";
        o["screens"][0]["sdk_display_id"] = 1;
        o["screens"][0]["display_id"] = 1;
        assert(capture_geometry(r, o).at("rect") == b);
        o["screens"][0]["status"] = "unsupported";
        rejects(r, o);
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
