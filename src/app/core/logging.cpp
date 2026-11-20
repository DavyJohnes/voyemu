#include "logging.hpp"
#include "error.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <vector>
namespace voyemu::logging {
namespace {
std::atomic<bool> human_output = false, verbose_output = false;
std::mutex console;
// The code points of the live line on the console, 0 without one.
std::size_t live_width = 0;
// The log file of open_file(), guarded by console.
std::ofstream journal;
constexpr std::size_t kept_files = 20;
} // namespace

void human_console(bool enabled) {
    human_output = enabled;
}

void verbose(bool enabled) {
    verbose_output = enabled;
}

std::filesystem::path open_file(const std::filesystem::path &directory, std::string_view command) {
    ensure_private_directory(directory);
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream name;
    name << command << '-' << std::put_time(&local, "%Y%m%d-%H%M%S") << ".log";
    const auto path = directory / name.str();
    {
        std::lock_guard lock(console);

        journal.close();
        journal.clear();
        journal.open(path, std::ios::app | std::ios::binary);
    }
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;

    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && !entry.is_symlink() && entry.path().extension() == ".log") {
            files.emplace_back(entry.last_write_time(), entry.path());
        }
    }

    // Newest first; the file just opened is the newest even within one timestamp tick.
    std::sort(files.begin(), files.end(), [&](const auto &left, const auto &right) {
        if ((left.second == path) != (right.second == path)) {
            return left.second == path;
        }

        return left.first != right.first ? left.first > right.first : left.second > right.second;
    });

    // A file that another process still holds open stays until a later command removes it.
    for (std::size_t i = kept_files; i < files.size(); ++i) {
        std::error_code ignored;

        std::filesystem::remove(files[i].second, ignored);
    }

    return path;
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
    const bool debug = threshold && (std::string_view(threshold) == "debug" ||
                                     std::string_view(threshold) == "DEBUG");
    // An ordinary command shows records only with --verbose; the log file keeps every record.
    const bool shown = (level != Level::debug || debug) && (!human_output || verbose_output);
    std::lock_guard lock(console);

    if (!shown && !journal.is_open()) {
        return;
    }

    Json entry;

    try {
        entry = record(level, component, message, std::move(fields));
    } catch (const std::exception &) {
        // A record that only the log file would keep never fails the operation that writes it.
        if (!shown) {
            return;
        }

        throw;
    }

    if (journal.is_open()) {
        journal << entry.dump(-1, ' ', false, Json::error_handler_t::replace) << '\n' << std::flush;
    }

    if (!shown) {
        return;
    }

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

void try_write(Level level, std::string_view component, std::string_view message,
               Json fields) noexcept {
    try {
        write(level, component, message, std::move(fields));
    } catch (...) { /* Ignored: a cleanup path keeps its own outcome without its record. */
    }
}
} // namespace voyemu::logging
