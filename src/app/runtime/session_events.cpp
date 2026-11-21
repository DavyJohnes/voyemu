#include "error.hpp"
#include "session_internal.hpp"

namespace voyemu {
using namespace session_detail;

Json EventQueue::routes() {
    Json output = Json::object();

    for (const auto &route : physical_routes) {
        output[route.id] = {{"qnx_index", route.index ? Json(route.index) : Json(nullptr)},
                            {"android", "unrecovered"}};
    }

    return output;
}

WheelEvent EventQueue::submit(const std::string &id, const std::string &phase,
                              const std::string &owner) {
    owner_valid(owner);
    auto index = route_index(id);

    if (phase != "down" && phase != "up" && phase != "cancel") {
        throw VoyemuError("WHEEL_PHASE", "down/up/cancel required");
    }

    auto held = held_.find(id);

    if (phase == "down" && held != held_.end()) {
        throw VoyemuError("WHEEL_HELD", "button already held");
    }

    if (phase != "down" && (held == held_.end() || held->second.owner != owner)) {
        throw VoyemuError("WHEEL_OWNER", "release owner mismatch");
    }

    // Reserve one release for every held button before accepting additional downs.
    if ((phase == "down" ? queue_.size() + held_.size() + 2 : queue_.size() + 1) > 512 ||
        sequence_ == UINT32_MAX) {
        throw VoyemuError("WHEEL_QUEUE", "bounded queue full");
    }

    if (phase == "down") {
        held_[id] = {owner, std::chrono::steady_clock::now()};
    } else {
        held_.erase(id);
    }

    WheelEvent event{++sequence_, id, phase, owner, index, phase == "down" ? 1 : 0};
    queue_.push_back(event);

    return event;
}

void EventQueue::release(const std::string &owner) {
    owner_valid(owner);
    std::vector<std::string> ids;

    for (const auto &[id, held] : held_) {
        if (held.owner == owner) {
            ids.push_back(id);
        }
    }

    for (const auto &id : ids) {
        submit(id, "cancel", owner);
    }
}

void EventQueue::release_all() {
    std::set<std::string> owners;

    for (const auto &[id, held] : held_) {
        owners.insert(held.owner);
    }

    for (const auto &owner : owners) {
        release(owner);
    }
}

WheelEvent EventQueue::force_release(const std::string &id, const std::string &owner) {
    owner_valid(owner);
    auto index = route_index(id);

    if (sequence_ == UINT32_MAX) {
        throw VoyemuError("WHEEL_SEQUENCE", "shutdown release exhausted");
    }

    auto held = held_.find(id);

    if (held != held_.end() && held->second.owner != owner) {
        throw VoyemuError("WHEEL_OWNER", "shutdown owner mismatch");
    }

    held_.erase(id);

    return {++sequence_, id, "cancel", owner, index, 0};
}

void EventQueue::renew(const std::string &owner) {
    owner_valid(owner);

    for (auto &[id, held] : held_) {
        if (held.owner == owner) {
            held.renewed = std::chrono::steady_clock::now();
        }
    }
}

void EventQueue::expire() {
    auto now = std::chrono::steady_clock::now();
    std::set<std::string> owners;

    for (const auto &[id, held] : held_) {
        if (now - held.renewed > std::chrono::seconds(5)) {
            owners.insert(held.owner);
        }
    }

    for (const auto &owner : owners) {
        release(owner);
    }
}

std::optional<WheelEvent> EventQueue::pop() {
    if (queue_.empty()) {
        return std::nullopt;
    }

    auto event = queue_.front();
    queue_.pop_front();

    return event;
}

int binder_integer(const std::string &reply) {
    std::smatch match;
    static const std::regex expression(
            R"(Result:\s*Parcel\(\s*([0-9a-fA-F]{8})\s+([0-9a-fA-F]{8})\s)");

    if (!std::regex_search(reply, match, expression) ||
        std::stoul(match[1].str(), nullptr, 16) != 0) {
        throw VoyemuError("BINDER_REPLY", "missing or exception");
    }

    return static_cast<std::int32_t>(std::stoul(match[2].str(), nullptr, 16));
}

Json parse_android_provider_reply(const std::string &reply) {
    if (reply.size() > 128 * 1024) {
        throw VoyemuError("ANDROID_PROVIDER_REPLY", "oversized output");
    }

    auto bundle = reply.find("Bundle[{");
    auto marker = bundle == std::string::npos ? std::string::npos : reply.find("json=", bundle + 8);

    if (marker == std::string::npos) {
        auto error = bundle == std::string::npos ? std::string::npos
                                                 : reply.find("error=", bundle + 8);

        if (error != std::string::npos) {
            auto end = reply.find("}]", error);
            auto reason = end == std::string::npos ? std::string{}
                                                   : reply.substr(error + 6, end - error - 6);

            if (reason == "authentication" || reason == "invalid-command" ||
                reason == "storage-or-delivery" || reason == "storage-or-submission") {
                throw VoyemuError("ANDROID_PROVIDER_ERROR", reason);
            }
        }

        throw VoyemuError("ANDROID_PROVIDER_REPLY", "no JSON field");
    }

    auto start = marker + 5;

    while (start < reply.size() && std::isspace(static_cast<unsigned char>(reply[start]))) {
        ++start;
    }

    if (start >= reply.size() || reply[start] != '{') {
        throw VoyemuError("ANDROID_PROVIDER_REPLY", "object required");
    }

    unsigned depth = 0;
    bool quoted = false, escaped = false;

    for (auto at = start; at < reply.size(); ++at) {
        char c = reply[at];

        if (quoted) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                quoted = false;
            }

            continue;
        }

