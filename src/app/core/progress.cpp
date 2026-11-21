#include "progress.hpp"
#include "logging.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <utility>
namespace voyemu::progress {
namespace {
std::atomic<bool> console_enabled = false, console_terminal = false;
std::atomic<cli::Language> console_language = cli::Language::english;
std::atomic<std::uint64_t> lines = 0;
// Whether the console has shown a progress line and whether the next one starts a block.
std::mutex blocks;
bool shown = false, pending = false;
// The open phases, each indenting the lines inside it by two spaces.
unsigned depth = 0;

// The catalog names of a labelled component in language; trailing space of an empty version
// removed.
Arguments named(const Arguments &arguments, cli::Language language) {
    auto result = arguments;
    const auto label = arguments.find("label");

    if (label == arguments.end()) {
        if (const auto name = arguments.find("name"); name != arguments.end()) {
            result["name_of"] = name->second;
        }

        return result;
    }

    const auto version = arguments.find("version");
    const Arguments values{{"version", version == arguments.end() ? "" : version->second}};

    for (const auto &[field, key] :
         {std::pair{"name", "progress.name."}, std::pair{"name_of", "progress.name_of."}}) {
        auto text = cli::message(language, key + label->second, values);

        while (text.ends_with(' ')) {
            text.pop_back();
        }

        result[field] = text;
    }

    return result;
}

void emit(std::string_view key, const Arguments &arguments, bool live) {
    if (key.empty()) {
        if (console_enabled) {
            logging::console_line({}, false);
        }

        return;
    }

    ++lines;

    if (console_enabled) {
        std::string indent;
        {
            std::lock_guard lock(blocks);

            // The lines inside a phase form one block with it.
            if ((pending || key == "download" || key == "download_size") && shown && !depth) {
                logging::console_blank();
            }

            pending = false;
            shown = true;
            indent.assign(std::size_t{2} * depth, ' ');
        }
        logging::console_line(indent + cli::message(console_language,
                                                    "progress." + std::string(key),
                                                    named(arguments, console_language)),
                              live);

        return;
    }

    Json fields = Json::object();

    for (const auto &[name, value] : named(arguments, cli::Language::english)) {
        fields[name] = value;
    }

    // Status lines of a meter are diagnostics; the other lines keep the record of each step.
    logging::write(key.find("_status") != std::string_view::npos ? logging::Level::debug
                                                                 : logging::Level::info,
                   "progress", key, std::move(fields));
}

// The decimal text of value with one decimal, without a trailing zero, in the separator of
// language.
std::string decimal(double value, cli::Language language) {
    char text[32];

    std::snprintf(text, sizeof text, "%.1f", value);
    std::string result = text;

    if (result.ends_with(".0")) {
        result.resize(result.size() - 2);
    } else if (language == cli::Language::russian) {
        std::replace(result.begin(), result.end(), '.', ',');
    }

    return result;
}
} // namespace

void console(cli::Language language, bool terminal) {
    console_language = language;
    console_terminal = terminal;
    console_enabled = true;
}

cli::Language language() {
    return console_language;
}

void report(std::string_view key, const Arguments &arguments) {
    emit(key, arguments, false);
}

Arguments named(const Arguments &arguments) {
    return named(arguments, console_language);
}

void block() {
    std::lock_guard lock(blocks);

    pending = true;
}

void separate() {
    std::lock_guard lock(blocks);

    if (console_enabled && shown) {
        logging::console_blank();
    }

    pending = false;
}

Elapsed::Elapsed(std::string kind, Clock clock)
    : kind_(std::move(kind)), clock_(std::move(clock)), start_(clock_()), shown_(start_) {
    block();
    report(kind_);
}

void Elapsed::tick() {
    const auto now = clock_();

    if (now - shown_ < std::chrono::minutes(1)) {
        return;
    }

    shown_ = now;
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - start_).count();
    report(kind_ + "_status",
           {{"time", duration_text(static_cast<std::uint64_t>(seconds), language())}});
}

