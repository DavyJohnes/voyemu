#pragma once
#include "model.hpp"
#include <filesystem>
#include <string_view>
namespace voyemu::logging {
enum class Level {
    debug,
    info,
    error
};
Json record(Level, std::string_view component, std::string_view message,
            Json fields = Json::object());
// Ordinary commands: write() shows no record on the console unless verbose() asks for it.
void human_console(bool enabled);
// On a human console, write() also prints info and error records, and debug records with
// VOYEMU_LOG_LEVEL=debug, as "component: message; key=value".
void verbose(bool enabled);
// Starts the log file <directory>/<command>-<YYYYMMDD-HHMMSS>.log of the local time and keeps the
// newest 20 log files of directory; write() appends every later record to it as one JSON line,
// debug records included. Returns its path.
std::filesystem::path open_file(const std::filesystem::path &directory, std::string_view command);
// Writes one console line to stderr. A live line is replaced in place by the next console line or
// ended by the next log line; empty text only ends a live line.
void console_line(std::string_view text, bool live);
// Writes an empty console line, after ending a live line.
void console_blank();
void write(Level, std::string_view component, std::string_view message,
           Json fields = Json::object());
// write() for destructors and cleanup paths: a record that cannot be written is dropped, never
// thrown.
void try_write(Level, std::string_view component, std::string_view message,
               Json fields = Json::object()) noexcept;
} // namespace voyemu::logging