        if (c == '"') {
            quoted = true;
            continue;
        }

        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (!depth) {
                throw VoyemuError("ANDROID_PROVIDER_REPLY", "unmatched brace");
            }

            if (--depth == 0) {
                auto result = Json::parse(reply.substr(start, at - start + 1));
                auto tail = reply.substr(at + 1);

                while (!tail.empty() && std::isspace(static_cast<unsigned char>(tail.back()))) {
                    tail.pop_back();
                }

                if (tail != "}]") {
                    throw VoyemuError("ANDROID_PROVIDER_REPLY", "unexpected trailing data");
                }

                return result;
            }
        }
    }

    throw VoyemuError("ANDROID_PROVIDER_REPLY", "incomplete object");
}

bool android_vehicle_receipt_matches(const Json &receipt, const Json &state) {
    try {
        validate_state(state);

        if (!receipt.is_object() || receipt.value("schema", 0) != 1 ||
            receipt.value("status", std::string{}) != "applied" ||
            receipt.value("map_sha256", std::string{}) !=
                    catalog_map_sha256(state_catalog(state)) ||
            receipt.value("field_count", 0U) != state.at("raw_oem").size() ||
            receipt.at("sequence") != state.at("sequence")) {
            return false;
        }

        if (!receipt.at("schema").is_number_integer() ||
            !receipt.at("field_count").is_number_integer()) {
            return false;
        }

        auto check = state;
        check["sequence"] = receipt.at("sequence");
        check["raw_oem"] = receipt.at("raw_oem");
        validate_state(check);

        return check.at("raw_oem") == state.at("raw_oem");
    } catch (const std::exception &) {
        return false;
    }
}

bool android_vehicle_receipt_pending(const Json &receipt, const Json &state) {
    try {
        validate_state(state);

        if (receipt.is_object() && receipt.size() == 2 &&
            receipt.at("schema").is_number_integer() && receipt.at("schema") == 1 &&
            receipt.at("status") == "not-ready") {
            return true;
        }

        if (!receipt.is_object() || !receipt.contains("sequence") ||
            !receipt.at("sequence").is_number_integer() ||
            receipt.at("sequence") >= state.at("sequence")) {
            return false;
        }

        auto observed = state;
        observed["sequence"] = receipt.at("sequence");
        observed["raw_oem"] = receipt.at("raw_oem");

        return android_vehicle_receipt_matches(receipt, observed);
    } catch (const std::exception &) {
        return false;
    }
}

bool android_wheel_release_reuses_identity(const WheelEvent &down, const WheelEvent &release) {
    return down.phase == "down" && (release.phase == "up" || release.phase == "cancel") &&
           down.id == release.id && down.owner == release.owner &&
           release.sequence == down.sequence + 1;
}

