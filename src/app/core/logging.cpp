#include "logging.hpp"
#include "error.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
namespace voyemu::logging {
namespace {
std::atomic<bool> human_output = false;
std::mutex console;
// The code points of the live line on the console, 0 without one.
std::size_t live_width = 0;
} // namespace

void human_console(bool enabled) {
    human_output = enabled;
}

void console_line(std::string_view text, bool live) {
    std::lock_guard lock(console);

    if (text.empty() && !live) {
        if (live_width) {
            std::cerr << '\n';
            live_width = 0;
        }

        return;
    }

    const auto width = static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char c) {
        return (static_cast<unsigned char>(c) & 0xC0) != 0x80;
    }));

    if (live_width) {
        std::cerr << '\r' << text << std::string(width < live_width ? live_width - width : 0, ' ');
    } else {
        std::cerr << text;
    }

    live_width = live ? width : 0;
    std::cerr << (live ? "" : "\n") << std::flush;
}

void console_blank() {
    std::lock_guard lock(console);

    std::cerr << (live_width ? "\n\n" : "\n") << std::flush;
    live_width = 0;
}

Json record(Level level, std::string_view component, std::string_view message, Json fields) {
    if (component.empty() || component.size() > 64 || message.size() > 4096 ||
        !fields.is_object() || fields.dump().size() > 8192) {
        throw VoyemuError("LOG_BOUNDS", "invalid native log record");
    }

    static std::atomic<std::uint64_t> sequence{};
    const auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    const auto millis =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() %
            1000;
    std::ostringstream stamp;
    stamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
          << millis << 'Z';
    const char *name = level == Level::error ? "error" : level == Level::info ? "info" : "debug";

    return {{"schemaVersion", 1},
            {"sequence", ++sequence},
            {"timestamp", stamp.str()},
            {"source", {{"kind", "host"}, {"id", "voyemu"}}},
            {"level", name},
            {"component", component},
            {"message", message},
            {"fields", std::move(fields)}};
}

void write(Level level, std::string_view component, std::string_view message, Json fields) {
    const char *threshold = std::getenv("VOYEMU_LOG_LEVEL");

    if (level == Level::debug && (!threshold || (std::string_view(threshold) != "debug" &&
                                                 std::string_view(threshold) != "DEBUG"))) {
        return;
    }

    std::lock_guard lock(console);
    const auto entry = record(level, component, message, std::move(fields));

    if (human_output) {
        if (live_width) {
            std::cerr << '\n';
            live_width = 0;
        }

        std::cerr << component << ": " << message;

        for (const auto &[key, value] : entry.at("fields").items()) {
            if (value.is_primitive()) {
                std::cerr << "; " << key << "=";

                if (value.is_string()) {
                    std::cerr << value.get<std::string>();
                } else {
                    std::cerr << value;
                }
            }
        }

        std::cerr << '\n';
    } else {
        std::cerr << entry.dump() << '\n';
    }
}
} // namespace voyemu::logging
