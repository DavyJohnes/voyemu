#include "phone_control.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;

// A synthetic profile that declares the patch of one pinned OEM Bluetooth phone.
static Json bluetooth_profile(std::string_view id, std::string_view sha256, const Json &steps) {
    return {{"id", id},
            {"original", {{"BluetoothPhone", {{"sha256", sha256}}}}},
            {"patches",
             {{"BluetoothPhone", {{"input", "original:BluetoothPhone"}, {"steps", steps}}}}}};
}

int main() try {
    const std::string id = "synthetic-qinggan09-a";
    const auto prepared = bluetooth_profile(
            id, "d5a7417be79e2e4bfd70e643df85ae76e1a8a15332a345f126ccc5781f5ba8a3",
            Json::array({"systemservice-framework.json", "add-qg-runtime.json",
                         "phone-provider.json"}));
    const auto capability = phone_adapter_for_profile(prepared);

    assert(capability == synthetic_phone_adapter(id));
    assert(capability == (Json{{"schema", 1},
                               {"profile", id},
                               {"authority", "com.voyemu.input.phone"},
                               {"kind", "synthetic-oem-bt-peer-v1"}}));
    {
        auto raw = prepared;
        raw.erase("id");
        assert(phone_adapter_for_profile(raw).empty());
        assert(phone_adapter_for_profile(raw, id) == synthetic_phone_adapter(id));
        assert(runtime_profile_reference(raw, id) == id);
    }

    // The declared patch and its pinned original decide; any profile id takes the same phone.
    for (const char *other : {"synthetic-qinggan11-b", "synthetic-passion-c"}) {
        auto renamed = prepared;
        renamed["id"] = other;
        assert(phone_adapter_for_profile(renamed) == synthetic_phone_adapter(other));
    }

    {
        auto copy = prepared;
        copy["id"] = "independent-local-copy";
        copy["capture"] = {{"profile", "independent-local-copy"}, {"referenceProfile", id}};
        assert(runtime_profile_reference(copy, "independent-local-copy") == id);
        assert(phone_adapter_for_profile(copy, "independent-local-copy") ==
               synthetic_phone_adapter(id));
        assert(phone_adapter_for_profile(copy, id).empty());
        copy["capture"]["profile"] = "different-copy";
        assert(phone_adapter_for_profile(copy, "independent-local-copy").empty());
    }
    {
        auto old = prepared;
        old["patches"]["BluetoothPhone"]["steps"].erase(2);
        assert(phone_adapter_for_profile(old).empty());
    }
    // The profile pin of the original is the only expected SHA-256.
    {
        auto changed = prepared;
        changed["original"]["BluetoothPhone"]["sha256"] = std::string(64, 'a');
        assert(phone_adapter_for_profile(changed) == synthetic_phone_adapter(id));
    }

    for (const auto &pin : Json::array({nullptr, 123, ""})) {
        auto unpinned = prepared;
        unpinned["original"]["BluetoothPhone"]["sha256"] = pin;
        assert(phone_adapter_for_profile(unpinned).empty());
        unpinned["original"]["BluetoothPhone"].erase("sha256");
        assert(phone_adapter_for_profile(unpinned).empty());
    }

    {
        auto bad = prepared;
        bad["patches"]["BluetoothPhone"]["input"] = "original:Foreign";
        assert(phone_adapter_for_profile(bad).empty());
    }

    // The NXP phone also requires its own Bluetooth component.
    {
        auto nxp = bluetooth_profile(
                "synthetic-nxp", "55e7602d15f5d677a5ddeaaa44c0c04c355235c5322b51edefa3a6ef29636451",
                Json::array({"add-qg-runtime.json", "nxp-phone-provider.json"}));
        nxp["components"] = {{"bluetooth", "components/phone-bluetooth"}};
        assert(phone_adapter_for_profile(nxp) == synthetic_phone_adapter("synthetic-nxp"));
        auto wrong = nxp;
        wrong["components"]["bluetooth"] = "components/bluetooth";
        assert(phone_adapter_for_profile(wrong).empty());
        wrong = nxp;
        wrong.erase("components");
        assert(phone_adapter_for_profile(wrong).empty());
        wrong = nxp;
        wrong["original"]["BluetoothPhone"].erase("sha256");
        assert(phone_adapter_for_profile(wrong).empty());
        wrong = nxp;
        wrong["patches"]["BluetoothPhone"]["steps"] = Json::array({"phone-provider.json"});
        assert(phone_adapter_for_profile(wrong).empty());
    }

    // Every API30 Bluetooth phone takes the single provider step, whatever original it pins.
    for (const auto &sha256 : {std::string(64, 'a'), std::string(64, 'b')}) {
        const auto api30 = bluetooth_profile("synthetic-qinggan11-a", sha256,
                                             Json::array({"api30-phone-provider.json"}));
        assert(phone_adapter_for_profile(api30) ==
               synthetic_phone_adapter("synthetic-qinggan11-a"));
        auto bad = api30;
        bad["patches"]["BluetoothPhone"]["steps"].push_back("extra.json");
        assert(phone_adapter_for_profile(bad).empty());
        bad = api30;
        bad["patches"]["BluetoothPhone"]["steps"][0] = "phone-provider.json";
        assert(phone_adapter_for_profile(bad).empty());
        bad = api30;
        bad["original"]["BluetoothPhone"].erase("sha256");
        assert(phone_adapter_for_profile(bad).empty());
    }

    // The Telecom phone takes the standalone phone component and the exact OEM injection contract.
    const std::string passion_id = "synthetic-passion-a";
    const Json passion{
            {"id", passion_id},
            {"components", {{"phone", "components/phone"}}},
            {"original",
             {{"LY_BtPhone",
               {{"sha256", "5bc0f97b94009b2e5aa6cb9759ea2547ab17cdadfbb8021d5e9cb6d6c6589047"}}}}},
            {"patches",
             {{"LY_BtPhone",
               {{"input", "original:LY_BtPhone"},
                {"type", "apk"},
                {"steps", Json::array({"passion-api30-bluetooth-role.json",
                                       "passion-api30-phone.json"})}}}}}};
    const Json passion_capability{{"schema", 1},
                                  {"profile", passion_id},
                                  {"authority", "com.voyemu.input.phone"},
                                  {"kind", "synthetic-telecom-v1"}};
    assert(phone_adapter_for_profile(passion) == passion_capability);
    assert(phone_adapter_for_profile(passion, passion_id) == passion_capability);
    {
        auto raw = passion;
        raw.erase("id");
        assert(phone_adapter_for_profile(raw).empty());
        assert(phone_adapter_for_profile(raw, passion_id) == passion_capability);
    }
    {
        auto bad = passion;
        bad["capture"] = {{"profile", "different-copy"}, {"referenceProfile", passion_id}};
        assert(phone_adapter_for_profile(bad, passion_id).empty());
    }

    for (const char *field : {"components", "patches", "original"}) {
        auto bad = passion;
        bad.erase(field);
        assert(phone_adapter_for_profile(bad).empty());

        for (const auto &malformed : Json::array({nullptr, 123, "foreign", Json::array()})) {
            bad = passion;
            bad[field] = malformed;
            assert(phone_adapter_for_profile(bad).empty());
        }
    }

    {
        auto bad = passion;
        bad["components"].erase("phone");
        assert(phone_adapter_for_profile(bad).empty());
    }

    for (const auto &component : Json::array(
                 {nullptr, 123, Json::object(), "components/foreign", "components/bluetooth"})) {
        auto bad = passion;
        bad["components"]["phone"] = component;
        assert(phone_adapter_for_profile(bad).empty());
    }

    {
        auto bad = passion;
        bad["patches"].erase("LY_BtPhone");
        assert(phone_adapter_for_profile(bad).empty());
    }
    {
        auto bad = passion;
        bad["original"].erase("LY_BtPhone");
        assert(phone_adapter_for_profile(bad).empty());
    }

    for (const auto &malformed : Json::array({nullptr, 123, "foreign", Json::array()})) {
        auto bad = passion;
        bad["patches"]["LY_BtPhone"] = malformed;
        assert(phone_adapter_for_profile(bad).empty());
        bad = passion;
        bad["original"]["LY_BtPhone"] = malformed;
        assert(phone_adapter_for_profile(bad).empty());
    }

    for (const char *field : {"input", "type", "steps"}) {
        auto bad = passion;
        bad["patches"]["LY_BtPhone"].erase(field);
        assert(phone_adapter_for_profile(bad).empty());
    }

    {
        auto bad = passion;
        bad["patches"]["LY_BtPhone"]["input"] = "original:BluetoothPhone";
        assert(phone_adapter_for_profile(bad).empty());
    }
    {
        auto bad = passion;
        bad["patches"]["LY_BtPhone"]["type"] = "jar";
        assert(phone_adapter_for_profile(bad).empty());
    }

    for (const auto &steps : Json::array(
                 {nullptr, "passion-api30-phone.json",
                  Json::array({"passion-api30-bluetooth-role.json"}),
                  Json::array({"passion-api30-phone.json", "passion-api30-bluetooth-role.json"}),
                  Json::array({"passion-api30-bluetooth-role.json", "foreign.json"}),
                  Json::array({"passion-api30-bluetooth-role.json", "passion-api30-phone.json",
                               "extra.json"})})) {
        auto bad = passion;
        bad["patches"]["LY_BtPhone"]["steps"] = steps;
        assert(phone_adapter_for_profile(bad).empty());
    }

    {
        auto bad = passion;
        bad["original"]["LY_BtPhone"].erase("sha256");
        assert(phone_adapter_for_profile(bad).empty());
    }

    for (const auto &pin : Json::array({nullptr, 123, ""})) {
        auto bad = passion;
        bad["original"]["LY_BtPhone"]["sha256"] = pin;
        assert(phone_adapter_for_profile(bad).empty());
    }

    {
        auto pinned = passion;
        pinned["original"]["LY_BtPhone"]["sha256"] = std::string(64, 'a');
        assert(phone_adapter_for_profile(pinned) == passion_capability);
    }

    // A launch reaches the phone bound to its own reference profile.
    const Json launch{{"reference_profile", id}, {"phone_adapter", capability}};
    assert(synthetic_phone_supported(launch));

    for (const auto path : {"/moved/machines/renamed", "/moved/machines/local-copy"}) {
        auto moved = launch;
        moved["machine"] = path;
        assert(synthetic_phone_supported(moved));
    }

    assert(synthetic_phone_supported(
            {{"reference_profile", passion_id}, {"phone_adapter", passion_capability}}));
    assert(!synthetic_phone_supported(Json::object()));
    assert(!synthetic_phone_supported({{"phone_adapter", capability}}));
    assert(!synthetic_phone_supported(
            {{"reference_profile", "synthetic-other"}, {"phone_adapter", capability}}));
    assert(!synthetic_phone_supported(
            {{"reference_profile", ""}, {"phone_adapter", synthetic_phone_adapter("")}}));
    assert(!synthetic_phone_supported({{"reference_profile", 1}, {"phone_adapter", capability}}));

    for (const char *field : {"schema", "profile", "authority", "kind"}) {
        auto bad = launch;
        bad["phone_adapter"].erase(field);
        assert(!synthetic_phone_supported(bad));
    }

    for (const auto &[field, value] : std::initializer_list<std::pair<const char *, Json>>{
                 {"schema", 2}, {"authority", "foreign"}, {"kind", "foreign-v2"}}) {
        auto bad = launch;
        bad["phone_adapter"][field] = value;
        assert(!synthetic_phone_supported(bad));
    }

    {
        auto bad = launch;
        bad["phone_adapter"]["extra"] = true;
        assert(!synthetic_phone_supported(bad));
    }
    const Json fixture{
            {"peer_name", "Voyemu test phone"},
            {"contacts", Json::array({{{"name", "Иван Иванов"}, {"number", "+70000000001"}},
                                      {{"name", "Анна Петрова"}, {"number", "+70000000002"}}})}};
    assert(phone_command("multimedia.phone.configure", fixture).at("payload") == fixture);

    for (const auto op :
         {"multimedia.phone.connect", "multimedia.phone.disconnect", "multimedia.phone.answer",
          "multimedia.phone.hangup", "multimedia.phone.state"}) {
        assert(phone_command(op, Json::object()).at("payload").empty());
    }

    assert(phone_command("multimedia.phone.incoming", {{"number", "+70000000001"}}).at("method") ==
           "incoming");
    auto rejected = [](auto action) {
        bool caught = false;
        try {
            action();
        } catch (const std::exception &) {
            caught = true;
        }

        assert(caught);
    };

    for (const Json &number : Json::array({"70000000001", "+7000000000", "+700000000001",
                                           "+7abcdefghij", "+7000000000\n", 123, nullptr})) {
        assert(!synthetic_phone_number(number));
    }

    for (const char *field : {"name", "number"}) {
        auto bad = fixture;
        bad["contacts"][0].erase(field);
        rejected([&] {
            phone_command("multimedia.phone.configure", bad);
        });
    }

    {
        auto bad = fixture;
        bad["contacts"][1]["number"] = bad["contacts"][0]["number"];
        rejected([&] {
            phone_command("multimedia.phone.configure", bad);
        });
    }
    {
        auto bad = fixture;
        bad["contacts"][0]["name"] = "bad\nname";
        rejected([&] {
            phone_command("multimedia.phone.configure", bad);
        });
    }
    {
        auto bad = fixture;
        bad["contacts"].erase(1);
        rejected([&] {
            phone_command("multimedia.phone.configure", bad);
        });
    }
    {
        auto bad = fixture;
        bad["contacts"][0]["extra"] = true;
        rejected([&] {
            phone_command("multimedia.phone.configure", bad);
        });
    }
    {
        auto bad = fixture;
        bad["peer_name"] = std::string(65, 'a');
        rejected([&] {
            phone_command("multimedia.phone.configure", bad);
        });
    }
    rejected([&] {
        phone_command("multimedia.phone.dial", Json::object());
    });
    rejected([&] {
        phone_command("multimedia.phone.connect", {{"number", "+70000000001"}});
    });
    const std::string token(32, 'a');
    Json reply{{"schema", 1},
               {"ok", true},
               {"session", "owned"},
               {"boot_id", "boot"},
               {"sequence", std::uint64_t{3}},
               {"provider_pid", 123},
               {"data", {{"outgoing", Json::array({"+70000000001"})}}}};
    validate_phone_reply(reply, "owned", "boot", 3, token);

    for (const char *field : {"schema", "session", "boot_id", "sequence", "data", "provider_pid"}) {
        auto bad = reply;
        bad.erase(field);
        rejected([&] {
            validate_phone_reply(bad, "owned", "boot", 3, token);
        });
    }

    for (const auto &sequence : Json::array({2, 4, 3.0, -1})) {
        auto bad = reply;
        bad["sequence"] = sequence;
        rejected([&] {
            validate_phone_reply(bad, "owned", "boot", 3, token);
        });
    }

    {
        auto bad = reply;
        bad["data"]["token"] = token;
        rejected([&] {
            validate_phone_reply(bad, "owned", "boot", 3, token);
        });
    }
    assert(phone_label(std::string(80, 'a'), 128, 80));
    assert(!phone_label(std::string(81, 'a'), 128, 80));
    assert(!phone_label("   ", 128, 80));
    assert(!phone_label(std::string("bad\x7f"), 128, 80));
    assert(phone_label("Иван Иванов", 128, 80));
    assert(phone_label("  Иван Иванов  ", 128, 80));
    {
        std::string name;

        for (int i = 0; i < 65; ++i) {
            name += "Я";
        }

        assert(!phone_label(name, 128, 80));
    }
    {
        std::string name;

        for (int i = 0; i < 33; ++i) {
            name += "😀";
        }

        assert(!phone_label(name, 128, 80));
    }
    {
        auto bad = reply;
        bad["provider_pid"] = 124;
        rejected([&] {
            validate_phone_reply(bad, "owned", "boot", 3, token, 123);
        });
    }

    for (const auto &pid : Json::array({0, -1, 1.5, "123"})) {
        auto bad = reply;
        bad["provider_pid"] = pid;
        rejected([&] {
            validate_phone_reply(bad, "owned", "boot", 3, token);
        });
    }

    for (const char *field :
         {"schema", "ok", "session", "boot_id", "sequence", "provider_pid", "data"}) {
        for (const auto &wrong : Json::array({nullptr, Json::array(), false, 1.5})) {
            auto bad = reply;
            bad[field] = wrong;
            bool protocol = false;
            try {
                validate_phone_reply(bad, "owned", "boot", 3, token, 123);
            } catch (const std::runtime_error &error) {
                protocol = std::string_view(error.what()).starts_with("PHONE_PROTOCOL:");
            }

            assert(protocol);
        }
    }

    {
        unsigned parses = 0, quarantines = 0;
        try {
            phone_provider_exchange(
                    "control",
                    [&] {
                        return parse_phone_provider_output(
                                "private-token-and-contact malformed response",
                                [&](const std::string &raw) -> Json {
                                    ++parses;
                                    throw std::runtime_error(raw);
                                });
                    },
                    [&] {
                        ++quarantines;
                    });
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string(error.what()) ==
                   "PHONE_PROVIDER_UNKNOWN: synthetic peer returned an unreadable receipt");
        }

        assert(parses == 1 && quarantines == 1);
        const auto parsed = parse_phone_provider_output("{}", [&](const std::string &raw) {
            ++parses;

            return Json::parse(raw);
        });
        assert(parsed == Json::object() && parses == 2 && quarantines == 1);
        try {
            phone_provider_exchange(
                    "snapshot",
                    [&] {
                        return parse_phone_provider_output("not-json", [](const std::string &raw) {
                            return Json::parse(raw);
                        });
                    },
                    [&] {
                        ++quarantines;
                    });
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string(error.what()).starts_with("PHONE_PROVIDER_UNKNOWN:"));
        }

        assert(quarantines == 1);
    }
    {
        const Json idle = {{"connected", false}, {"call", {{"state", 131}}}};
        assert(phone_control_idempotent_noop("disconnect", idle));
        assert(phone_control_idempotent_noop("hangup", idle));
        assert(!phone_control_idempotent_noop("connect", idle));
        auto connected = idle;
        connected["connected"] = true;
        assert(phone_control_idempotent_noop("connect", connected));
        assert(!phone_control_idempotent_noop("disconnect", connected));
        auto live = connected;
        live["call"]["state"] = 128;
        assert(!phone_control_idempotent_noop("hangup", live));
        assert(!phone_control_idempotent_noop("connect", live));

        for (const Json bad :
             {Json::object(), Json{{"call", {{"state", 131}}}}, Json{{"connected", false}},
              Json{{"connected", false}, {"call", Json::object()}},
              Json{{"connected", 0}, {"call", {{"state", 131}}}},
              Json{{"connected", false}, {"call", {{"state", 999}}}}}) {
            rejected([&] {
                phone_control_idempotent_noop("disconnect", bad);
            });
        }

        // Configure contract probe: a connected or non-terminated peer must reject configure
        // locally (PeerConfiguration.configure throws PHONE_CONNECTED strictly before any durable
        // mutation).
        assert(!phone_control_configure_requires_reject(idle));
        assert(phone_control_configure_requires_reject(connected));
        assert(phone_control_configure_requires_reject(live));
        auto ringing_idle = connected;
        ringing_idle["call"]["state"] = 130;
        assert(phone_control_configure_requires_reject(ringing_idle));
        auto disconnected_ringing = idle;
        disconnected_ringing["call"]["state"] = 129;
        assert(phone_control_configure_requires_reject(disconnected_ringing));

        for (const Json bad :
             {Json::object(), Json{{"call", {{"state", 131}}}}, Json{{"connected", false}},
              Json{{"connected", 0}, {"call", {{"state", 131}}}}}) {
            rejected([&] {
                phone_control_configure_requires_reject(bad);
            });
        }

        // Call-path contract probe: incoming on a disconnected or non-idle peer must reject
        // locally.
        assert(!phone_control_call_requires_reject("incoming", connected));
        assert(phone_control_call_requires_reject("incoming", idle));
        assert(phone_control_call_requires_reject("incoming", live));
        assert(!phone_control_call_requires_reject("other", idle));

        for (const Json bad :
             {Json::object(), Json{{"call", {{"state", 131}}}}, Json{{"connected", false}}}) {
            rejected([&] {
                phone_control_call_requires_reject("incoming", bad);
            });
        }

        // answer: requires a connected ringing/dialing peer; hangup: requires a connected peer.
        auto ringing = connected;
        ringing["call"]["state"] = 130;
        auto dialing = connected;
        dialing["call"]["state"] = 129;
        assert(!phone_control_call_requires_reject("answer", ringing));
        assert(!phone_control_call_requires_reject("answer", dialing));
        assert(phone_control_call_requires_reject("answer", connected));
        assert(phone_control_call_requires_reject("answer", idle));
        assert(phone_control_call_requires_reject("hangup", idle));
        assert(!phone_control_call_requires_reject("hangup", ringing));
        // Production Passion snapshot while Telecom has not yet delivered an outgoing connection.
        const Json pending = {{"ok", true},
                              {"incarnation", "owned-incarnation"},
                              {"sequence", 7},
                              {"connected", true},
                              {"pbap_connected", false},
                              {"sync_state", 0},
                              {"peer_name", "Voyemu Phone"},
                              {"peer_address", "02:00:00:00:00:01"},
                              {"call",
                               {{"state", 131},
                                {"id", 1},
                                {"number", "+70000000001"},
                                {"outgoing", true},
                                {"active_started_elapsed_ms", 0}}},
                              {"contacts", fixture.at("contacts")},
                              {"outgoing", Json::array()},
                              {"events", Json::array({"outgoing_requested"})},
                              {"diagnostics",
                               {{"transport", "synthetic-telecom"},
                                {"pending", true},
                                {"oem_hfp_delivered", false},
                                {"oem_pbap_delivered", false}}}};
        assert(!phone_control_idempotent_noop("hangup", pending));
        assert(!phone_control_call_requires_reject(
                "hangup", pending)); // cancellation must reach the pending Telecom call
        assert(!phone_control_idempotent_noop("disconnect", pending));
        assert(phone_control_idempotent_noop(
                "connect", pending)); // connect already has a peer and makes no mutation
        assert(phone_control_configure_requires_reject(pending));
        assert(phone_control_call_requires_reject("incoming", pending));
        assert(phone_control_call_requires_reject("answer", pending));
        auto settled = pending;
        settled["diagnostics"]["pending"] = false;
        assert(phone_control_idempotent_noop("hangup", settled));
        assert(!phone_control_call_requires_reject("incoming", settled));

        for (const auto &diagnostic :
             Json::array({nullptr, Json::object(), Json{{"pending", true}},
                          Json{{"transport", "foreign"}, {"pending", true}}})) {
            auto foreign = connected;
            foreign["diagnostics"] = diagnostic;
            assert(phone_control_idempotent_noop("hangup", foreign));
            assert(!phone_control_call_requires_reject("incoming", foreign));
        }

        {
            auto malformed = pending;
            malformed["diagnostics"].erase("pending");
            rejected([&] {
                phone_control_idempotent_noop("hangup", malformed);
            });
            rejected([&] {
                phone_control_configure_requires_reject(malformed);
            });
            rejected([&] {
                phone_control_call_requires_reject("incoming", malformed);
            });
        }

        for (const auto &value : Json::array({nullptr, 1, "true", Json::array(), Json::object()})) {
            auto malformed = pending;
            malformed["diagnostics"]["pending"] = value;
            rejected([&] {
                phone_control_idempotent_noop("hangup", malformed);
            });
            rejected([&] {
                phone_control_configure_requires_reject(malformed);
            });
            rejected([&] {
                phone_control_call_requires_reject("incoming", malformed);
            });
        }
    }
    {
        Json state = {{"connected", false}, {"powered", false}, {"call", {{"state", 131}}}};
        unsigned dispatches = 0;
        std::uint64_t sequence = 7;
        bool unknown = false;
        auto connect = [&] {
            require_phone_outcome_known("same-boot", "same-boot", unknown, true);
            require_phone_connect_power(state);
            ++sequence;
            ++dispatches;
        };
        try {
            connect();
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string(error.what()).starts_with("PHONE_PROVIDER_REJECTED:"));
        }

        assert(sequence == 7 && dispatches == 0 && !unknown);
        state["powered"] = true;
        connect();
        assert(sequence == 8 && dispatches == 1 && !unknown);
        state.erase("powered");
        connect();
        assert(sequence == 9 && dispatches == 2 && !unknown);

        for (const auto &power :
             Json::array({nullptr, 0, 1, "false", Json::object(), Json::array()})) {
            state["powered"] = power;
            try {
                connect();
                assert(false);
            } catch (const std::runtime_error &error) {
                assert(std::string(error.what()) ==
                       "PHONE_PROTOCOL: invalid synthetic peer power state");
            }

            assert(sequence == 9 && dispatches == 2 && !unknown);
        }

        rejected([&] {
            require_phone_connect_power(Json::array());
        });
    }
    // Quarantine policy: only genuinely uncertain dispatches poison the boot. A clean
    // closed-vocabulary rejection (PHONE_PROVIDER_REJECTED — the guest certified
    // mutation_started=false BEFORE reserving sequence) is a KNOWN not-delivered outcome for BOTH
    // bootstrap and control verbs.
    {
        const char *clean =
                "PHONE_PROVIDER_REJECTED: synthetic peer rejected the request; stage=identity; reason=PHONE_OWNER";
        unsigned quarantines = 0;
        try {
            phone_provider_exchange(
                    "control",
                    [&]() -> Json {
                        throw std::runtime_error(clean);
                    },
                    [&] {
                        ++quarantines;
                    });
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string(error.what()) == clean);
        }

        assert(quarantines == 0); // clean rejection must NOT poison
        try {
            phone_provider_exchange(
                    "bootstrap",
                    [&]() -> Json {
                        throw std::runtime_error(clean);
                    },
                    [&] {
                        ++quarantines;
                    });
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string(error.what()) == clean);
        }

        assert(quarantines == 0);
        // PhonePublic exceptions (delivery/protocol/uncertain) still poison bootstrap/control.
        try {
            phone_provider_exchange(
                    "control",
                    [&]() -> Json {
                        throw std::runtime_error(
                                "PHONE_DELIVERY_UNKNOWN: protected peer command did not return a verified receipt");
                    },
                    [&] {
                        ++quarantines;
                    });
            assert(false);
        } catch (const std::runtime_error
                         &) { /* Ignored: the expected rejection is asserted afterwards. */
        }

        assert(quarantines == 1);
        // Foreign (non-phone) failures are classified UNKNOWN and poison as before.
        try {
            phone_provider_exchange(
                    "control",
                    [&]() -> Json {
                        throw std::runtime_error("foreign transport error");
                    },
                    [&] {
                        ++quarantines;
                    });
            assert(false);
        } catch (const std::runtime_error &error) {
            assert(std::string(error.what()) ==
                   "PHONE_PROVIDER_UNKNOWN: synthetic peer exchange failed after dispatch");
        }

        assert(quarantines == 2);
    }

    for (const auto *failure :
         {"PHONE_DELIVERY_UNKNOWN: timeout", "PHONE_DELIVERY_UNKNOWN: exit status",
          "PHONE_DELIVERY_UNKNOWN: oversized receipt", "PHONE_PROTOCOL: wrong success identity",
          "PHONE_STALE: postdispatch boot fence"}) {
        unsigned dispatches = 0, quarantines = 0;
        bool unknown = false;
        std::string retained_boot;
        auto input = [&](const char *verb) {
            require_phone_outcome_known("boot", retained_boot, unknown, true);

            return phone_provider_exchange(
                    verb,
                    [&]() -> Json {
                        ++dispatches;
                        throw std::runtime_error(failure);
                    },
                    [&] {
                        ++quarantines;
                        retained_boot = "boot";
                        unknown = true;
                    });
        };
        rejected([&] {
            input("bootstrap");
        });
        assert(dispatches == 1 && quarantines == 1 && unknown);
        rejected([&] {
            input("bootstrap");
        });
        assert(dispatches == 1 && quarantines == 1);
        rejected([&] {
            input("control");
        });
        assert(dispatches == 1 && quarantines == 1);
        require_phone_outcome_known("new-boot", retained_boot, unknown, true);
        require_phone_outcome_known("boot", retained_boot, unknown, false);
    }

    for (const auto *verb : {"bootstrap", "control", "snapshot"}) {
        std::string message;
        try {
            phone_provider_exchange(
                    verb,
                    []() -> Json {
                        throw std::runtime_error("PHONE_PROTOCOL: wrong success identity");
                    },
                    [] {});
        } catch (const std::runtime_error &error) {
            message = error.what();
        }

        // A malformed receipt is an unknown outcome only after a dispatched mutation.
        assert(message == (std::string_view(verb) == "snapshot"
                                   ? "PHONE_PROTOCOL: wrong success identity"
                                   : "PHONE_PROTOCOL_UNKNOWN: wrong success identity"));
    }

    for (const auto *error :
         {"PHONE_STALE: private-boot-value", "ANDROID_IDENTITY_MISMATCH: private-owned-value",
          "unclassified private-token"}) {
        unsigned quarantines = 0;
        try {
            phone_provider_exchange(
                    "bootstrap",
                    [&]() -> Json {
                        throw std::runtime_error(error);
                    },
                    [&] {
                        ++quarantines;
                    });
            assert(false);
        } catch (const std::runtime_error &failure) {
            assert(std::string(failure.what()) ==
                   "PHONE_PROVIDER_UNKNOWN: synthetic peer exchange failed after dispatch");
        }

        assert(quarantines == 1);
    }

    {
        unsigned dispatches = 0, quarantines = 0;
        const auto result = phone_provider_exchange(
                "bootstrap",
                [&] {
                    ++dispatches;

                    return Json::object();
                },
                [&] {
                    ++quarantines;
                });
        assert(result == Json::object() && dispatches == 1 && quarantines == 0);
    }

    // A lost takeover receipt can hide a disconnect. Exercise the same bootstrap coordinator as
    // Session.
    for (const char *failure :
         {"PHONE_DELIVERY_UNKNOWN: protected peer command did not return a verified receipt",
          "PHONE_PROVIDER_UNKNOWN: synthetic peer returned an unreadable receipt"}) {
        std::string retained;
        std::uint64_t sequence = 19;
        bool unknown = false;
        std::int64_t pid = 73;
        unsigned dispatches = 0;
        auto attempt = [&] {
            bootstrap_phone_session("boot", retained, sequence, unknown, pid, [&] {
                return phone_provider_exchange(
                        "bootstrap",
                        [&]() -> Json {
                            ++dispatches;
                            throw std::runtime_error(failure);
                        },
                        [&] {
                            retained = "boot";
                            sequence = 0;
                            unknown = true;
                            pid = 0;
                        });
            });
        };
        rejected(attempt);
        assert(dispatches == 1 && unknown && retained == "boot" && pid == 0);
        rejected(attempt);
        assert(dispatches == 1); // a subsequent public request cannot replay either
    }

    {
        std::string retained;
        std::uint64_t sequence = 19;
        bool unknown = false;
        std::int64_t pid = 0;
        unsigned dispatches = 0;
        bootstrap_phone_session("boot", retained, sequence, unknown, pid, [&] {
            ++dispatches;

            return Json{{"provider_pid", 123}};
        });
        assert(dispatches == 1 && retained == "boot" && sequence == 0 && !unknown && pid == 123);
    }
    auto failure_code = [&](const Json &failure) {
        try {
            validate_phone_reply(failure, "owned", "boot", 3, token, 123);
        } catch (const std::runtime_error &error) {
            return std::string(error.what());
        }

        return std::string("ACCEPTED");
    };
    {
        auto stale = reply;
        stale["session"] = "private-other-session";
        const auto message = failure_code(stale);
        assert(message ==
                       "PHONE_PROTOCOL: synthetic peer receipt rejected; field=session; reason=value" &&
               message.find("private-other-session") == std::string::npos);
    }
    {
        auto stale = reply;
        stale["sequence"] = std::uint64_t{4};
        assert(failure_code(stale) ==
               "PHONE_PROTOCOL: synthetic peer receipt rejected; field=sequence; reason=value");
    }
    {
        auto stale = reply;
        stale["sequence"] = "private-not-a-number";
        const auto message = failure_code(stale);
        assert(message ==
                       "PHONE_PROTOCOL: synthetic peer receipt rejected; field=sequence; reason=type" &&
               message.find("private-not-a-number") == std::string::npos);
    }
    {
        auto stale = reply;
        stale.erase("boot_id");
        assert(failure_code(stale) ==
               "PHONE_PROTOCOL: synthetic peer receipt rejected; field=boot_id; reason=missing");
    }
    const Json legacy_failure{{"schema", 1}, {"ok", false}, {"error", "PHONE_CONTROL_REJECTED"}};
    assert(failure_code(legacy_failure).starts_with("PHONE_PROVIDER_UNKNOWN:"));
    auto failure = legacy_failure;
    failure["stage"] = "adapter_lookup";
    failure["reason"] = "PHONE_ADAPTER_INIT";
    failure["mutation_started"] = false;
    assert(failure_code(failure) ==
           "PHONE_PROVIDER_REJECTED: synthetic peer rejected the request; stage=adapter_lookup; reason=PHONE_ADAPTER_INIT");
    failure["reason"] = "PHONE_ADAPTER_FRESH_PROCESS_REQUIRED";
    assert(failure_code(failure).starts_with("PHONE_PROVIDER_REJECTED:"));
    failure["reason"] = "PHONE_ADAPTER_INIT";
    failure["mutation_started"] = true;
    assert(failure_code(failure).starts_with("PHONE_PROVIDER_UNKNOWN:"));
    failure["mutation_started"] = false;
    failure["stage"] = "adapter_control";
    assert(failure_code(failure).starts_with("PHONE_PROVIDER_UNKNOWN:"));
    {
        auto off = failure;
        off["stage"] = "adapter_precondition";
        off["reason"] = "PHONE_POWER_OFF";
        off["mutation_started"] = false;
        assert(failure_code(off).starts_with("PHONE_PROVIDER_REJECTED:"));
        off["mutation_started"] = true;
        assert(failure_code(off).starts_with("PHONE_PROVIDER_UNKNOWN:"));
        off["mutation_started"] = false;
        off["reason"] = "PHONE_ADAPTER_TIMEOUT";
        assert(failure_code(off).starts_with("PHONE_PROVIDER_UNKNOWN:"));
        off["reason"] = "PHONE_POWER_OFF";
        off["stage"] = "adapter_control";
        assert(failure_code(off).starts_with("PHONE_PROVIDER_UNKNOWN:"));
    }

    for (const char *field : {"stage", "reason", "mutation_started"}) {
        auto malformed = failure;
        malformed.erase(field);
        assert(failure_code(malformed).starts_with("PHONE_PROTOCOL:"));
    }

    for (const auto &bad : Json::array({nullptr, 123, false, "secret untrusted text"})) {
        auto malformed = failure;
        malformed["reason"] = bad;
        const auto message = failure_code(malformed);
        assert(message == "PHONE_PROTOCOL: malformed peer rejection diagnostics" &&
               message.find("secret") == std::string::npos);
    }

    {
        auto malformed = failure;
        malformed["stage"] = "../../private";
        assert(failure_code(malformed) == "PHONE_PROTOCOL: malformed peer rejection diagnostics");
    }
    {
        auto malformed = failure;
        malformed["mutation_started"] = "false";
        assert(failure_code(malformed) == "PHONE_PROTOCOL: malformed peer rejection diagnostics");
    }
    {
        auto leaked = failure;
        leaked["private"] = token;
        assert(failure_code(leaked) == "PHONE_PROTOCOL: private credential in response");
    }
    {
        auto malformed = legacy_failure;
        malformed["error"] = "OTHER_ERROR";
        assert(failure_code(malformed).starts_with("PHONE_PROTOCOL:"));
    }

    for (const auto verb : {"bootstrap", "snapshot"}) {
        const auto args = phone_provider_arguments(verb, "session", token, "boot", 0, "snapshot",
                                                   Json::object());
        assert(args.size() == (std::string_view(verb) == "bootstrap" ? 18 : 16) &&
               args.at(5) == verb && args.at(15) == "sequence:l:0");

        if (std::string_view(verb) == "bootstrap") {
            assert(args.at(17) == "reset:b:false");
        }

        assert(std::none_of(args.begin(), args.end(), [](const auto &value) {
            return value.starts_with("method:s:") || value.starts_with("payload:s:");
        }));
    }

    {
        const auto args = phone_provider_arguments("bootstrap", "session", token, "boot", 0,
                                                   "snapshot", Json::object(), true);
        assert(args.size() == 18 && args.at(17) == "reset:b:true");
    }
    {
        const auto args = phone_provider_arguments("control", "session", token, "boot", 1,
                                                   "configure", fixture);
        assert(args.size() == 20 && args.at(17) == "method:s:configure");
        // Guest content CLI binding parser (splitWithEscaping): '\' escapes the next character,
        // so the wire form differs from the raw JSON (colons and backslashes are escaped).
        const auto wire = args.at(19).substr(10);
        std::string unescaped;

        for (std::size_t i = 0; i < wire.size(); ++i) {
            if (wire.at(i) == '\\' && i + 1 < wire.size()) {
                ++i;
            }

            unescaped += wire.at(i);
        }

        assert(Json::parse(unescaped) == fixture);
        assert(wire.find(":") == std::string::npos ? wire.size() == unescaped.size() : true);
        {
            auto nested = fixture;
            nested["contacts"][0]["number"] = "+7\\0001";
            const auto wired = phone_provider_arguments("control", "session", token, "boot", 1,
                                                        "configure", nested)
                                       .at(19)
                                       .substr(10);
            std::string back;

            for (std::size_t i = 0; i < wired.size(); ++i) {
                if (wired.at(i) == '\\' && i + 1 < wired.size()) {
                    ++i;
                }

                back += wired.at(i);
            }

            assert(Json::parse(back) == nested);
        }
    }
    {
        auto legacy = fixture;
        legacy["peer_name"] = "A9: \"quoted\" \\ path";
        const auto args = phone_provider_arguments("control", "session", token, "boot", 1,
                                                   "configure", legacy, false, 28);
        // API28 Content.Parser.parseBindValue takes the substring after its second colon verbatim.
        const auto binding = args.at(19);
        const auto first = binding.find(':'), second = binding.find(':', first + 1);
        assert(Json::parse(binding.substr(second + 1)) == legacy);
    }
    std::cout << "phone control contracts passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