std::string android_short_wheel_pair_command(const std::string &session, const std::string &token,
                                             const std::string &boot, const WheelEvent &down,
                                             const WheelEvent &up, int hold_ms,
                                             AndroidShortWheelContract contract,
                                             std::string_view nonce) {
    if (hold_ms < 1 || hold_ms > 200 || down.phase != "down" || up.phase != "up" ||
        up.sequence != down.sequence + 1 || down.id != up.id || down.owner != up.owner ||
        (down.id != "volume_up" && down.id != "volume_down")) {
        throw VoyemuError("WHEEL_PAIR", "exact owned short pair required");
    }

    auto command = [&](const WheelEvent &event) {
        std::vector<std::string> args{"content",  "call",
                                      "--uri",    "content://com.voyemu.input.wheel",
                                      "--method", "input"};

        for (const auto &extra : std::vector<std::string>{
                     "schema:i:1", "session:s:" + session, "token:s:" + token,
                     "sequence:l:" + std::to_string(event.sequence), "button:s:" + event.id,
                     "owner:s:" + event.owner, "phase:s:" + event.phase}) {
            args.push_back("--extra");
            args.push_back(extra);
        }

        std::string result;

        for (const auto &arg : args) {
            if (!result.empty()) {
                result += ' ';
            }

            result += android_shell_quote(arg);
        }

        return result;
    };

    if (contract == AndroidShortWheelContract::qinggan09) {
        if (!android_boot_id_valid(boot) || !windows_startup_token(nonce)) {
            throw VoyemuError("WHEEL_PAIR", "fixed A9 boot and nonce required");
        }

        const auto release_guard =
                "IFS= read -r pair_boot < /proc/sys/kernel/random/boot_id && [ \"$pair_boot\" = " +
                android_shell_quote(boot) + " ] && [ \"$(getprop qemu.voyemu.token)\" = " +
                android_shell_quote(std::string(nonce)) + " ] || exit 79; ";

        return "down_status=0; down=$(" + command(down) +
               ") || down_status=$?; "
               "printf '%s\\n' VOYEMU_DOWN \"$down\"; "
               "hold_status=0; sleep " +
               std::to_string(hold_ms / 1000.0) + " || hold_status=$?; " + release_guard +
               "up_status=0; up=$(" + command(up) +
               ") || up_status=$?; "
               "printf '%s\\n' VOYEMU_UP \"$up\"; "
               "[ \"$down_status\" = 0 ] && [ \"$hold_status\" = 0 ] && [ \"$up_status\" = 0 ]";
    }

    if (contract != AndroidShortWheelContract::qinggan11) {
        throw VoyemuError("WHEEL_PAIR", "known receipt contract required");
    }

    // This exact pinned provider envelope authorizes UP on the guest, before any host round trip.
    nlohmann::ordered_json receipt = {
            {"schema", 1},
            {"session_id", session},
            {"boot_id", boot},
            {"sequence", down.sequence},
            {"button", down.id},
            {"phase", "down"},
            {"status", "terminal-requested"},
            {"oem_handled", nullptr},
            {"visible_effect_verified", false},
            {"submission_confirmed", true},
            {"key_code", down.id == "volume_up" ? 2 : 1},
            {"action", 4096},
            {"flags", 0},
            {"scope", "OEM transaction7 completed; callbacks/effects not inferred"}};
    const auto raw = receipt.dump();
    std::string escaped;

    for (char c : raw) {
        if (c == '/') {
            escaped += '\\';
        }

        escaped += c;
    }

    const auto expected = "Result: Bundle[{json=" + raw + "}]",
               alternate = "Result: Bundle[{json=" + escaped + "}]";

    return "down=$(" + command(down) +
           ") || exit 21\n"
           "printf '%s\\n' VOYEMU_DOWN \"$down\"\n"
           "case \"$down\" in " +
           android_shell_quote(expected) + "|" + android_shell_quote(alternate) +
           ") ;; *) exit 24;; esac\n"
           "sleep " +
           std::to_string(hold_ms / 1000.0) +
           " || exit 25\n"
           "up=$(" +
           command(up) +
           ") || exit 26\n"
           "printf '%s\\n' VOYEMU_UP \"$up\"";
}