void Elapsed::done(std::string_view key) {
    const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(clock_() - start_).count();

    report(key, {{"time", duration_text(static_cast<std::uint64_t>(seconds), language())}});
}

Phase::Phase(std::string kind, const Arguments &arguments, Clock clock)
    : kind_(std::move(kind)), clock_(std::move(clock)), start_(clock_()) {
    block();
    report(kind_, arguments);
    std::lock_guard lock(blocks);
    ++depth;
}

Phase::~Phase() {
    if (open_) {
        std::lock_guard lock(blocks);
        --depth;
    }
}

void Phase::done(const Arguments &arguments) {
    if (open_) {
        std::lock_guard lock(blocks);
        --depth;
        open_ = false;
    }

    const auto seconds =
            std::chrono::duration_cast<std::chrono::seconds>(clock_() - start_).count();
    auto values = arguments;
    values["time"] = duration_text(static_cast<std::uint64_t>(seconds), language());
    report(kind_ + "_done", values);
}

std::uint64_t reported() {
    return lines;
}

void Steps::begin(bool new_block) {
    if (new_block) {
        block();
    }

    before_ = lines;
}

void Steps::end(std::string_view name) {
    const bool cached = lines == before_;

    // A cached step after another cached step continues the compact list without an empty line.
    if (cached && compact_) {
        std::lock_guard lock(blocks);
        pending = false;
    }

    report(std::string(name) + (cached ? "_cached" : "_installed"));
    compact_ = cached;
}

std::string size_text(std::uint64_t bytes, cli::Language language) {
    const bool russian = language == cli::Language::russian;

    if (bytes < 1024) {
        return std::to_string(bytes) + (russian ? " Б" : " B");
    }

    static constexpr const char *iec[] = {"KiB", "MiB", "GiB", "TiB"},
                                *cyrillic[] = {"КиБ", "МиБ", "ГиБ", "ТиБ"};
    auto value = static_cast<double>(bytes) / 1024;
    std::size_t unit = 0;

    while (value >= 1024 && unit + 1 < std::size(iec)) {
        value /= 1024;
        ++unit;
    }

    return decimal(value, language) + " " + (russian ? cyrillic : iec)[unit];
}

std::string rate_text(std::uint64_t bytes_per_second, cli::Language language) {
    return size_text(bytes_per_second, language) +
           (language == cli::Language::russian ? "/с" : "/s");
}

std::string duration_text(std::uint64_t seconds, cli::Language language) {
    const char *second = "s", *minute = "min", *hour = "h";

    if (language == cli::Language::russian) {
        second = "с";
        minute = "мин";
        hour = "ч";
    } else if (language == cli::Language::chinese) {
        second = "秒";
        minute = "分";
        hour = "小时";
    }

    const auto unit = [](std::uint64_t value, const char *name) {
        return std::to_string(value) + " " + name;
    };

    if (seconds < 60) {
        return unit(seconds, second);
    }

    if (seconds < 3600) {
        return unit(seconds / 60, minute) + (seconds % 60 ? " " + unit(seconds % 60, second) : "");
    }

    return unit(seconds / 3600, hour) +
           (seconds / 60 % 60 ? " " + unit(seconds / 60 % 60, minute) : "");
}

std::string path_text(const std::filesystem::path &path) {
    std::error_code error;
    const auto current = std::filesystem::current_path(error);

    if (!error) {
        const auto relative =
                path.lexically_relative(std::filesystem::weakly_canonical(current, error));

        if (!error && !relative.empty() && relative != "." && *relative.begin() != "..") {
            return relative.string();
        }
    }

    return path.string();
}

