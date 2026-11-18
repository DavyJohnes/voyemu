#include "pointer_drag.hpp"
#include "error.hpp"
#include <cmath>
#include <stdexcept>
namespace voyemu::api {
namespace {
int integer(const Json &value, int minimum, int maximum) {
    if ((!value.is_number_integer() && !value.is_number_unsigned()) ||
        (value.is_number_unsigned()
                 ? (value.get<std::uint64_t>() < static_cast<std::uint64_t>(minimum) ||
                    value.get<std::uint64_t>() > static_cast<std::uint64_t>(maximum))
                 : value.get<std::int64_t>() < minimum || value.get<std::int64_t>() > maximum)) {
        throw VoyemuError("TEST_ARGUMENT",
                          "drag requires bounded integer coordinates and duration");
    }

    return value.get<int>();
}
} // namespace

Json drag_gesture(const Json &request, int width, int height) {
    if (width <= 0 || height <= 0) {
        throw VoyemuError("TEST_ARGUMENT", "drag requires positive screen dimensions");
    }

    if (!request.is_object() || request.value("op", Json{}) != "input.drag" ||
        !request.contains("screen") || !request.at("screen").is_string() ||
        (request.at("screen") != "primary" && request.at("screen") != "secondary") ||
        request.contains("screens") || request.contains("points") ||
        request.contains("display_id")) {
        throw VoyemuError("TEST_ARGUMENT", "drag requires one Android screen and endpoints");
    }

    int coordinates[4]{};
    int index = 0;

    for (const char *key : {"from", "to"}) {
        if (!request.contains(key) || !request.at(key).is_object() || request.at(key).size() != 2 ||
            !request.at(key).contains("x") || !request.at(key).contains("y")) {
            throw VoyemuError("TEST_ARGUMENT", "drag endpoint requires x and y");
        }

        coordinates[index++] = integer(request.at(key).at("x"), 0, width - 1);
        coordinates[index++] = integer(request.at(key).at("y"), 0, height - 1);
    }

    const int duration = integer(request.value("duration_ms", Json(300)), 1, 2000);
    const int intervals = (duration + 31) / 32;
    Json points = Json::array();

    for (int i = 0; i <= intervals; ++i) {
        const auto interpolate = [&](int from, int to) {
            return static_cast<int>(
                    std::llround(from + (static_cast<double>(to) - from) * i / intervals));
        };
        points.push_back({{"x", interpolate(coordinates[0], coordinates[2])},
                          {"y", interpolate(coordinates[1], coordinates[3])},
                          {"at_ms", duration * i / intervals}});
    }

    Json result = request;
    result["op"] = "input.gesture";
    result["points"] = std::move(points);

    for (const char *key : {"from", "to", "duration_ms"}) {
        result.erase(key);
    }

    return result;
}
} // namespace voyemu::api