std::optional<Json> android_wheel_delivery_receipt(const Json &receipt,
                                                   const std::string &session_id,
                                                   const WheelEvent &event) {
    try {
        if (!receipt.is_object() || !receipt.at("schema").is_number_integer() ||
            receipt.at("schema") != 1 || !receipt.at("sequence").is_number_integer() ||
            receipt.at("sequence") != event.sequence ||
            receipt.at("session_id").get<std::string>() != session_id ||
            receipt.at("button").get<std::string>() != event.id ||
            receipt.at("phase").get<std::string>() != event.phase) {
            return std::nullopt;
        }

        // The guest committed this phase before dispatch. A terminal failure is final,
        // so retrying its sequence can only block the following release and buttons.
        static const std::set<std::string> statuses{"callbacks", "terminal-requested",
                                                    "no-consumer", "restricted",
                                                    "terminal-unavailable"};
        const auto status = receipt.at("status").get<std::string>();

        if (!statuses.contains(status)) {
            return std::nullopt;
        }

        return Json{{"event_sequence", event.sequence},
                    {"button", event.id},
                    {"status", status},
                    {"guest_acknowledged", true},
                    {"receipt", receipt},
                    {"visible_effect_verified", false},
                    {"transport", "DUMP-protected provider with actual Binder UID guard"}};
    } catch (const Json::exception &) {
        return std::nullopt;
    }
}

static bool qinggan11_skin_valid(int skin) {
    return skin == 1 || skin == 1001 || skin == 3001 || skin == 3002 || skin == 3003;
}

std::optional<ThemeState> qinggan11_cluster_skin_theme(std::string_view reply) {
    int skin = 0;

    try {
        skin = binder_integer(std::string(reply));
    } catch (const std::exception &) {
        return std::nullopt;
    }

    if (!qinggan11_skin_valid(skin)) {
        return std::nullopt;
    }

    return ThemeState{(skin + 999) / 1000, skin};
}

// A guest that sets the qinggan11 theme through the OEM mode and skin setters reads theme_skin, any
// other guest reads theme_id.
std::optional<Json> qinggan11_theme_change(std::optional<ThemeState> previous, ThemeState current) {
    if (!qinggan11_skin_valid(current.skin) || current.mode != (current.skin + 999) / 1000) {
        throw VoyemuError("QINGGAN11_THEME", "exact OEM mode and skin required");
    }

    if (previous && *previous == current) {
        return std::nullopt;
    }

    return Json{{"theme_id", current.mode},
                {"theme_skin", current.skin},
                {"source", "qg.cluster.service/SKIN"}};
}

// qg.canbus transaction 57 reads one IPK property by its ordinal and value id; qg.cluster.service
// transaction 7 reads the cluster skin.
std::vector<std::vector<std::string>> oem_theme_queries(const Json &android) {
    if (qinggan11_presentation(android)) {
        return {{"shell", "service", "call", "qg.cluster.service", "7", "i32", "1", "i32", "27",
                 "i32", "28"}};
    }

    return {{"shell", "service", "call", "qg.canbus", "57", "i32", "1", "i32", "599", "i32", "818"},
            {"shell", "service", "call", "qg.canbus", "57", "i32", "1", "i32", "978", "i32",
             "1195"}};
}

std::optional<ThemeState> oem_theme_state(const Json &android,
                                          const std::vector<std::string> &replies) {
    if (qinggan11_presentation(android)) {
        return qinggan11_cluster_skin_theme(replies.at(0));
    }

    return ThemeState{binder_integer(replies.at(0)), binder_integer(replies.at(1))};
}

std::optional<Json> oem_theme_command(const Json &android, std::optional<ThemeState> previous,
                                      ThemeState current) {
    return qinggan11_presentation(android) ? qinggan11_theme_change(previous, current)
                                           : theme_change(previous, current);
}

