#include "input_fence.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;
using namespace voyemu::api;

int main() try {
    const Json bounds = {{"x", 10}, {"y", 20}, {"width", 100}, {"height", 40}};
    const Json window = {{"id", 1},
                         {"type", 1},
                         {"bounds", bounds},
                         {"title", "Settings"},
                         {"package", "settings"}};
    const Json target = {{"window_id", 1},        {"resource_id", "settings:id/button"},
                         {"package", "settings"}, {"class", "Button"},
                         {"bounds", bounds},      {"clipped_bounds", bounds}};
    Json node = target;

    node["actionable_candidate"] = true;
    node["visible"] = true;
    node["enabled"] = true;
    node["clickable"] = true;
    Json request = {{"screen", "primary"},
                    {"selector", {{"resource_id", "settings:id/button"}}},
                    {"expected",
                     {{"boot_id", "boot"},
                      {"source_instance", "observer"},
                      {"revision", "old-observation"},
                      {"window", window},
                      {"target", target}}}};
    Json observation = {{"boot_id", "boot"},
                        {"source_instance", "observer"},
                        {"revision", "fresh-observation"},
                        {"screens", Json::array({{{"screen", "primary"},
                                                  {"status", "ok"},
                                                  {"complete", true},
                                                  {"width", 1920},
                                                  {"height", 720},
                                                  {"windows", Json::array({window})},
                                                  {"nodes", Json::array({node})}}})}};
    assert(fenced_point(request, observation) == Json({{"x", 60}, {"y", 40}, {"at_ms", 0}}));
    auto rejects = [&](Json r, Json o) {
        bool failed = false;
        try {
            (void)fenced_point(r, o);
        } catch (const std::exception &) {
            failed = true;
        }

        assert(failed);
    };

    for (const char *key : {"boot_id", "source_instance"}) {
        auto o = observation;
        o[key] = "changed";
        rejects(request, o);
    }

    {
        auto o = observation;
        o["screens"][0]["complete"] = false;
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"].push_back(node);
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["windows"].push_back(window);
        rejects(request, o);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"][0]["clickable"] = false;
        rejects(request, o);
    }

    for (const char *key : {"visible", "enabled", "clickable"}) {
        auto o = observation;
        o["screens"][0]["nodes"][0][key] = false;
        rejects(request, o);
    }

    {
        auto r = request;
        r["expected"]["window"]["bounds"]["x"] = 11;
        rejects(r, observation);
    }
    {
        auto r = request;
        r["expected"]["target"]["bounds"]["x"] = 11;
        rejects(r, observation);
    }
    {
        auto r = request;
        r["expected"]["ignored"] = true;
        rejects(r, observation);
    }
    {
        auto r = request;
        r["expected"]["target"]["ignored"] = true;
        rejects(r, observation);
    }
    {
        auto r = request;
        r.erase("selector");
        r["x"] = 11;
        r["y"] = 21;
        assert(fenced_point(r, observation)["x"] == 11);
        r["x"] = 110;
        rejects(r, observation);
    }
    {
        auto o = observation;
        o["screens"][0].erase("width");
        o["screens"][0].erase("height");
        bool proper = false;
        try {
            fenced_point(request, o);
        } catch (const std::exception &e) {
            proper = std::string(e.what()).starts_with("TEST_INPUT_UNAVAILABLE:");
        }

        assert(proper);
    }
    {
        auto r = request;
        r.erase("expected");
        rejects(r, observation);
    }
    {
        auto r = request;
        r["intent"] = "scroll";
        auto o = observation;
        o["screens"][0]["nodes"][0]["clickable"] = false;
        o["screens"][0]["nodes"][0]["scrollable"] = true;
        assert(fenced_point(r, o)["x"] == 60);
        o["screens"][0]["nodes"][0]["scrollable"] = false;
        rejects(r, o);
    }
    {
        auto r = request;
        r["intent"] = "drag";
        auto o = observation;
        o["screens"][0]["nodes"][0]["clickable"] = false;
        o["screens"][0]["nodes"][0]["long_clickable"] = true;
        assert(fenced_point(r, o)["x"] == 60);
    }

    {
        auto r = request;
        r.erase("selector");
        r["x"] = 11;
        r["y"] = 21;
        auto o = observation;
        o["screens"][0]["nodes"][0]["clickable"] = false;
        o["screens"][0]["nodes"][0]["actionable_candidate"] = false;
        assert(fenced_point(r, o, InputIntent::Gesture)["x"] == 11);
        rejects(r, o);
        auto drag = r;
        drag["intent"] = "drag";
        rejects(drag, o);
        auto rejects_gesture = [&](Json changed_request, Json changed_observation) {
            bool failed = false;
            try {
                (void)fenced_point(changed_request, changed_observation, InputIntent::Gesture);
            } catch (const std::exception &) {
                failed = true;
            }

            assert(failed);
        };

        for (const char *key : {"visible", "enabled"}) {
            auto changed = o;
            changed["screens"][0]["nodes"][0][key] = false;
            rejects_gesture(r, changed);
        }

        for (const char *key : {"boot_id", "source_instance"}) {
            auto changed = o;
            changed[key] = "foreign";
            rejects_gesture(r, changed);
        }

        {
            auto changed = r;
            changed["x"] = 110;
            rejects_gesture(changed, o);
        }
        {
            auto changed = r;
            changed["expected"]["window"]["bounds"]["x"] = 11;
            rejects_gesture(changed, o);
        }
        {
            auto changed = r;
            changed["expected"]["target"]["bounds"]["x"] = 11;
            rejects_gesture(changed, o);
        }
        {
            auto changed = o;
            changed["screens"][0]["nodes"].push_back(changed["screens"][0]["nodes"][0]);
            rejects_gesture(r, changed);
        }
        {
            auto changed = o;
            changed["screens"][0]["complete"] = false;
            rejects_gesture(r, changed);
        }
        {
            auto changed = r;
            changed["intent"] = "unknown";
            rejects_gesture(changed, o);
        }
        {
            auto changed = r;
            changed["intent"] = "scroll";
            rejects_gesture(changed, o);
        }
        {
            auto changed = o;
            auto extra = window;
            extra["id"] = 2;
            changed["screens"][0]["windows"].push_back(extra);
            rejects_gesture(r, changed);
        }
        {
            auto changed = o;
            changed["screens"][0]["windows"][0]["layer"] = 2;
            auto extra = window;
            extra["id"] = 2;
            extra["layer"] = 3;
            changed["screens"][0]["windows"].push_back(extra);
            rejects_gesture(r, changed);
        }
    }

    {
        auto o = observation;
        o["screens"][0]["windows"][0]["layer"] = 2;
        auto extra = window;
        extra["id"] = 2;
        extra["layer"] = 1;
        o["screens"][0]["windows"].push_back(extra);
        assert(fenced_point(request, o)["x"] == 60);
        o["screens"][0]["windows"][1]["layer"] = 3;
        rejects(request, o);
    }
    {
        auto o = observation;
        auto extra = window;
        extra["id"] = 2;
        extra["bounds"]["x"] = 200;
        o["screens"][0]["windows"].push_back(extra);
        assert(fenced_point(request, o)["x"] == 60);
    }

    auto diagnostic = [&](const Json &r, const Json &o, const char *reason,
                          const char *code = "TEST_INPUT_STALE") {
        bool caught = false;
        try {
            (void)fenced_point(r, o);
        } catch (const InputFenceError &e) {
            caught = true;
            assert(std::string(e.what()).starts_with(code));
            assert(e.details.at("reason") == reason);
            assert(e.details.dump().size() < 512);
            assert(e.details.dump().find("Settings") == std::string::npos);
            assert(e.details.dump().find("settings") == std::string::npos);

            return e.details;
        }

        assert(caught);

        return Json();
    };
    {
        auto o = observation;
        o["boot_id"] = "private-boot";
        diagnostic(request, o, "boot_changed");
        o = observation;
        o["source_instance"] = "private-source";
        diagnostic(request, o, "source_changed");
    }
    {
        auto o = observation;
        o["screens"][0]["windows"] = Json::array();
        const auto d = diagnostic(request, o, "window_missing");
        assert(d.at("expected_window").at("id") == 1 && !d.contains("observed_window"));
    }
    {
        auto r = request;
        r["expected"]["window"]["layer"] = 1;
        auto o = observation;
        o["screens"][0]["windows"][0]["layer"] = 2;
        const auto d = diagnostic(r, o, "window_changed");
        assert(d.at("expected_window").at("layer") == 1 &&
               d.at("observed_window").at("layer") == 2);
    }
    {
        auto o = observation;
        o["screens"][0]["windows"][0]["bounds"]["x"] = 11;
        const auto d = diagnostic(request, o, "window_changed");
        assert(d.at("observed_window").at("bounds").at("x") == 11);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"] = Json::array();
        assert(diagnostic(request, o, "target_missing", "TEST_INPUT_BLOCKED").at("target_count") ==
               0);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"].push_back(node);
        assert(diagnostic(request, o, "target_ambiguous", "TEST_INPUT_BLOCKED")
                       .at("target_count") == 2);
    }
    {
        auto o = observation;
        o["screens"][0]["nodes"][0]["bounds"]["x"] = 11;
        diagnostic(request, o, "target_changed");
    }
    {
        auto r = request;
        r.erase("selector");
        r["x"] = 110;
        r["y"] = 21;
        diagnostic(r, observation, "point_outside");
    }
    {
        auto o = observation;
        o["screens"][0]["complete"] = false;
        diagnostic(request, o, "coverage_unavailable", "TEST_INPUT_BLOCKED");
    }
    {
        const auto projected =
                fence_window({{"id", "credential"},
                              {"type", 4294967295ULL},
                              {"layer", nullptr},
                              {"bounds", {{"x", "secret"}, {"y", 0}, {"width", 1}, {"height", 1}}},
                              {"text", "secret"}});
        assert(projected.empty());
    }

    // API28 original Phone overlay: complete target tree, honest partial aggregate.
    {
        auto r = request, o = observation;
        auto &screen = o["screens"][0];
        screen["status"] = "partial";
        screen["complete"] = false;
        screen["display_id"] = 0;
        screen["display_epoch"] = "display";
        r["scope"] = "all_attached";
        r["selector"] = {{"resource_id", "com.qinggan.bluetoothphone:id/answer_iv"},
                         {"package", "com.qinggan.bluetoothphone"}};
        auto &w = screen["windows"][0];
        w["type"] = 2010;
        w["package"] = "com.qinggan.bluetoothphone";
        w["read_complete"] = true;
        w["root_id"] = "root";
        w["node_count"] = 2;
        Json binding = {{"schema", 1},
                        {"basis", "api28_wms_process_viewdump_v1"},
                        {"boot_id", "boot"},
                        {"observer_epoch", "observer"},
                        {"display_epoch", "display"},
                        {"display_id", 0},
                        {"window_id", 1},
                        {"session_id", "session"},
                        {"session_pid", 2449},
                        {"session_uid", 1000},
                        {"owner_uid", 1000},
                        {"package", "com.qinggan.bluetoothphone"},
                        {"process_name", "com.qinggan.bluetoothphone"},
                        {"window_token", "token"},
                        {"window_client", "client"},
                        {"root_id", "root"},
                        {"node_count", 2},
                        {"wm_order", 0},
                        {"wm_order_verified", true},
                        {"read_complete", true},
                        {"process_verified", true},
                        {"current_focus", true},
                        {"touchable", true},
                        {"focusable", true},
                        {"visible", true},
                        {"has_surface", true},
                        {"drawn", true},
                        {"view_visible", true},
                        {"obscured", false}};
        w["verified_binding"] = binding;
        auto &n = screen["nodes"][0];
        n["id"] = "answer";
        n["package"] = "com.qinggan.bluetoothphone";
        n["resource_id"] = "com.qinggan.bluetoothphone:id/answer_iv";
        n["parent"] = "root";
        n["actionable_candidate"] = false;
        auto root = n;
        root["id"] = "root";
        root["parent"] = nullptr;
        root["resource_id"] = "";
        root["clickable"] = false;
        screen["nodes"].push_back(root);
        r["expected"]["window"] = w;
        r["expected"]["target"] = target;
        r["expected"]["target"]["id"] = "answer";
        r["expected"]["target"]["package"] = "com.qinggan.bluetoothphone";
        r["expected"]["target"]["resource_id"] = "com.qinggan.bluetoothphone:id/answer_iv";
        auto wallpaper = w;
        wallpaper["id"] = 2;
        wallpaper["type"] = 2013;
        wallpaper["layer"] = nullptr;
        wallpaper.erase("verified_binding");
        wallpaper["read_complete"] = false;
        screen["windows"].push_back(wallpaper);
        assert(fenced_point(r, o) == Json({{"x", 60}, {"y", 40}, {"at_ms", 0}}));

        for (const char *key : {"session_pid", "session_uid", "owner_uid", "package",
                                "process_name", "window_token", "window_client", "observer_epoch",
                                "display_epoch", "window_id", "root_id", "node_count"}) {
            auto changed = o;
            changed["screens"][0]["windows"][0]["verified_binding"][key] = "changed";
            rejects(r, changed);
        }

        for (const char *key : {"read_complete", "process_verified", "current_focus", "touchable",
                                "focusable", "visible", "has_surface", "drawn", "view_visible"}) {
            auto changed = o;
            changed["screens"][0]["windows"][0]["verified_binding"][key] = false;
            rejects(r, changed);
        }

        {
            auto changed = o;
            changed["screens"][0]["windows"][0]["read_complete"] = false;
            rejects(r, changed);
        }
        {
            auto changed = o;
            changed["screens"][0]["nodes"].erase(1);
            rejects(r, changed);
        }
        {
            auto changed = o;
            changed["screens"][0]["nodes"][0]["id"] = "reused";
            rejects(r, changed);
        }
        {
            auto changed = o;
            auto duplicate = changed["screens"][0]["windows"][0];
            duplicate["title"] = "conflicting";
            changed["screens"][0]["windows"].push_back(duplicate);
            rejects(r, changed);
        }
        {
            auto changed = r;
            changed["x"] = 60;
            changed["y"] = 40;
            rejects(changed, o);
        }
        {
            auto changed = r;
            changed["intent"] = "drag";
            rejects(changed, o);
        }
        {
            auto changed = r;
            changed.erase("selector");
            rejects(changed, o);
        }
        {
            auto changed = r;
            changed.erase("scope");
            rejects(changed, o);
        }
        {
            auto changed = o;
            changed["screens"][0]["nodes"][0]["visible"] = false;
            rejects(r, changed);
        }
        {
            auto changed = o, expected = r;
            changed["screens"][0]["windows"][0]["bounds"]["width"] = 1;
            expected["expected"]["window"]["bounds"]["width"] = 1;
            rejects(expected, changed);
        }
        {
            auto changed = r;
            changed["expected"]["window"]["verified_binding"]["package"] = "foreign";
            rejects(changed, o);
        }
    }

    {
        std::ifstream input(std::filesystem::path(__FILE__).parent_path() /
                            "input_fence.fixture.json");
        const auto actual = Json::parse(input);
        const auto r = actual.at("request"), o = actual.at("observation");
        assert(fenced_point(r, o) == actual.at("point"));

        for (const char *key : {"session_pid", "session_uid", "owner_uid", "package",
                                "process_name", "window_token", "window_client", "observer_epoch",
                                "display_epoch", "window_id", "root_id", "node_count"}) {
            auto changed = o;
            changed["screens"][0]["windows"][0]["verified_binding"][key] = "changed";
            rejects(r, changed);
        }

        {
            auto changed = o;
            changed["screens"][0]["windows"][0]["read_complete"] = false;
            rejects(r, changed);
        }
        {
            auto changed = o;
            auto other = changed["screens"][0]["windows"][0];
            other["title"] = "conflicting";
            changed["screens"][0]["windows"].push_back(other);
            rejects(r, changed);
        }
        {
            auto changed = o;
            changed["screens"][0]["nodes"][0]["id"] = "reused";
            rejects(r, changed);
        }
    }
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
