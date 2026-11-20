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
// Writes one console line to stderr. A live line is replaced in place by the next console line or
// ended by the next log line; empty text only ends a live line.
void console_line(std::string_view text, bool live);
// Writes an empty console line, after ending a live line.
void console_blank();
void write(Level, std::string_view component, std::string_view message,
           Json fields = Json::object());
} // namespace voyemu::logging