std::optional<Json> theme_change(std::optional<ThemeState> previous, ThemeState current) {
    if (current.mode < 1 || current.mode > 6 || current.skin < 1 || current.skin > 6000) {
        throw VoyemuError("THEME_VALUE", "unknown OEM value");
    }

    int group = (current.skin + 999) / 1000;

    if (!previous) {
        if (group != 1 && group != 2 && group != 4) {
            return std::nullopt;
        }

        return Json{{"theme_id", group}, {"source", "IPK_THEME_SKIN"}, {"value", current.skin}};
    }

    if (current.skin != previous->skin) {
        return Json{{"theme_id", group}, {"source", "IPK_THEME_SKIN"}, {"value", current.skin}};
    }

    if (current.mode != previous->mode) {
        return Json{
                {"theme_id", current.mode}, {"source", "IPK_THEM_MODE"}, {"value", current.mode}};
    }

    return std::nullopt;
}

std::optional<Json> find_qnx_theme_receipt(const std::filesystem::path &directory,
                                           const std::vector<std::uint64_t> &producers,
                                           std::uint32_t sequence, unsigned theme, unsigned skin) {
    if (!sequence || theme < 1 || theme > 6 ||
        (skin && (!qinggan11_skin_valid(skin) || theme != (skin + 999) / 1000))) {
        return std::nullopt;
    }

    std::optional<Json> observed;

    for (auto address : producers) {
        std::ostringstream name;
        name << "producer-" << std::hex << address;
        const auto producer = directory / name.str();
        auto matches = [&](const Json &value) {
            return value.is_object() && value.contains("schema") &&
                   value["schema"].is_number_integer() && value["schema"] == 1 &&
                   value.contains("sequence") && value["sequence"].is_number_unsigned() &&
                   value["sequence"].get<std::uint64_t>() == sequence &&
                   value.contains("producer_address") &&
                   value["producer_address"].is_number_unsigned() &&
                   value["producer_address"].get<std::uint64_t>() == address;
        };
        auto exact611 = [&](const Json &value) {
            auto field = [&](const char *key, unsigned expected) {
                return value.contains(key) && value.at(key).is_number_unsigned() &&
                       value.at(key).get<std::uint64_t>() == expected;
            };

            return value.value("contract", std::string{}) == "qinggan11-611-theme-v1" &&
                   field("theme_id", theme) && field("theme_skin", skin);
        };
        auto getters611 = [&](const Json &value) {
            return value.contains("observed_requested_theme") &&
                   value.at("observed_requested_theme").is_number_unsigned() &&
                   value.at("observed_requested_theme").get<std::uint64_t>() == theme &&
                   value.contains("observed_skin") &&
                   value.at("observed_skin").is_number_unsigned() &&
                   value.at("observed_skin").get<std::uint64_t>() == skin;
        };
        const auto ack_path = producer / "theme-ack.json";

        if (std::filesystem::exists(ack_path)) {
            const auto ack = read_private_json(ack_path, 4096);

            if (matches(ack) && ack.contains("guest_acknowledged") &&
                ack["guest_acknowledged"].is_boolean() && ack["guest_acknowledged"].get<bool>() &&
                (!skin || (exact611(ack) && getters611(ack)))) {
                return Json{{"status", "guest_acknowledged"},
                            {"guest_acknowledged", true},
                            {"producer_address", address},
                            {"receipt_source", "original-setting-request-ACK"},
                            {"visible_effect_verified", false}};
            }
        }

        const auto path = producer / "theme-observed.json";

        if (std::filesystem::exists(path)) {
            const auto value = read_private_json(path, 4096);

            if (skin) {
                if (matches(value) && exact611(value) &&
                    value.value("source", std::string{}) ==
                            "original-requested-theme-and-skin-getters") {
                    const auto outcome = value.value("native_outcome", std::string("none"));

                    if (value.value("native_attempt_sequence", 0ULL) == sequence &&
                        (outcome == "mode_ipc_failed" || outcome == "skin_ipc_failed" ||
                         outcome == "getter_mismatch" || outcome == "request_entry_missing")) {
                        observed = Json{{"status", "partial_failure"},
                                        {"native_outcome", outcome},
                                        {"guest_acknowledged", false},
                                        {"producer_address", address},
                                        {"receipt_source", "original-setting-request-outcome"},
                                        {"visible_effect_verified", false}};
                    } else if (value.value("request_suppressed", false) && getters611(value)) {
                        observed = Json{
                                {"status", "already_selected"},
                                {"guest_acknowledged", false},
                                {"producer_address", address},
                                {"receipt_source", "original-requested-theme-and-skin-getters"},
                                {"visible_effect_verified", false}};
                    }
                }

                continue;
            }

            if (matches(value) &&
                value.value("source", std::string{}) == "original-setting-getter" &&
                value.contains("request_suppressed") && value["request_suppressed"].is_boolean() &&
                value["request_suppressed"].get<bool>() && value.contains("theme_id") &&
                value["theme_id"].is_number_unsigned() &&
                value["theme_id"].get<std::uint64_t>() == theme &&
                value.contains("observed_display_theme") &&
                value["observed_display_theme"].is_number_unsigned() &&
                qnx_theme_display_matches(theme,
                                          value["observed_display_theme"].get<std::uint64_t>())) {
                observed = Json{{"status", "already_selected"},
                                {"guest_acknowledged", false},
                                {"producer_address", address},
                                {"receipt_source", "original-setting-getter"},
                                {"visible_effect_verified", false}};
            }
        }
    }

    return observed;
}

