#include "logging.hpp"
#include <cassert>
#include <exception>
#include <iostream>
#include <regex>
#include <sstream>

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
    std::ostringstream captured;
    auto *prior = std::cerr.rdbuf(captured.rdbuf());
    human_console(true);
    write(Level::info, "fetch", "inputs verified", {{"files", 2}});
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
    console_line("12.0 MiB", true);
    console_line("9 MiB", true);
    console_line("Done", false);
    console_line("1 MiB", true);
    write(Level::info, "fetch", "inputs verified");
    console_line("2 MiB", true);
    console_line({}, false);
    console_line({}, false);
    human_console(false);
    std::cerr.rdbuf(prior);
    assert(console.str() == "12.0 MiB\r9 MiB   \rDone \n1 MiB\nfetch: inputs verified\n2 MiB\n");
    std::cout << "native canonical DEBUG/INFO/ERROR JSONL record guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
