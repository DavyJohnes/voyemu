#pragma once
#include "model.hpp"
#include <string_view>
namespace voyemu::logging {
enum class Level {
    debug,
    info,
    error
};
Json record(Level, std::string_view component, std::string_view message,
            Json fields = Json::object());
void human_console(bool enabled);
void write(Level, std::string_view component, std::string_view message,
           Json fields = Json::object());
} // namespace voyemu::logging