std::string url_host(std::string_view url) {
    const auto scheme = url.find("://");
    auto host = scheme == std::string_view::npos ? url : url.substr(scheme + 3);

    host = host.substr(0, host.find_first_of("/?#"));

    // A pinned URL carries no credentials; a port follows the last colon.
    if (const auto port = host.rfind(':'); port != std::string_view::npos) {
        host = host.substr(0, port);
    }

    return std::string(host);
}

std::string url_file(std::string_view url) {
    auto path = url.substr(0, url.find_first_of("?#"));

    path = path.substr(path.rfind('/') == std::string_view::npos ? 0 : path.rfind('/') + 1);
    const auto digit = [](char c) {
        return c >= '0' && c <= '9'   ? c - '0'
               : c >= 'a' && c <= 'f' ? c - 'a' + 10
               : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                      : -1;
    };
    std::string file;

    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size() && digit(path[i + 1]) >= 0 &&
            digit(path[i + 2]) >= 0) {
            file += char(digit(path[i + 1]) * 16 + digit(path[i + 2]));
            i += 2;
        } else {
            file += path[i];
        }
    }

    return file;
}

Meter::Meter(std::string kind, Arguments context, bool terminal, Output output, Clock clock,
             cli::Language language)
    : kind_(std::move(kind)), context_(std::move(context)), terminal_(terminal),
      output_(std::move(output)), clock_(std::move(clock)), language_(language) {}

Meter::Meter(std::string kind, Arguments context)
    : Meter(std::move(kind), std::move(context), console_enabled && console_terminal, emit,
            std::chrono::steady_clock::now, console_language) {}

Meter::~Meter() {
    if (live_ && !finished_) {
        try {
            output_({}, {}, false);
        } catch (...) { /* Ignored: an unfinished live line cannot fail the unwinding transfer. */
        }
    }
}

void Meter::begin() {
    if (started_) {
        return;
    }

    started_ = true;
    start_ = shown_ = received_ = clock_();

    output_(kind_ + (total_ ? "_size" : ""), rates(), false);
}

void Meter::length(std::uint64_t total) {
    total_ = total;
    begin();
}

void Meter::advance(std::uint64_t bytes) {
    begin();
    done_ += bytes;
    received_ = clock_();
    const auto quiet = received_ - shown_;

    if (terminal_) {
        if (quiet < std::chrono::milliseconds(500)) {
            return;
        }
    } else {
        const auto step = total_ ? std::min<std::uint64_t>(done_ * 10 / total_, 10) : 0;

        // Steps come at least 2 s apart, so a fast transfer shows only its start and end.
        if (!(step > step_ && step < 10 && quiet >= std::chrono::seconds(2)) &&
            quiet < std::chrono::seconds(30)) {
            return;
        }

        step_ = std::max(step_, std::min<std::uint64_t>(step, 9));
    }

    shown_ = received_;
    live_ = terminal_;
    output_(kind_ + (total_ ? "_status_size" : "_status"), rates(), terminal_);
}

void Meter::finish() {
    finish(kind_ + "_done");
}

void Meter::finish(std::string_view key) {
    begin();
    finished_ = true;
    live_ = false;
    output_(key, rates(), false);
}

Arguments Meter::rates() const {
    const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(received_ - start_).count();
    const std::uint64_t rate =
            milliseconds > 0 ? done_ * 1000 / static_cast<std::uint64_t>(milliseconds) : done_;
    auto arguments = context_;

    arguments["done"] = size_text(done_, language_);
    arguments["rate"] = rate_text(rate, language_);
    arguments["time"] = duration_text(static_cast<std::uint64_t>(milliseconds) / 1000, language_);

    if (total_) {
        const auto remaining = total_ > done_ ? total_ - done_ : 0;
        arguments["total"] = size_text(total_, language_);
        arguments["percent"] = std::to_string(std::min<std::uint64_t>(done_ * 100 / total_, 100));
        arguments["eta"] = rate ? duration_text((remaining + rate - 1) / rate, language_) : "?";
    }

    return arguments;
}
} // namespace voyemu::progress
