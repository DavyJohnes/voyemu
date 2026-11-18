#pragma once
#include "model.hpp"
#include "process.hpp"
#include <charconv>
#include <chrono>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
namespace voyemu {
// A diagnostic decoder, never a source of guest identity or mutation authority.
class AndroidAdbTraceSanitizer {
    struct Stream {
        std::string kind = "unknown", action;
        std::uint64_t packets = 0, bytes = 0;
    };
    unsigned console_;
    std::uint64_t generation_;
    std::string line_;
    bool oversized_ = false;
    std::map<std::uint32_t, Stream> streams_;
    std::uint64_t discarded_ = 0, oversized_lines_ = 0, emitted_ = 0;

    static bool number(std::string_view value, std::uint32_t &output) {
        int base = 10;

        if (value.starts_with("0x")) {
            value.remove_prefix(2);
            base = 16;
        }

        if (value.empty() || value.size() > 10) {
            return false;
        }

        const auto result = std::from_chars(value.data(), value.data() + value.size(), output,
                                            base);

        return result.ec == std::errc{} && result.ptr == value.data() + value.size();
    }

    static bool field(std::string_view rest, std::string_view name, std::uint32_t &value) {
        const auto begin = rest.find(name);

        if (begin == std::string_view::npos) {
            return false;
        }

        auto text = rest.substr(begin + name.size());
        const auto end = text.find_first_of(" )\r\n");

        return number(text.substr(0, end), value);
    }

    static Stream service(std::string_view value) {
        Stream stream;

        if (value.starts_with("shell")) {
            stream.kind = "shell";
        } else if (value.starts_with("exec")) {
            stream.kind = "exec";
        } else if (value.starts_with("host")) {
            stream.kind = "host";
        }

        constexpr std::string_view marker = "VOYEMU_STARTUP_";
        std::string frame;

        for (std::size_t at = 0; (at = value.find(marker, at)) != std::string_view::npos;) {
            at += marker.size();

            if (value.size() - at < 32) {
                frame.clear();
                break;
            }

            auto candidate = value.substr(at, 32);

            if (candidate.find_first_not_of("0123456789abcdef") != std::string_view::npos) {
                frame.clear();
                break;
            }

            if (!frame.empty() && frame != candidate) {
                frame.clear();
                break;
            }

            frame = std::string(candidate);
            at += 32;
        }

        if (!frame.empty()) {
            stream.kind = "framed_startup";
            stream.action = sha256_bytes(frame);
        }

        return stream;
    }

    std::string decode(std::string_view text, std::int64_t child) {
        std::istringstream input{std::string(text)};
        std::string date, time, pid, tid, level, program, colon, source;
        std::uint32_t parsed_pid = 0;

        if (!(input >> date >> time >> pid >> tid >> level >> program >> colon >> source) ||
            !number(pid, parsed_pid) || parsed_pid != child ||
            (program != "adb.exe" && program != "adb") || colon != ":" ||
            (level != "D" && level != "V")) {
            return {};
        }

        std::string rest;
        std::getline(input, rest);

        if (source.starts_with("sockets.cpp:")) {
            const auto at = rest.find("LS(");
            const auto connect = rest.find(": connect(");

            if (at == std::string::npos || connect == std::string::npos || connect < at + 3) {
                return {};
            }

            std::uint32_t id = 0;

            if (!number(std::string_view(rest).substr(at + 3, connect - at - 3), id)) {
                return {};
            }

            if (streams_.size() >= 64 && !streams_.contains(id)) {
                ++discarded_;

                return {};
            }

            streams_[id] = service(std::string_view(rest).substr(connect + 10));

            return {};
        }

        if (!source.starts_with("transport.cpp:")) {
            return {};
        }

        const auto serial = " emulator-" + std::to_string(console_) + ": ";

        if (!rest.starts_with(serial)) {
            return {};
        }

        auto packet = std::string_view(rest).substr(serial.size());
        bool outgoing = false;

        if (packet.starts_with("to remote: [")) {
            outgoing = true;
            packet.remove_prefix(12);
        } else if (packet.starts_with("from remote: [")) {
            packet.remove_prefix(14);
        } else {
            return {};
        }

        if (packet.size() < 5 || packet[4] != ']') {
            return {};
        }

        const auto tag = packet.substr(0, 4);

        if (tag != "OPEN" && tag != "OKAY" && tag != "WRTE" && tag != "CLSE" && tag != "AUTH" &&
            tag != "CNXN") {
            return {};
        }

        std::uint32_t arg0 = 0, arg1 = 0, length = 0;

        if (!field(packet, "arg0=", arg0) || !field(packet, "arg1=", arg1) ||
            !field(packet, "(len=", length) || length > 16 * 1024 * 1024) {
            return {};
        }

        const auto id = outgoing ? arg0 : arg1;
        Stream *stream = nullptr;

        if (tag == "OPEN" || tag == "OKAY" || tag == "WRTE" || tag == "CLSE") {
            if (auto found = streams_.find(id); found != streams_.end()) {
                stream = &found->second;
                ++stream->packets;
                stream->bytes += length;
            }
        }

        Json event = {
                {"event", "adb_packet"},
                {"server_pid", child},
                {"generation", generation_},
                {"direction", outgoing ? "to_remote" : "from_remote"},
                {"packet", tag},
                {"arg0", arg0},
                {"arg1", arg1},
                {"length", length},
                {"classification", stream ? stream->kind : "unknown"},
                {"host_receive_utc_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                                std::chrono::system_clock::now().time_since_epoch())
                                                .count()},
                {"host_receive_steady_ns",
                 std::chrono::duration_cast<std::chrono::nanoseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count()}};

        if (stream && !stream->action.empty()) {
            event["action_sha256"] = stream->action;
        }

        if (tag == "CLSE" && stream) {
            event["stream_packet_count"] = stream->packets;
            event["stream_payload_bytes"] = stream->bytes;
            event["stream_closed"] = true;
            streams_.erase(id);
        }

        ++emitted_;

        return event.dump() + "\n";
    }

public:
    AndroidAdbTraceSanitizer(unsigned console, std::uint64_t generation)
        : console_(console), generation_(generation) {
        line_.reserve(16384);
    }

    std::string operator()(std::string_view chunk, std::int64_t child, bool eof) {
        std::string output;

        output.reserve(32768);

        for (char ch : chunk) {
            if (ch == '\n') {
                if (!oversized_) {
                    auto event = decode(line_, child);

                    if (!event.empty() && output.size() + event.size() <= 32768) {
                        output += event;
                    } else {
                        ++discarded_;
                    }
                }

                line_.clear();
                oversized_ = false;
                continue;
            }

            if (!oversized_) {
                if (line_.size() == 16384) {
                    line_.clear();
                    oversized_ = true;
                    ++oversized_lines_;
                } else {
                    line_ += ch;
                }
            }
        }

        if (eof) {
            if (!line_.empty() || oversized_) {
                ++discarded_;
            }

            line_.clear();
            streams_.clear();
            const auto summary =
                    Json{{"event", "adb_trace_eof"},         {"server_pid", child},
                         {"generation", generation_},        {"emitted_packets", emitted_},
                         {"unclassified_lines", discarded_}, {"oversized_lines", oversized_lines_}}
                            .dump() +
                    "\n";

            if (output.size() + summary.size() <= 32768) {
                output += summary;
            }
        }

        return output;
    }
};
} // namespace voyemu
