#include "pointer_drag.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace voyemu;
using namespace voyemu::api;

int main() try {
    const Json base{{"op", "input.drag"},
                    {"screen", "primary"},
                    {"from", {{"x", 0}, {"y", 719}}},
                    {"to", {{"x", 1919}, {"y", 0}}},
                    {"session", {{"ownership_id", "owner"}}},
                    {"request_id", "drag-one"}};
    auto result = drag_gesture(base, 1920, 720);

    assert(result["op"] == "input.gesture" && result["session"] == base["session"] &&
           result["request_id"] == "drag-one");
    assert(result["screen"] == "primary" && !result.contains("from") && !result.contains("to") &&
           !result.contains("duration_ms"));
    assert(result["points"].front() == Json({{"x", 0}, {"y", 719}, {"at_ms", 0}}));
    assert(result["points"].back() == Json({{"x", 1919}, {"y", 0}, {"at_ms", 300}}));

    for (int duration : {1, 31, 32, 33, 1999, 2000}) {
        auto request = base;
        request["duration_ms"] = duration;
        const auto points = drag_gesture(request, 1920, 720).at("points");
        assert(points.size() >= 2 && points.size() <= 64 && points.back()["at_ms"] == duration);

        for (std::size_t i = 1; i < points.size(); ++i) {
            const int delta = points[i]["at_ms"].get<int>() - points[i - 1]["at_ms"].get<int>();
            assert(delta > 0 && delta <= 32);
            assert(points[i]["x"].get<int>() >= points[i - 1]["x"].get<int>());
            assert(points[i]["y"].get<int>() <= points[i - 1]["y"].get<int>());
        }
    }

    auto stationary = base;
    stationary["to"] = stationary["from"];
    stationary["screen"] = "secondary";
    assert(drag_gesture(stationary, 1920, 720)["points"].back()["y"] == 719);
    auto custom = base;
    custom["from"] = {{"x", 0}, {"y", 0}};
    custom["to"] = {{"x", 99}, {"y", 49}};
    assert(drag_gesture(custom, 100, 50)["points"].back()["x"] == 99);
    const auto rejects = [](const Json &request, int width = 1920, int height = 720) {
        bool failed = false;
        try {
            (void)drag_gesture(request, width, height);
        } catch (const std::runtime_error &) {
            failed = true;
        }

        assert(failed);
    };

    for (const Json &bad : {Json(-1), Json(1920), Json(1.5), Json("1"), Json(true), Json(nullptr),
                            Json(std::numeric_limits<std::uint64_t>::max())}) {
        auto request = base;
        request["from"]["x"] = bad;
        rejects(request);
    }

    for (const Json &bad : {Json(0), Json(std::uint64_t{0}), Json(-1), Json(2001), Json(1.0),
                            Json(false), Json("300"), Json(nullptr)}) {
        auto request = base;
        request["duration_ms"] = bad;
        rejects(request);
    }

    for (const char *key : {"points", "display_id", "screens"}) {
        auto request = base;
        request[key] = Json::array();
        rejects(request);
    }

    for (const char *screen : {"cluster", "", "left"}) {
        auto request = base;
        request["screen"] = screen;
        rejects(request);
    }

    auto request = base;
    request["to"]["y"] = 720;
    rejects(request);
    request = base;
    request["from"].erase("x");
    rejects(request);
    request = base;
    request["to"]["extra"] = 1;
    rejects(request);
    request = base;
    request["from"] = Json::array({0, 0});
    rejects(request);
    request = base;
    request.erase("screen");
    rejects(request);
    request = base;
    request["op"] = "input.gesture";
    rejects(request);
    rejects(base, 0, 720);
    rejects(base, 1920, -1);
    rejects(Json::array());
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
