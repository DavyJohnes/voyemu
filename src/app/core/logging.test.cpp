#include "logging.hpp"
#include "process.hpp"
#include <cassert>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <type_traits>
#include <vector>

int main() try {
    using namespace voyemu;
    using namespace voyemu::logging;
    auto info = record(Level::info, "supervisor", "session.started", {{"pid", 42}});
    assert(info.at("schemaVersion") == 1 && info.at("level") == "info" &&
           info.at("source").at("kind") == "host");
    assert(std::regex_match(info.at("timestamp").get<std::string>(),
                            std::regex(R"(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z)")));
    auto error = record(Level::error, "graphics", "rpc.failed"),
         debug = record(Level::debug, "graphics", "rpc.completed");
    assert(error.at("level") == "error" && debug.at("level") == "debug" &&
           error.at("sequence") == info.at("sequence").get<unsigned>() + 1);
    assert(Json::parse(record(Level::debug, "cluster", "text\nwith\"quotes").dump())
                   .at("message") == "text\nwith\"quotes");
    bool failed = false;
    try {
        record(Level::info, "", "invalid");
    } catch (...) {
        failed = true;
    }

    assert(failed);
    failed = false;
    try {
        record(Level::info, "module", "invalid", Json::array());
    } catch (...) {
        failed = true;
    }

    assert(failed);
    // An ordinary command shows no record on its console; --verbose shows them as text.
    std::ostringstream captured;
    auto *prior = std::cerr.rdbuf(captured.rdbuf());
    human_console(true);
    write(Level::info, "fetch", "hidden");
    write(Level::error, "fetch", "hidden");
    verbose(true);
    write(Level::info, "fetch", "inputs verified", {{"files", 2}});
    verbose(false);
    human_console(false);
    write(Level::info, "fixture", "retained JSON");
    std::cerr.rdbuf(prior);
    const auto split = captured.str().find('\n');
    assert(captured.str().substr(0, split) == "fetch: inputs verified; files=2");
    assert(Json::parse(captured.str().substr(split + 1)).at("message") == "retained JSON");
    // A live line is replaced in place, padded over its longer predecessor; a log line or empty
    // text ends it.
    std::ostringstream console;
    prior = std::cerr.rdbuf(console.rdbuf());
    human_console(true);
    verbose(true);
    console_line("12.0 MiB", true);
    console_line("9 MiB", true);
    console_line("Done", false);
    console_line("1 MiB", true);
    write(Level::info, "fetch", "inputs verified");
    console_line("2 MiB", true);
    console_line({}, false);
    console_line({}, false);
    verbose(false);
    human_console(false);
    std::cerr.rdbuf(prior);
    assert(console.str() == "12.0 MiB\r9 MiB   \rDone \n1 MiB\nfetch: inputs verified\n2 MiB\n");
    // Chinese ideographs take two terminal columns each, so a shorter live line covers them all.
    std::ostringstream wide;
    prior = std::cerr.rdbuf(wide.rdbuf());
    console_line("\u4e0b\u8f7d 12 MiB", true);
    console_line("9 MiB", true);
    console_line({}, false);
    std::cerr.rdbuf(prior);
    assert(terminal_columns("\u4e0b\u8f7d 12 MiB") == 11 &&
           wide.str() == "\u4e0b\u8f7d 12 MiB\r9 MiB      \n");
    // try_write() records like write() but drops an invalid record instead of throwing.
    using JsonTryWrite = void (*)(Level, std::string_view, std::string_view, Json) noexcept;
    using FieldTryWrite = void (*)(Level, std::string_view, std::string_view, std::string_view,
                                   const char *) noexcept;
    static_assert(std::is_nothrow_invocable_v<JsonTryWrite, Level, std::string_view,
                                              std::string_view, Json>);
    static_assert(std::is_nothrow_invocable_v<FieldTryWrite, Level, std::string_view,
                                              std::string_view, std::string_view, const char *>);
    [[maybe_unused]] const JsonTryWrite json_try_write = &try_write;
    [[maybe_unused]] const FieldTryWrite field_try_write = &try_write;
    std::ostringstream cleanup_records;
    prior = std::cerr.rdbuf(cleanup_records.rdbuf());
    try_write(Level::error, "", "invalid component");
    try_write(Level::error, "session", "terminal.stop_failed", "error", "stop failed");
    std::cerr.rdbuf(prior);
    const auto cleanup_record = Json::parse(cleanup_records.str());
    assert(cleanup_record.at("component") == "session" &&
           cleanup_record.at("fields").at("error") == "stop failed");
    // The log file of a command keeps every record as JSON Lines, debug included, while the
    // console shows none; the newest 20 log files stay.
    const auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                           ("voyemu-logging-" + random_token());
    struct Cleanup {
        std::filesystem::path path;

        ~Cleanup() {
            std::error_code ignored;

            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{directory};
    ensure_private_directory(directory);
    const auto past = std::filesystem::file_time_type::clock::now() - std::chrono::hours(1);

    for (int i = 0; i < 25; ++i) {
        const auto old = directory / ("fetch-old-" + std::to_string(100 + i) + ".log");
        std::ofstream(old) << "{}\n";
        std::filesystem::last_write_time(old, past + std::chrono::seconds(i));
    }

    std::ofstream(directory / "notes.txt") << "kept\n";
    std::ostringstream quiet;
    prior = std::cerr.rdbuf(quiet.rdbuf());
    human_console(true);
    const auto file = open_file(directory, "build");
    write(Level::info, "qnx", "checkpoint.selected", {{"mode", "cold"}});
    write(Level::debug, "qnx", "guest.marker");
    write(Level::error, "build", "build output retained");
    human_console(false);
    std::cerr.rdbuf(prior);
    assert(quiet.str().empty());
    assert(file.parent_path() == directory &&
           std::regex_match(file.filename().string(), std::regex(R"(build-\d{8}-\d{6}\.log)")));
    std::ifstream lines(file);
    std::vector<Json> records;

    for (std::string line; std::getline(lines, line);) {
        records.push_back(Json::parse(line));
    }

    assert(records.size() == 3 && records[0].at("message") == "checkpoint.selected" &&
           records[0].at("fields").at("mode") == "cold" && records[1].at("level") == "debug" &&
           records[2].at("level") == "error");
    std::size_t logs = 0;

    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() == ".log") {
            ++logs;
            const auto name = entry.path().filename().string();
            // The new file and the 19 newest old ones stay: fetch-old-106 .. fetch-old-124.
            assert(entry.path() == file || name >= "fetch-old-106.log");
        }
    }

    assert(logs == 20 && std::filesystem::exists(directory / "notes.txt"));
    std::cout << "native canonical DEBUG/INFO/ERROR JSONL record guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
