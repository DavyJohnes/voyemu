#include "system_observation.hpp"
#include "error.hpp"
#include "process.hpp"
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
namespace voyemu {
Json parse_system_power(std::string_view text) {
    Json result = {{"status", "unavailable"},
                   {"complete", false},
                   {"wakefulness", nullptr},
                   {"display_state", nullptr},
                   {"display_ready", nullptr},
                   {"coverage", "point sample; Android display power, not vehicle ACC"}};

    if (text.size() >= 128 * 1024) {
        result["reason"] = "power dump exceeds bounded read";

        return result;
    }

    std::istringstream input{std::string(text)};
    std::string row;
    bool heading = false;
    std::set<std::string> seen;
    bool invalid = false;
    const std::regex field(R"(\s*(mWakefulness|mDisplayReady|Display Power: state)=(\S+)\s*)");

    while (std::getline(input, row)) {
        if (row == "POWER MANAGER (dumpsys power)" || row == "POWER MANAGER (dumpsys power)\r") {
            heading = true;
        }

        std::smatch match;

        if (!std::regex_match(row, match, field)) {
            continue;
        }

        const auto key = match[1].str(), value = match[2].str();

        if (!seen.insert(key).second) {
            invalid = true;
            continue;
        }

        if (key == "mWakefulness") {
            if (value == "Awake" || value == "Asleep" || value == "Dreaming" || value == "Dozing") {
                result["wakefulness"] = value;
            } else {
                invalid = true;
            }
        } else if (key == "mDisplayReady") {
            if (value == "true" || value == "false") {
                result["display_ready"] = value == "true";
            } else {
                invalid = true;
            }
        } else {
            if (value == "ON" || value == "OFF" || value == "DOZE" || value == "DOZE_SUSPEND" ||
                value == "VR" || value == "UNKNOWN") {
                result["display_state"] = value;
            } else {
                invalid = true;
            }
        }
    }

    if (!heading || invalid) {
        result["wakefulness"] = nullptr;
        result["display_state"] = nullptr;
        result["display_ready"] = nullptr;
        result["reason"] = invalid ? "ambiguous or invalid power fields"
                                   : "unrecognized Android power heading";

        return result;
    }

    const bool complete = !result["wakefulness"].is_null() && !result["display_state"].is_null() &&
                          !result["display_ready"].is_null();
    result["complete"] = complete;
    result["status"] = complete ? "ready" : seen.empty() ? "unavailable" : "partial";

    if (!complete) {
        result["reason"] = "missing Android power fields";
    }

    return result;
}

Json parse_system_components(std::string_view text, bool services) {
    const auto unavailable = [](const char *reason) {
        return Json{{"status", "unavailable"},
                    {"items", Json::array()},
                    {"complete", false},
                    {"reason", reason},
                    {"coverage", "point sample; not continuous"}};
    };

    if (text.size() >= 128 * 1024) {
        return unavailable("inventory exceeds bounded read");
    }

    std::istringstream input{std::string(text)};
    std::string row;
    bool heading = false, empty = false;
    std::size_t malformed = 0;
    Json items = Json::array();
    std::set<std::string> seen;
    const std::regex record(
            services
                    ? R"(.*ServiceRecord\{[0-9a-fA-F]+ u([0-9]{1,9}) ([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+)/([A-Za-z_.][A-Za-z0-9_.$]*)\}.*)"
                    : R"(.*ActivityRecord\{[0-9a-fA-F]+ u([0-9]{1,9}) ([A-Za-z][A-Za-z0-9_]*(?:\.[A-Za-z][A-Za-z0-9_]*)+)/([A-Za-z_.][A-Za-z0-9_.$]*)(?: t[0-9]+)?\}.*)");

    while (std::getline(input, row)) {
        if (std::regex_match(
                    row,
                    std::regex(
                            services
                                    ? R"(ACTIVITY MANAGER SERVICES(?: \(dumpsys activity services\))?\r?)"
                                    : R"(ACTIVITY MANAGER ACTIVITIES(?: \(dumpsys activity activities\))?\r?)"))) {
            heading = true;
        }

        if (heading && std::regex_match(row, std::regex(R"(\s*\(nothing\)\s*)"))) {
            empty = true;
        }

        const auto at = row.find(services ? "ServiceRecord{" : "ActivityRecord{");

        if (at == std::string::npos) {
            continue;
        }

        std::smatch match;

        if (!std::regex_match(row, match, record)) {
            ++malformed;
            continue;
        }

        const auto component = match[2].str() + "/" + match[3].str();
        const auto key = match[1].str() + ":" + component;

        if (component.size() > 512 || items.size() >= 1024) {
            return unavailable("inventory exceeds item limit");
        }

        if (seen.insert(key).second) {
            items.push_back({{"user", std::stoll(match[1].str())}, {"component", component}});
        }
    }

    if (!heading) {
        return unavailable("unrecognized Android inventory heading");
    }

    if (items.empty() && (!empty || malformed)) {
        return unavailable("no complete inventory records");
    }

    const bool complete = malformed == 0 && !(empty && !items.empty());

    return {{"status", complete ? "ready" : "partial"},
            {"items", items},
            {"complete", complete},
            {"malformed_records", malformed},
            {"coverage", "point sample; not continuous"}};
}

Json parse_system_processes(std::string_view text) {
    if (text.size() > 128 * 1024) {
        throw VoyemuError("TEST_SYSTEM_FORMAT", "process inventory exceeds limit");
    }

    std::istringstream input{std::string(text)};
    std::string row;

    if (!std::getline(input, row) ||
        !std::regex_match(row, std::regex(R"(\s*PID\s+PPID\s+UID\s+NAME\s*)"))) {
        throw VoyemuError("TEST_SYSTEM_FORMAT", "process header missing");
    }

    Json result = Json::array();
    std::set<std::int64_t> seen;

    while (std::getline(input, row)) {
        if (row.find_first_not_of(" \r\t") == std::string::npos) {
            continue;
        }

        std::istringstream fields(row);
        std::int64_t pid, ppid, uid;
        std::string name, extra;

        if (!(fields >> pid >> ppid >> uid >> name) || pid <= 0 || ppid < 0 || uid < 0 ||
            name.size() > 256 || (fields >> extra) || !seen.insert(pid).second ||
            result.size() >= 4096) {
            throw VoyemuError("TEST_SYSTEM_FORMAT",
                              "complete unique numeric process rows required");
        }

        result.push_back({{"pid", pid}, {"ppid", ppid}, {"uid", uid}, {"name", name}});
    }

    if (result.empty()) {
        throw VoyemuError("TEST_SYSTEM_FORMAT", "empty inventory is not proof of absence");
    }

    return result;
}

Json parse_system_anr(std::string_view text) {
    if (text.size() > 128 * 1024) {
        throw VoyemuError("TEST_SYSTEM_FORMAT", "ANR dump exceeds limit");
    }

    std::istringstream input{std::string(text)};
    std::string row;
    bool heading = false, none = false;
    Json record = Json::object();

    while (std::getline(input, row)) {
        if (row.find("ACTIVITY MANAGER LAST ANR") != std::string::npos) {
            heading = true;
        }

        if (row.find("<no ANR has occurred>") != std::string::npos ||
            row.find("<no ANR has occurred since boot>") != std::string::npos) {
            none = true;
        }

        auto field = [&](const char *label, const char *key) {
            auto at = row.find(label);

            if (at == std::string::npos) {
                return;
            }

            auto value = row.substr(at + std::string(label).size());
            auto first = value.find_first_not_of(" \t");

            if (first == std::string::npos) {
                return;
            }

            value.erase(0, first);

            if (value.size() > 512) {
                value.resize(512);
            }

            record[key] = value;
        };
        field("ANR in ", "process");
        field("Reason: ", "reason");
        field("Time: ", "reported_time");
        std::smatch match;

        if (std::regex_match(row, match, std::regex(R"(\s*PID:\s*([0-9]+)\s*)"))) {
            record["pid"] = std::stoll(match[1].str());
        }
    }

    if (!heading) {
        return {{"status", "unknown"}, {"reason", "unrecognized Android lastanr format"}};
    }

    if (none && record.empty()) {
        return {{"status", "absent"}, {"scope", "last ANR retained by activity manager"}};
    }

    if (record.contains("process") && record.contains("reason")) {
        return {{"status", "present"},
                {"record", record},
                {"scope", "last ANR retained by activity manager"}};
    }

    return {{"status", "unknown"}, {"reason", "incomplete Android lastanr record"}};
}

Json parse_anr_trace_inventory(std::string_view text) {
    if (text.size() > 16 * 1024) {
        throw VoyemuError("TEST_LOGS_FORMAT", "ANR inventory exceeds limit");
    }

    std::set<std::string> dated;
    bool legacy = false;
    const std::regex name(
            R"(^anr_[0-9]{4}-[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{2}-[0-9]{2}[A-Za-z0-9_.-]*$)");
    std::istringstream input{std::string(text)};
    std::string row;

    while (std::getline(input, row)) {
        if (row.empty() || row.front() != '-') {
            continue;
        }

        std::istringstream fields(row);
        std::string field, last;

        while (fields >> field) {
            last = field;
        }

        if (last == "traces.txt") {
            legacy = true;
        } else if (std::regex_match(last, name)) {
            dated.insert(last);
        }

        if (dated.size() > 64) {
            throw VoyemuError("TEST_LOGS_FORMAT", "too many retained ANR traces");
        }
    }

    Json files = Json::array();

    for (const auto &value : dated) {
        files.push_back(value);
    }

    if (legacy) {
        files.push_back("traces.txt");
    }

    return {{"status", "ready"},
            {"files", files},
            {"selected", !dated.empty() ? Json(*dated.rbegin())
                         : legacy       ? Json("traces.txt")
                                        : Json(nullptr)},
            {"selection", "newest recognized dated basename; otherwise standard traces.txt"},
            {"boot_association", "unknown"}};
}

std::string redact_diagnostic_text(std::string_view text, const std::vector<std::string> &secrets) {
    std::string sanitized(text);

    for (const auto &secret : secrets) {
        if (!secret.empty()) {
            std::size_t at = 0;

            while ((at = sanitized.find(secret, at)) != std::string::npos) {
                sanitized.replace(at, secret.size(), "[redacted]");
                at += 10;
            }
        }
    }

    const std::regex labeled(
            R"((token|password|secret|authorization)(\s*[:=]\s*)("[^"]*"|'[^']*'|[^\s,;]+))",
            std::regex::icase);
    const std::regex bearer(R"((authorization\s*[:=]\s*Bearer\s+)[^\s,;]+)", std::regex::icase);
    sanitized = std::regex_replace(sanitized, bearer, "$1[redacted]");
    sanitized = std::regex_replace(sanitized, labeled, "$1$2[redacted]");

    for (auto &ch : sanitized) {
        const auto byte = static_cast<unsigned char>(ch);

        if ((byte < 32 && ch != '\n' && ch != '\t' && ch != '\r') || byte >= 127) {
            ch = ' ';
        }
    }

    return sanitized;
}

std::string native_handler_diagnostics_command() {
    return "value=$(logcat -d -b main -b system -b crash -v epoch -m 128 libc:V DEBUG:V crash_dump32:V crash_dump64:V tombstoned:V libdebuggerd_client:V debuggerd:V IPCThreadState:W ProcessState:W '*:S' 2>/dev/null); if [ $? -eq 0 ]; then printf '%s' \"$value\" | head -c 32769; else echo VOYEMU_NATIVE_UNAVAILABLE; fi";
}

Json parse_native_handler_diagnostics(std::string_view text,
                                      const std::vector<std::string> &secrets) {
    if (text.find("VOYEMU_NATIVE_UNAVAILABLE") != std::string_view::npos) {
        return {{"status", "unavailable"},
                {"reason", "fixed handler diagnostics read failed with existing SDK identity"},
                {"source_complete", false}};
    }

    auto result = parse_native_prefix(text, secrets);
    result["selection"] =
            "first 128 matching lines across main/system/crash; full buffer completeness unknown";
    result["selected_text_complete"] = result.at("source_complete");
    result["source_complete"] = false;
    result["clock"] = "android.realtime";

    return result;
}

Json parse_native_inventory(std::string_view text) {
    if (text.size() >= 8192) {
        throw VoyemuError("TEST_LOGS_FORMAT", "native inventory exceeds bound");
    }

    Json files = Json::array();
    std::istringstream input{std::string(text)};
    std::string row;
    const std::regex name(R"(tombstone_[0-9]{2})");

    while (std::getline(input, row)) {
        if (row.empty() || row.front() != '-') {
            continue;
        }

        std::istringstream fields(row);
        std::string value, last;

        while (fields >> value) {
            last = value;
        }

        if (std::regex_match(last, name)) {
            files.push_back({{"basename", last}, {"metadata", row}});
        }

        if (files.size() > 100) {
            throw VoyemuError("TEST_LOGS_FORMAT", "native inventory exceeds file limit");
        }
    }

    return {{"status", "ready"},
            {"files", files},
            {"boot_association", "unknown; retained files can predate this boot"},
            {"source_sha256", sha256_bytes(text)}};
}

Json parse_native_prefix(std::string_view text, const std::vector<std::string> &secrets) {
    if (text.size() > 32769) {
        throw VoyemuError("TEST_LOGS_FORMAT", "native prefix exceeds bound");
    }

    const bool truncated = text.size() > 32768;
    const auto captured = text.substr(0, 32768);
    auto sanitized = redact_diagnostic_text(captured, secrets);
    Json pid = nullptr, timestamp = nullptr;
    const std::string original(captured);
    std::smatch match;

    if (std::regex_search(original, match,
                          std::regex(R"((?:^|\n)pid: ([0-9]+), tid: ([0-9]+), name:)"))) {
        pid = std::stoll(match[1].str());
    }

    if (std::regex_search(sanitized, match, std::regex(R"((?:^|\n)Timestamp: ([^\r\n]+))"))) {
        timestamp = match[1].str();
    }

    return {{"status", "ready"},
            {"excerpt", sanitized},
            {"pid", pid},
            {"timestamp", timestamp},
            {"source_complete", !truncated},
            {"excerpt_truncated", truncated},
            {"captured_bytes", captured.size()},
            {"captured_sha256", sha256_bytes(captured)},
            {"read_limit_bytes", 32768},
            {"redacted", sanitized != captured},
            {"boot_association", "unknown; retained text does not prove current boot"}};
}

Json parse_anr_diagnostics(std::string_view text, const std::vector<std::string> &secrets) {
    if (text.size() > 128 * 1024) {
        throw VoyemuError("TEST_LOGS_FORMAT", "ANR dump exceeds limit");
    }

    auto sanitized = redact_diagnostic_text(text, secrets);
    const auto summary = parse_system_anr(sanitized);
    const bool truncated = sanitized.size() > 16 * 1024;

    if (truncated) {
        sanitized.resize(16 * 1024);
    }

    return {{"status", "ready"},
            {"source", "anr"},
            {"summary", summary},
            {"excerpt", sanitized},
            {"excerpt_truncated", truncated},
            {"source_bytes", text.size()},
            {"source_sha256", sha256_bytes(text)},
            {"continuous", false},
            {"gap", nullptr},
            {"cursor", nullptr},
            {"coverage", "last ANR retained by activity manager; not continuous"}};
}

Json parse_android_logs(std::string_view text, bool details,
                        const std::vector<std::string> &secrets) {
    if (text.size() > 128 * 1024) {
        throw VoyemuError("TEST_LOGS_FORMAT", "bounded Android log snapshot required");
    }

    Json events = Json::array();
    std::istringstream input{std::string(text)};
    std::string row;
    const std::regex format(
            R"(^\s*([0-9]+\.[0-9]+)\s+([0-9]+)\s+([0-9]+)\s+([VDIWEF])\s+([^ :]+)\s*:(.*)$)");
    const std::set<std::string> diagnostic_tags{"art",     "DEBUG",       "libc",
                                                "dex2oat", "app_process", "AndroidRuntime"};
    const std::regex labeled(
            R"((token|password|secret|authorization)(\s*[:=]\s*)("[^"]*"|'[^']*'|[^\s,;]+))",
            std::regex::icase);
    std::size_t skipped = 0;

    while (std::getline(input, row)) {
        if (row.starts_with("---------")) {
            continue;
        }

        std::smatch m;

        if (!std::regex_match(row, m, format)) {
            ++skipped;
            continue;
        }

        if (events.size() >= 128) {
            throw VoyemuError("TEST_LOGS_FORMAT", "Android log snapshot exceeds event limit");
        }

        Json event{{"time", m[1].str()},
                   {"clock", "android.realtime"},
                   {"pid", std::stoll(m[2].str())},
                   {"tid", std::stoll(m[3].str())},
                   {"level", m[4].str()},
                   {"tag", m[5].str()},
                   {"message_omitted", true}};

        if (details && diagnostic_tags.contains(m[5].str())) {
            auto message = m[6].str();

            for (const auto &secret : secrets) {
                if (!secret.empty()) {
                    std::size_t at = 0;

                    while ((at = message.find(secret, at)) != std::string::npos) {
                        message.replace(at, secret.size(), "[redacted]");
                        at += 10;
                    }
                }
            }

            message = std::regex_replace(message, labeled, "$1$2[redacted]");
            const bool truncated = message.size() > 2048;

            if (truncated) {
                message.resize(2048);
            }

            for (auto &ch : message) {
                const auto byte = static_cast<unsigned char>(ch);

                if (byte < 32 || byte >= 127) {
                    ch = ' ';
                }
            }

            event["message"] = message;
            event["message_omitted"] = false;
            event["message_truncated"] = truncated;
        }

        events.push_back(std::move(event));
    }

    return {{"status", skipped ? "partial" : "ready"},
            {"source", "android"},
            {"events", events},
            {"unparsed_lines", skipped},
            {"continuous", false},
            {"gap", nullptr},
            {"cursor", nullptr},
            {"coverage", "bounded recent snapshot; journal continuity unknown"},
            {"messages_omitted_for_privacy", !details},
            {"details", details},
            {"message_scope", details ? "fixed diagnostic tags only" : "metadata only"}};
}

std::string diagnostic_tombstone(const Json &operation, std::string_view source) {
    if (!operation.contains("tombstone")) {
        return {};
    }

    const auto &value = operation.at("tombstone");

    if (source != "native" || !value.is_string() ||
        !std::regex_match(value.get_ref<const std::string &>(),
                          std::regex(R"(tombstone_[0-9]{2})"))) {
        throw VoyemuError("TEST_ARGUMENT", "native tombstone must be tombstone_00..99");
    }

    return value.get<std::string>();
}

Json collect_retained_android_diagnostics(
        std::string_view source, const std::string &tombstone,
        const std::vector<std::string> &secrets,
        const std::function<std::string(std::vector<std::string>)> &read) {
    const bool native = source == "native";
    Json result;

    if (native) {
        result = {
                {"status", "ready"},
                {"source", "native"},
                {"continuous", false},
                {"cursor", nullptr},
                {"coverage",
                 "retained crash buffer and optional tombstone; not continuous; file boot association unknown"}};
        auto retained = [&](const std::string &command) {
            const auto value = read({"shell", command});

            if (value.find("VOYEMU_NATIVE_UNAVAILABLE") != std::string::npos) {
                return Json{{"status", "unavailable"},
                            {"reason", "not readable with existing SDK identity"},
                            {"source_complete", false}};
            }

            return parse_native_prefix(value, secrets);
        };
        result["crash_buffer"] = retained(
                "value=$(logcat -d -b crash -v epoch -t 512 2>/dev/null); if [ $? -eq 0 ]; then printf '%s' \"$value\" | head -c 32769; else echo VOYEMU_NATIVE_UNAVAILABLE; fi");
        result["crash_buffer"]["selection"] = "last 512 lines; full buffer completeness unknown";
        result["crash_buffer"]["selected_text_complete"] =
                result["crash_buffer"].value("source_complete", false);
        result["crash_buffer"]["source_complete"] = false;
        result["handler_diagnostics"] = parse_native_handler_diagnostics(
                read({"shell", native_handler_diagnostics_command()}), secrets);
        const auto inventory = read(
                {"shell",
                 "value=$(ls -l /data/tombstones 2>/dev/null); if [ $? -eq 0 ]; then printf '%s' \"$value\" | head -c 8192; else echo VOYEMU_NATIVE_UNAVAILABLE; fi"});

        if (inventory.find("VOYEMU_NATIVE_UNAVAILABLE") != std::string::npos) {
            result["inventory"] = {{"status", "unavailable"},
                                   {"reason", "not readable with existing SDK identity"}};
        } else {
            result["inventory"] = parse_native_inventory(inventory);
        }

        if (!tombstone.empty()) {
            const auto path = "/data/tombstones/" + tombstone;
            result["tombstone"] = retained(
                    "if [ -f " + path + " ] && [ ! -L " + path + " ] && [ -r " + path +
                    " ]; then head -c 32769 " + path +
                    " 2>/dev/null || echo VOYEMU_NATIVE_UNAVAILABLE; else echo VOYEMU_NATIVE_UNAVAILABLE; fi");
            result["tombstone"]["basename"] = tombstone;
        }
    } else {
        result = parse_anr_diagnostics(read({"shell", "dumpsys", "activity", "lastanr"}), secrets);
        Json traces;
        try {
            traces = parse_anr_trace_inventory(read({"shell", "ls", "-l", "/data/anr"}));

            if (!traces.at("selected").is_null()) {
                const auto basename = traces.at("selected").get<std::string>();
                const auto captured =
                        read({"shell", "head", "-c", "131071", "/data/anr/" + basename});
                const auto diagnostic = parse_anr_diagnostics(captured, secrets);
                traces["prefix"] = {{"excerpt", diagnostic.at("excerpt")},
                                    {"excerpt_truncated", diagnostic.at("excerpt_truncated")},
                                    {"captured_bytes", captured.size()},
                                    {"captured_sha256", diagnostic.at("source_sha256")},
                                    {"source_complete", false},
                                    {"read_limit_bytes", 131071}};
            }
        } catch (const std::exception &) {
            traces = {{"status", "unavailable"},
                      {"reason", "fixed retained ANR inventory or prefix read failed"},
                      {"boot_association", "unknown"}};
        }

        result["traces"] = traces;
    }

    return result;
}
} // namespace voyemu
