#pragma once
#include "cli_messages.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
namespace voyemu::progress {
using Arguments = cli::MessageArguments;
// Shows progress lines on stderr as the CLI catalog messages "progress.<key>" in language; on a
// terminal the status line of a download is replaced in place. Without a console each line is a
// log record, so JSON callers keep their output machine-readable.
void console(cli::Language language, bool terminal);
// The language of the console, English without one.
cli::Language language();
// One line for the console user: the catalog message "progress.<key>" with its arguments. A
// "label" argument names a catalog component progress.name.<label> (nominative, as {name}) and
// progress.name_of.<label> (genitive, as {name_of}) with its "version"; otherwise {name_of} repeats
// {name}.
void report(std::string_view key, const Arguments &arguments = {});
// The next console line starts a new block after an empty line; a download start does this itself.
void block();
// Prints the empty line of a new block now, when the console has shown a progress line.
void separate();
// The steps of a command such as deps, each ending with one line: <name>_cached when the step
// reported nothing, otherwise <name>_installed. A step starts a new block, except that consecutive
// steps that only report themselves as cached form one compact list.
class Steps {
public:
    // Starts a step; new_block false keeps the lines that the caller reported for it before.
    void begin(bool new_block = true);
    // Ends the step with its cached or installed line.
    void end(std::string_view name);

private:
    bool compact_ = true;
    std::uint64_t before_ = 0;
};
// The number of lines reported so far, by which a caller tells whether a step did any work.
std::uint64_t reported();
// A byte count in IEC units with one decimal and no trailing zero: "900 B", "1.5 KiB", "306 MiB";
// Russian uses its unit names and a decimal comma: "1,5 КиБ".
std::string size_text(std::uint64_t bytes, cli::Language language);
// A transfer rate: "9.5 MiB/s", "9,5 МиБ/с".
std::string rate_text(std::uint64_t bytes_per_second, cli::Language language);
// A duration: "42 s", "3 min 5 s", "1 h 2 min"; Russian "42 с", "3 мин 5 с", "1 ч 2 мин".
std::string duration_text(std::uint64_t seconds, cli::Language language);
// The host of an https:// URL.
std::string url_host(std::string_view url);
// The percent-decoded last path segment of a URL.
std::string url_file(std::string_view url);

// The byte progress of one long operation of a kind such as "download": the start line <kind>, or
// <kind>_size once the total is known, appears with the total or the first bytes; status lines
// <kind>_status or <kind>_status_size while bytes arrive; and finish() ends with <kind>_done. Every
// line carries the context arguments and done, rate and time; with a known total also total,
// percent and eta. A terminal gets a live status line at most twice a second; other output gets a
// status line at each further 10% of a known total at least 2 s after the previous one, or after
// 30 s without one.
class Meter {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    // A line to show: its catalog key, its arguments and whether it is a live status line that the
    // next line replaces. An empty key ends a live line without text.
    using Output = std::function<void(std::string_view key, const Arguments &, bool live)>;
    Meter(std::string kind, Arguments context, bool terminal, Output output, Clock clock,
          cli::Language language = cli::Language::english);
    // A meter on the console of console(), or on log records without one.
    Meter(std::string kind, Arguments context);
    ~Meter();
    Meter(const Meter &) = delete;
    Meter &operator=(const Meter &) = delete;
    void length(std::uint64_t total);
    void advance(std::uint64_t bytes);
    void finish();

private:
    void begin();
    [[nodiscard]] Arguments rates() const;

    std::string kind_;
    Arguments context_;
    bool terminal_;
    Output output_;
    Clock clock_;
    cli::Language language_;
    std::uint64_t total_ = 0, done_ = 0, step_ = 0;
    bool started_ = false, live_ = false, finished_ = false;
    std::chrono::steady_clock::time_point start_, shown_, received_;
};
} // namespace voyemu::progress
