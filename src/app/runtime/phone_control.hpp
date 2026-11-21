#pragma once
#include "error.hpp"
#include "model.hpp"
#include "profile_catalog.hpp"
#include "profile_identity.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>

namespace voyemu {
// The synthetic phone a machine reaches through the phone patch its profile declares.
inline Json synthetic_phone_adapter(std::string_view profile,
                                    std::string_view kind = "synthetic-oem-bt-peer-v1") {
    return {{"schema", 1},
            {"profile", profile},
            {"authority", "com.voyemu.input.phone"},
            {"kind", kind}};
}

// Whether the profile pins this OEM phone by its SHA-256 and declares its patch with exactly these
// steps; the profile pin is the only expected SHA-256.
inline bool phone_patch_declared(const Json &definition, const std::string &name,
                                 const Json &steps) {
    if (!definition.contains("patches") || !definition.at("patches").is_object() ||
        !definition.at("patches").contains(name) || !definition.contains("original") ||
        !definition.at("original").is_object() || !definition.at("original").contains(name)) {
        return false;
    }

    const auto &patch = definition.at("patches").at(name);
    const auto &original = definition.at("original").at(name);

    return patch.is_object() && patch.value("input", Json{}) == "original:" + name &&
           patch.value("steps", Json{}) == steps && original.is_object() &&
           original.contains("sha256") && original.at("sha256").is_string() &&
           !original.at("sha256").get_ref<const std::string &>().empty();
}

inline bool phone_component_declared(const Json &definition, const std::string &name,
                                     const std::string &path) {
    return definition.contains("components") && definition.at("components").is_object() &&
           definition.at("components").value(name, Json{}) == path;
}

// The pinned OEM phone and the declared patch select the synthetic phone; the profile id does not.
inline Json phone_adapter_for_profile(const Json &definition,
                                      std::string_view verified_profile = {}) {
    // Raw snapshots may have no id field; the caller may supply the already verified runtime
    // manifest profile.
    if (!definition.is_object()) {
        return Json::object();
    }

    if (verified_profile.empty()) {
        if (!definition.contains("id") || !definition.at("id").is_string()) {
            return Json::object();
        }

        verified_profile = definition.at("id").get_ref<const std::string &>();
    }

    const auto family = runtime_profile_reference(definition, verified_profile);

    if (family.empty()) {
        return Json::object();
    }

    // The OEM Telecom phone takes a standalone phone component.
    if (phone_component_declared(definition, "phone", "components/phone") &&
        phone_patch_declared(
                definition, "LY_BtPhone",
                Json::array({"passion-api30-bluetooth-role.json", "passion-api30-phone.json"})) &&
        definition.at("patches").at("LY_BtPhone").value("type", Json{}) == "apk") {
        return synthetic_phone_adapter(family, "synthetic-telecom-v1");
    }

    // The OEM Bluetooth phones share the phone provider sources and authority.
    if (phone_patch_declared(definition, "BluetoothPhone",
                             Json::array({"api30-phone-provider.json"})) ||
        phone_patch_declared(definition, "BluetoothPhone",
                             Json::array({"systemservice-framework.json", "add-qg-runtime.json",
                                          "phone-provider.json"})) ||
        (phone_component_declared(definition, "bluetooth", "components/phone-bluetooth") &&
         phone_patch_declared(definition, "BluetoothPhone",
                              Json::array({"add-qg-runtime.json", "nxp-phone-provider.json"})))) {
        return synthetic_phone_adapter(family);
    }

    return Json::object();
}

// A launch reaches the synthetic phone that preparation bound to its own reference profile.
inline bool synthetic_phone_supported(const Json &launch) {
    if (!launch.is_object() || !launch.contains("phone_adapter") ||
        !launch.contains("reference_profile") || !launch.at("reference_profile").is_string()) {
        return false;
    }

    const auto &adapter = launch.at("phone_adapter");
    const auto &profile = launch.at("reference_profile").get_ref<const std::string &>();

    return !profile.empty() &&
           (adapter == synthetic_phone_adapter(profile) ||
            adapter == synthetic_phone_adapter(profile, "synthetic-telecom-v1"));
}

inline bool phone_input_operation(std::string_view op) {
    return op == "multimedia.phone.configure" || op == "multimedia.phone.connect" ||
           op == "multimedia.phone.disconnect" || op == "multimedia.phone.incoming" ||
           op == "multimedia.phone.answer" || op == "multimedia.phone.hangup";
}

inline bool synthetic_phone_number(const Json &value) {
    if (!value.is_string()) {
        return false;
    }

    const auto text = value.get<std::string>();

    return text.size() == 12 && text.starts_with("+7") &&
           std::all_of(text.begin() + 2, text.end(), [](unsigned char c) {
               return c >= '0' && c <= '9';
           });
}

inline void phone_keys(const Json &object, const std::set<std::string> &keys) {
    if (!object.is_object()) {
        throw VoyemuError("TEST_ARGUMENT", "phone payload must be an object");
    }

    for (const auto &item : object.items()) {
        if (!keys.contains(item.key())) {
            throw VoyemuError("TEST_ARGUMENT", "unknown phone payload field");
        }
    }
}

inline bool phone_label(const Json &value, std::size_t maximum, std::size_t maximum_units) {
    if (!value.is_string()) {
        return false;
    }

    const auto text = value.get<std::string>();

    if (text.empty() || text.size() > maximum ||
        std::any_of(text.begin(), text.end(), [](unsigned char c) {
            return c < 32 || c == 127;
        })) {
        return false;
    }

    try {
        (void)value.dump();
    } catch (const std::exception &) {
        return false;
    }

    if (std::all_of(text.begin(), text.end(), [](unsigned char c) {
            return c <= 32;
        })) {
        return false;
    }

    std::size_t units = 0;

    // dump() has already rejected malformed UTF-8; supplementary characters use two Java UTF-16
    // units.
    for (unsigned char c : text) {
        if ((c & 0xc0) != 0x80) {
            units += (c & 0xf8) == 0xf0 ? 2 : 1;
        }
    }

    return units <= maximum_units;
}

inline Json phone_command(std::string_view operation, const Json &payload) {
    if (operation == "multimedia.phone.state") {
        phone_keys(payload, {});

        return {{"method", "snapshot"}, {"payload", payload}};
    }

    if (!phone_input_operation(operation)) {
        throw VoyemuError("TEST_UNSUPPORTED", "unknown synthetic phone operation");
    }

    const auto method = std::string(operation.substr(std::string_view("multimedia.phone.").size()));

    if (method == "configure") {
        phone_keys(payload, {"contacts", "peer_name"});

        if (payload.contains("peer_name") && !phone_label(payload.at("peer_name"), 64, 64)) {
            throw VoyemuError("TEST_ARGUMENT", "bounded peer name required");
        }

        if (!payload.contains("contacts") || !payload.at("contacts").is_array() ||
            payload.at("contacts").size() != 2) {
            throw VoyemuError("TEST_ARGUMENT", "exactly two synthetic contacts required");
        }

        std::set<std::string> numbers;

        for (const auto &contact : payload.at("contacts")) {
            phone_keys(contact, {"name", "number"});

            if (!contact.contains("name") || !phone_label(contact.at("name"), 128, 80) ||
                !contact.contains("number") || !synthetic_phone_number(contact.at("number")) ||
                !numbers.insert(contact.at("number").get<std::string>()).second) {
                throw VoyemuError("TEST_ARGUMENT", "unique +7 synthetic contacts required");
            }
        }
    } else if (method == "incoming") {
        phone_keys(payload, {"number"});

        if (!payload.contains("number") || !synthetic_phone_number(payload.at("number"))) {
            throw VoyemuError("TEST_ARGUMENT", "synthetic +7 incoming number required");
        }
    } else {
        phone_keys(payload, {});
    }

    return {{"method", method}, {"payload", payload}};
}

inline std::vector<std::string>
phone_provider_arguments(const std::string &verb, const std::string &session,
                         const std::string &token, const std::string &boot, std::uint64_t sequence,
                         const std::string &method, const Json &payload, bool reset = false,
                         unsigned api = 30) {
    if (api != 28 && api != 30) {
        throw VoyemuError("PHONE_PROTOCOL", "unsupported guest content parser");
    }

    if (verb != "bootstrap" && verb != "control" && verb != "snapshot") {
        throw VoyemuError("PHONE_PROTOCOL", "closed provider verb required");
    }

    // API28 Content.Parser.parseBindValue keeps the string after the second colon verbatim.
    // API30 guest content CLI binding parser
    // (com.android.commands.content.Content$Parser.splitWithEscaping) splits each --extra binding
    // on unescaped ':' into exactly three parts and treats '\' + any character as a literal. JSON
    // payload values therefore need '\' and ':' escaped; session/token/boot/method are hex/UUID
    // vocabulary that never needs it (and PHONE_REQUEST rejects foreign shapes anyway).
    const auto binding_value = [&](const std::string &text) {
        if (api == 28) {
            return text;
        }

        std::string escaped;
        escaped.reserve(text.size());

        for (const char character : text) {
            if (character == '\\' || character == ':') {
                escaped += '\\';
            }

            escaped += character;
        }

        return escaped;
    };
    std::vector<std::string> args{"content",  "call",
                                  "--uri",    "content://com.voyemu.input.phone",
                                  "--method", verb,
                                  "--extra",  "schema:i:1",
                                  "--extra",  std::string("session:s:") + session,
                                  "--extra",  std::string("token:s:") + token,
                                  "--extra",  std::string("boot:s:") + boot,
                                  "--extra",  "sequence:l:" + std::to_string(sequence)};

    if (verb == "bootstrap") {
        args.insert(args.end(), {"--extra", std::string("reset:b:") + (reset ? "true" : "false")});
    }

    if (verb == "control") {
        args.insert(args.end(), {"--extra", std::string("method:s:") + method, "--extra",
                                 std::string("payload:s:") + binding_value(payload.dump())});
    }

    return args;
}

inline void require_phone_outcome_known(std::string_view boot, std::string_view retained_boot,
                                        bool unknown, bool mutation) {
    if (mutation && unknown && boot == retained_boot) {
        throw VoyemuError(
                "PHONE_DELIVERY_UNKNOWN",
                "prior phone action has an unknown outcome; restart Android before further mutations");
    }
}

// Passion keeps the call idle until Telecom delivers its callback; pending cancellation still
// mutates.
inline bool phone_control_telecom_pending(const Json &data) {
    if (!data.contains("diagnostics") || !data.at("diagnostics").is_object()) {
        return false;
    }

    const auto &diagnostics = data.at("diagnostics");

    if (diagnostics.value("transport", Json{}) != "synthetic-telecom") {
        return false;
    }

    if (!diagnostics.contains("pending") || !diagnostics.at("pending").is_boolean()) {
        throw VoyemuError("PHONE_PROTOCOL", "invalid synthetic Telecom pending state");
    }

    return diagnostics.at("pending").get<bool>();
}

// NXP rejects connect while Bluetooth is off before changing the peer. Other adapters omit powered.
inline void require_phone_connect_power(const Json &data) {
    if (!data.is_object()) {
        throw VoyemuError("PHONE_PROTOCOL", "incomplete synthetic peer state");
    }

    if (!data.contains("powered")) {
        return;
    }

    if (!data.at("powered").is_boolean()) {
        throw VoyemuError("PHONE_PROTOCOL", "invalid synthetic peer power state");
    }

    if (!data.at("powered").get<bool>()) {
        throw VoyemuError("PHONE_PROVIDER_REJECTED",
                          "connect requires Bluetooth power (PHONE_POWER_OFF)");
    }
}

inline bool phone_control_idempotent_noop(std::string_view method, const Json &data) {
    if (!data.is_object() || !data.contains("connected") || !data.at("connected").is_boolean() ||
        !data.contains("call") || !data.at("call").is_object() ||
        !data.at("call").contains("state") || !data.at("call").at("state").is_number_integer()) {
        throw VoyemuError("PHONE_PROTOCOL", "incomplete synthetic peer state");
    }

    const auto state = data.at("call").at("state").get<std::int64_t>();

    if (state < 128 || state > 131) {
        throw VoyemuError("PHONE_PROTOCOL", "invalid synthetic peer call state");
    }

    const bool connected = data.at("connected").get<bool>(), idle = state == 131,
               pending = phone_control_telecom_pending(data);

    return (method == "connect" && connected && idle) ||
           (!pending &&
            ((method == "disconnect" && !connected && idle) || (method == "hangup" && idle)));
}

// Contract probe for configure (PeerConfiguration.configure throws
// IllegalStateException("PHONE_CONNECTED") strictly before any durable mutation). Used by
// Session::phone_control to reject a configure on a connected/non-terminated peer locally — clean
// known outcome, no dispatch, no boot quarantine.
inline bool phone_control_configure_requires_reject(const Json &data) {
    if (!data.is_object() || !data.contains("connected") || !data.at("connected").is_boolean() ||
        !data.contains("call") || !data.at("call").is_object() ||
        !data.at("call").contains("state") || !data.at("call").at("state").is_number_integer()) {
        throw VoyemuError("PHONE_PROTOCOL", "incomplete synthetic peer state");
    }

    const auto state = data.at("call").at("state").get<std::int64_t>();

    if (state < 128 || state > 131) {
        throw VoyemuError("PHONE_PROTOCOL", "invalid synthetic peer call state");
    }

    return phone_control_telecom_pending(data) || data.at("connected").get<bool>() || state != 131;
}

// Call-path contract probe (SyntheticPhonePeer.begin/answer/hangup): every guarded rebision throws
// IllegalStateException strictly before any durable mutation, so rejecting locally keeps the
// outcome known (nothing delivered) instead of poisoning the boot as an uncertain adapter_control.
inline bool phone_control_call_requires_reject(std::string_view method, const Json &data) {
    if (method != "incoming" && method != "answer" && method != "hangup") {
        return false;
    }

    if (!data.is_object() || !data.contains("connected") || !data.at("connected").is_boolean() ||
        !data.contains("call") || !data.at("call").is_object() ||
        !data.at("call").contains("state") || !data.at("call").at("state").is_number_integer()) {
        throw VoyemuError("PHONE_PROTOCOL", "incomplete synthetic peer state");
    }

    const auto state = data.at("call").at("state").get<std::int64_t>();

    if (state < 128 || state > 131) {
        throw VoyemuError("PHONE_PROTOCOL", "invalid synthetic peer call state");
    }

    const bool connected = data.at("connected").get<bool>(),
               pending = phone_control_telecom_pending(data);

    if (!connected) {
        return true; // requireConnected() throws PHONE_DISCONNECTED first for any call mutation
    }

    if (method == "incoming") {
        return pending || state != 131; // PHONE_CALL_ACTIVE
    }

    if (method == "answer") {
        return pending || (state != 130 && state != 129); // PHONE_NOT_RINGING_OR_DIALING
    }

    return false; // live and pending Telecom calls may dispatch hangup; settled idle was handled by
                  // the no-op probe
}

template <class Exchange, class Quarantine>
inline Json phone_provider_exchange(std::string_view verb, Exchange exchange,
                                    Quarantine quarantine) {
    try {
        return exchange();
    } catch (const std::exception &error) {
        // Quarantine only for genuinely uncertain dispatches. A clean closed-vocabulary provider
        // rejection (PHONE_PROVIDER_REJECTED: mutation_started=false in the peer diagnostics) was
        // fully classified by the peer: the mutation never started, so the outcome is KNOWN (not
        // delivered) — poisoning the whole boot for it turned every recoverable "wrong state"
        // refusal (e.g. connect on an already connected peer) into a permanent host latch until a
        // reboot. Let clean refusals propagate.
        const std::string_view message = error.what();
        const bool clean_rejection = message.starts_with("PHONE_PROVIDER_REJECTED:");
        const bool mutating = verb == "bootstrap" || verb == "control";

        if (mutating && !clean_rejection) {
            quarantine();
        }

        // A malformed receipt after a dispatched mutation leaves its outcome unknown; snapshot
        // replies keep PHONE_PROTOCOL because nothing was mutated.
        if (mutating && message.starts_with("PHONE_PROTOCOL:")) {
            auto detail = std::string(message.substr(sizeof "PHONE_PROTOCOL:" - 1));

            if (detail.starts_with(' ')) {
                detail.erase(0, 1);
            }

            throw VoyemuError("PHONE_PROTOCOL_UNKNOWN", detail);
        }

        if (message.starts_with("PHONE_PROVIDER_UNKNOWN:") ||
            message.starts_with("PHONE_DELIVERY_UNKNOWN:") ||
            message.starts_with("PHONE_PROTOCOL:") ||
            message.starts_with("PHONE_PROVIDER_REJECTED:")) {
            throw;
        }

        throw VoyemuError("PHONE_PROVIDER_UNKNOWN",
                          "synthetic peer exchange failed after dispatch");
    } catch (...) {
        if (verb == "bootstrap" || verb == "control") {
            quarantine();
        }

        throw VoyemuError("PHONE_PROVIDER_UNKNOWN",
                          "synthetic peer exchange failed after dispatch");
    }
}

template <class Exchange>
inline void bootstrap_phone_session(std::string_view boot, std::string &retained_boot,
                                    std::uint64_t &sequence, bool &unknown,
                                    std::int64_t &provider_pid, Exchange exchange) {
    require_phone_outcome_known(boot, retained_boot, unknown, true);
    // A bootstrap takeover may disconnect the old peer. A missing receipt is not permission to
    // replay it.
    const auto reply = exchange();
    provider_pid = reply.at("provider_pid").template get<std::int64_t>();
    retained_boot = boot;
    sequence = 0;
    unknown = false;
}

template <class Parser>
inline Json parse_phone_provider_output(const std::string &output, Parser parse) {
    try {
        return parse(output);
    } catch (const std::exception &) {
        throw VoyemuError("PHONE_PROVIDER_UNKNOWN",
                          "synthetic peer returned an unreadable receipt");
    }
}

inline void reject_phone_provider_failure(const Json &reply) {
    if (!reply.is_object() || !reply.contains("schema") ||
        !reply.at("schema").is_number_integer() || reply.at("schema") != 1 ||
        !reply.contains("ok") || !reply.at("ok").is_boolean() || reply.at("ok") != false ||
        !reply.contains("error") || reply.at("error") != "PHONE_CONTROL_REJECTED") {
        return;
    }

    const bool diagnostic = reply.contains("stage") || reply.contains("reason") ||
                            reply.contains("mutation_started");

    if (!diagnostic) {
        throw VoyemuError("PHONE_PROVIDER_UNKNOWN",
                          "synthetic peer rejected the request without execution diagnostics");
    }

    const std::set<std::string> stages = {
            "request",        "identity",        "bootstrap",
            "adapter_lookup", "adapter_control", "adapter_precondition",
            "reply"};
    const std::set<std::string> reasons = {"PHONE_REQUEST",
                                           "PHONE_OWNER",
                                           "PHONE_SEQUENCE",
                                           "PHONE_ACTIVE_CALL",
                                           "PHONE_ADAPTER_UNAVAILABLE",
                                           "PHONE_ADAPTER_CLASS",
                                           "PHONE_ADAPTER_FIELD",
                                           "PHONE_ADAPTER_METHOD",
                                           "PHONE_ADAPTER_ACCESS",
                                           "PHONE_ADAPTER_INIT",
                                           "PHONE_ADAPTER_FRESH_PROCESS_REQUIRED",
                                           "PHONE_ADAPTER_CONTROL",
                                           "PHONE_ADAPTER_TIMEOUT",
                                           "PHONE_ADAPTER_RESPONSE",
                                           "PHONE_BOOT_IO",
                                           "PHONE_UNCERTAIN",
                                           "PHONE_INTERNAL",
                                           "PHONE_POWER_OFF"};

    if (!reply.contains("stage") || !reply.at("stage").is_string() ||
        !stages.contains(reply.at("stage").get<std::string>()) || !reply.contains("reason") ||
        !reply.at("reason").is_string() ||
        !reasons.contains(reply.at("reason").get<std::string>()) ||
        !reply.contains("mutation_started") || !reply.at("mutation_started").is_boolean()) {
        throw VoyemuError("PHONE_PROTOCOL", "malformed peer rejection diagnostics");
    }

    const auto stage = reply.at("stage").get<std::string>(),
               reason = reply.at("reason").get<std::string>();
    const bool before_mutation = reply.at("mutation_started") == false &&
                                 (stage == "request" || stage == "identity" ||
                                  stage == "bootstrap" || stage == "adapter_lookup" ||
                                  (stage == "adapter_precondition" && reason == "PHONE_POWER_OFF"));
    throw VoyemuError(before_mutation ? "PHONE_PROVIDER_REJECTED" : "PHONE_PROVIDER_UNKNOWN",
                      "synthetic peer rejected the request; stage=" + stage + "; reason=" + reason);
}

inline void validate_phone_reply(const Json &reply, std::string_view session, std::string_view boot,
                                 std::uint64_t sequence, std::string_view token,
                                 std::int64_t expected_pid = 0) {
    if (reply.dump().find(token) != std::string::npos) {
        throw VoyemuError("PHONE_PROTOCOL", "private credential in response");
    }

    reject_phone_provider_failure(reply);
    auto reject = [](const char *field, const char *reason) {
        throw VoyemuError("PHONE_PROTOCOL", std::string("synthetic peer receipt rejected; field=") +
                                                    field + "; reason=" + reason);
    };

    if (!reply.is_object()) {
        reject("object", "type");
    }

    if (!reply.contains("schema")) {
        reject("schema", "missing");
    }

    if (!reply.at("schema").is_number_integer()) {
        reject("schema", "type");
    }

    if (reply.at("schema") != 1) {
        reject("schema", "value");
    }

    if (!reply.contains("ok")) {
        reject("ok", "missing");
    }

    if (!reply.at("ok").is_boolean()) {
        reject("ok", "type");
    }

    if (reply.at("ok") != true) {
        reject("ok", "value");
    }

    if (!reply.contains("session")) {
        reject("session", "missing");
    }

    if (!reply.at("session").is_string()) {
        reject("session", "type");
    }

    if (reply.at("session") != session) {
        reject("session", "value");
    }

    if (!reply.contains("boot_id")) {
        reject("boot_id", "missing");
    }

    if (!reply.at("boot_id").is_string()) {
        reject("boot_id", "type");
    }

    if (reply.at("boot_id") != boot) {
        reject("boot_id", "value");
    }

    if (!reply.contains("sequence")) {
        reject("sequence", "missing");
    }

    if (!reply.at("sequence").is_number_unsigned()) {
        reject("sequence", "type");
    }

    if (reply.at("sequence") != sequence) {
        reject("sequence", "value");
    }

    if (!reply.contains("provider_pid")) {
        reject("provider_pid", "missing");
    }

    if (!reply.at("provider_pid").is_number_integer()) {
        reject("provider_pid", "type");
    }

    if (reply.at("provider_pid").get<std::int64_t>() <= 0 ||
        (expected_pid > 0 && reply.at("provider_pid") != expected_pid)) {
        reject("provider_pid", "value");
    }

    if (!reply.contains("data")) {
        reject("data", "missing");
    }

    if (!reply.at("data").is_object()) {
        reject("data", "type");
    }
}
} // namespace voyemu