std::optional<Json> find_qnx_language_receipt(const std::filesystem::path &directory,
                                              const std::vector<std::uint64_t> &producers,
                                              std::uint32_t sequence, unsigned language) {
    if (!sequence || language > 1) {
        return std::nullopt;
    }

    std::optional<Json> observed;

    for (auto address : producers) {
        std::ostringstream name;
        name << "producer-" << std::hex << address;
        const auto producer = directory / name.str();
        auto matches = [&](const Json &value) {
            return value.is_object() && value.value("schema", 0) == 1 &&
                   value.contains("sequence") && value["sequence"].is_number_unsigned() &&
                   value["sequence"].get<std::uint64_t>() == sequence &&
                   value.contains("producer_address") &&
                   value["producer_address"].is_number_unsigned() &&
                   value["producer_address"].get<std::uint64_t>() == address;
        };
        const auto ack_path = producer / "language-ack.json";

        if (std::filesystem::exists(ack_path)) {
            const auto ack = read_private_json(ack_path, 4096);

            // A submission is never the observed effect. The ack carries the commanded
            // language; only an OEM getter observation may claim the applied value.
            if (matches(ack) && ack.value("language", 99u) == language &&
                ack.value("native_request_issued", false)) {
                if (ack.contains("observed_language") &&
                    ack["observed_language"].is_number_unsigned() &&
                    ack["observed_language"].get<std::uint64_t>() == language) {
                    return Json{{"status", "guest_acknowledged"},
                                {"guest_acknowledged", true},
                                {"producer_address", address},
                                {"receipt_source", "original-setting-request-ACK"},
                                {"visible_effect_verified", true}};
                }

                observed = Json{{"status", "request_issued"},
                                {"guest_acknowledged", false},
                                {"producer_address", address},
                                {"receipt_source", "original-setting-request"},
                                {"visible_effect_verified", false}};
            }
        }
    }

    return observed;
}

std::optional<Json> qnx_vehicle_receipt(const Json &ack, const QnxVehicleReceiver &receiver,
                                        std::uint32_t sequence) {
    auto field = [&](const char *key, std::uint64_t expected) {
        return ack.contains(key) && ack.at(key).is_number_unsigned() &&
               ack.at(key).get<std::uint64_t>() == expected;
    };

    if (!sequence || !ack.is_object() || !field("schema", 1) ||
        !ack.contains("guest_acknowledged") || ack.at("guest_acknowledged") != true ||
        !field("sequence", sequence) || !field("producer_address", receiver.producer_address) ||
        !field("generation", receiver.generation) ||
        !field("worker_nonce", receiver.worker_nonce) || !field("guest_pid", receiver.guest_pid) ||
        !field("producer_incarnation", receiver.producer_incarnation)) {
        return std::nullopt;
    }

    return ack;
}
} // namespace voyemu
