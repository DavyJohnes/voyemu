#include "api.hpp"
#include "guest_android_fake.hpp"
#include "profile_catalog.hpp"
#include "session.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;

// Usage: ui_tree_test <scratch directory>
int main(int argc, char **argv) try {
    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) / ("voyemu-ui-tree-" + random_token());
    const auto id = random_token();
    const std::string profile =
            embedded_profile_names(ProfilePlatform::qinggan09, ProfileBoard::sa8155).at(0);
    const auto auth = random_token();
    const auto directory = root / ".local/machines" / profile / "sessions" / id;

    ensure_private_directory(directory);
    Session session(directory, id);
    write_private_json(root / ".local/machines" / profile / "active" / (profile + ".json"),
                       {{"schema", 1},
                        {"session_id", id},
                        {"directory", directory.string()},
                        {"endpoint", "owned-tree-fixture"}});
    const std::string boot = "aaaaaaaa-1111-2222-3333-444444444444",
                      other_boot = "bbbbbbbb-1111-2222-3333-444444444444";
    unsigned controls = 0, dispatches = 0;
    bool changed_pid = false, changed_boot = false;
    unsigned observer_api = 28, ui_reads = 0;
    bool interactive_capability = true;
    Json last_ui;
    Json node = {{"id", "1"},
                 {"parent", nullptr},
                 {"window_id", 8},
                 {"resource_id", "example:id/switch"},
                 {"class", "android.widget.Switch"},
                 {"text", "Light"},
                 {"description", ""},
                 {"bounds", {10, 20, 50, 60}},
                 {"clipped_bounds", {10, 20, 50, 60}},
                 {"visible", true},
                 {"enabled", true},
                 {"clickable", true},
                 {"actionable", true},
                 {"checked", false},
                 {"text_redacted", false}};
    Json primary = {{"surface", "media_primary"},
                    {"display_id", 0},
                    {"status", "ok"},
                    {"complete", true},
                    {"windows", Json::array({{{"id", 8}, {"package", "example"}}})},
                    {"nodes", Json::array({node})}};
    Json secondary = {{"surface", "media_secondary"},
                      {"display_id", 1},
                      {"status", "unsupported"},
                      {"reason", "api28_secondary_tree_not_verified"}};
    api::Transports transports;
    auto guest = std::make_shared<api::ScriptedGuestAndroid>();
    transports.android = guest;
    guest->owner = [&](const Json &, bool, int) {
        ++controls;

        return Json{{"ok", true},
                    {"session_id", id},
                    {"android_pid", 123},
                    {"connection",
                     {{"port", 23456u},
                      {"auth", auth},
                      {"boot_id", boot},
                      {"observer_pid", 777},
                      {"api_level", observer_api}}}};
    };
    transports.control = [&](const auto &, const Json &operation, int) {
        ++controls;

        if (operation.at("op") == "media_input") {
            ++dispatches;

            return Json{{"ok", true}, {"session_id", id}, {"accepted", true}, {"released", true}};
        }

        auto result = session.handle(operation);
        result["android_pid"] = changed_pid ? 124 : 123;

        return result;
    };
    guest->exchange = [&](const Json &connection, const Json &operation, int) {
        assert(connection.at("auth") == auth);

        if (operation.at("op") == "hello") {
            return Json{{"ok", true},
                        {"boot_id", changed_boot ? other_boot : boot},
                        {"observer_pid", 777},
                        {"parallel_surfaces", true},
                        {"observer_epoch", "observer"},
                        {"ui_scopes", interactive_capability
                                              ? Json::array({"all_attached", "interactive"})
                                              : Json::array({"all_attached"})}};
        }

        assert(operation.at("op") == "ui");
        ++ui_reads;
        last_ui = operation;

        if (operation.contains("scope")) {
            assert(operation.at("scope") == Json({{"windows", "all_attached"}}) ||
                   operation.at("scope") == Json({{"windows", "interactive"}}));
        }

        const bool parallel = operation.value("parallel_surfaces", false);
        Json surfaces = Json::array();

        for (const auto &name : operation.at("surfaces")) {
            auto slot = name == "media_primary" ? primary : secondary;

            if (parallel) {
                slot.update({{"scope", "all_attached"},
                             {"clock_domain", "android_uptime_ms"},
                             {"started_uptime_ms", 101u},
                             {"completed_uptime_ms", 109u}});

                if (slot.contains("windows")) {
                    for (auto &window : slot.at("windows")) {
                        window["display_id"] = slot.at("display_id");
                    }
                }
            }

            surfaces.push_back(std::move(slot));
        }

        Json result{{"schema", 1},
                    {"ok", true},
                    {"observation_id", "observed-tree"},
                    {"observer_epoch", "observer"},
                    {"boot_id", boot},
                    {"observer_pid", 777},
                    {"started_uptime_ms", 100},
                    {"completed_uptime_ms", 110},
                    {"surfaces", surfaces}};

        if (parallel) {
            result["parallel_surfaces"] = true;
        }

        return result;
    };
    api::Api api(root, "unused-fixture", transports);
    auto request = Json{{"schema", 1},
                        {"request_id", "tree-test"},
                        {"op", "session.ui"},
                        {"profile", profile},
                        {"session", {{"id", id}}},
                        {"screens", Json::array({"primary", "secondary"})}};
    const auto media_error = [](const Json &response, int index = 0) {
        return response.at("result").at("multimedia")[index].at("error");
    };
    const auto result = api.call(request);
    assert(result.at("ok") == true && result.at("result").at("format") == "voyemu.session.ui.v1" &&
           last_ui.at("parallel_surfaces") == true);
    assert(result.at("result").at("session_id") == id &&
           result.at("result").at("multimedia")[0].at("android_pid") == 123);
    assert(result.at("result").at("complete") == false &&
           result.at("result").at("multimedia").size() == 2 &&
           result.at("result").at("cluster").is_null());
    assert(result.dump().find(auth) == std::string::npos &&
           !result.at("result").contains("connection"));
    auto single = request;
    single["screens"] = {"primary"};
    const auto single_result = api.call(single).at("result");
    assert(single_result.at("status") == "ready" && single_result.at("complete") == true &&
           single_result.at("atomic_ui_state") == false);
    {
        const Json connection = {
                {"observer_id", id}, {"boot_id", boot}, {"observer_pid", 777}, {"api_level", 28}};
        auto private_ipc = transports;
        private_ipc.android = nullptr;
        unsigned exchanged = 0;
        private_ipc.control = [&](const auto &endpoint, const Json &operation, int timeout) {
            const auto name = operation.at("op");

            if (name == "ui_observer") {
                return Json{{"ok", true},
                            {"session_id", id},
                            {"android_pid", 123},
                            {"connection", connection}};
            }

            if (name == "ui_observer_exchange") {
                ++exchanged;
                assert(operation.at("connection") == connection &&
                       operation.at("session_id") == id);
                assert(timeout > 0 && operation.at("timeout_ms") == timeout);
                const auto &message = operation.at("message");
                assert(!message.contains("auth") && !connection.contains("port"));

                return Json{{"ok", true},
                            {"session_id", id},
                            {"observation", guest->exchange({{"auth", auth}}, message, timeout)}};
            }

            return transports.control(endpoint, operation, timeout);
        };
        api::Api through_supervisor(root, "unused-fixture", private_ipc);
        const auto result = through_supervisor.call(single);
        assert(result.at("ok") == true && result.at("result").at("status") == "ready" &&
               exchanged == 2);
        assert(result.dump().find(auth) == std::string::npos);
    }
    const auto &screen = single_result.at("multimedia")[0];
    assert(single_result.at("multimedia")[1].is_null() && single_result.at("cluster").is_null());
    assert(screen.at("source") == "android.ui" &&
           screen.at("source_clock_domain") == "android.uptime" && screen.at("boot_id") == boot);
    assert(!screen.contains("qnx_generation") && screen.at("host_interval").at("clock_id") == id);
    assert(screen.at("screen") == "primary" && screen.at("status") == "ok" &&
           screen.at("complete") == true && screen.at("windows").is_array());
    const auto &observed_node = screen.at("nodes")[0];
    assert(observed_node.at("id") == "1" && observed_node.at("parent").is_null() &&
           observed_node.at("visible") == true && observed_node.at("enabled") == true);
    auto targeted = request;
    targeted["screens"] = {"primary"};
    targeted["selector"] = {{"resource_id", "example:id/switch"}, {"package", "example"}};
    primary["nodes"][0]["package"] = "example";
    primary["nodes"][0]["parent"] = nullptr;
    primary["matched_count"] = 1;
    primary["selection"] = "resource_id";
    auto selected = api.call(targeted);
    assert(selected.at("ok") == true &&
           selected.at("result").at("multimedia")[0].at("selector") == targeted.at("selector") &&
           !last_ui.contains("parallel_surfaces"));
    primary["nodes"] = Json::array();
    primary["matched_count"] = 0;
    assert(api.call(targeted).at("result").at("multimedia")[0].at("nodes").empty());
    primary["nodes"] = Json::array({node});
    primary["nodes"][0]["package"] = "example";
    primary["matched_count"] = 1;
    primary["nodes"][0]["resource_id"] = "example:id/unrelated";
    assert(media_error(api.call(targeted)).at("code") == "TREE_PROTOCOL");
    primary["nodes"][0]["resource_id"] = "example:id/switch";
    primary.erase("matched_count");
    primary.erase("selection");
    primary["nodes"][0] = node;

    for (const auto &selector :
         {Json::object(), Json{{"resource_id", "not_a_resource"}},
          Json{{"resource_id", "example:id/switch"}, {"contains", "switch"}},
          Json{{"resource_id", "example:id/switch"}, {"package", "bad package"}}}) {
        auto malformed = targeted;
        malformed["selector"] = selector;
        const auto before = controls;
        assert(api.call(malformed).at("error").at("code") == "TEST_ARGUMENT" && controls == before);
    }

    single["screens"] = {"secondary"};
    assert(media_error(api.call(single), 1).at("code") == "TEST_UNSUPPORTED");

    for (const auto &invalid : {Json{{"screens", {"primary", "primary"}}},
                                Json{{"screens", "primary"}}, Json{{"screens", {"media_primary"}}},
                                Json{{"screen", "primary"}, {"screens", {"primary"}}},
                                Json{{"surfaces", {"media_primary"}}},
                                Json{{"surface", "media_primary"}}, Json{{"timeout_ms", 0}}}) {
        auto malformed = request;
        malformed.update(invalid);
        const auto before = controls;
        assert(api.call(malformed).at("error").at("code") == "TEST_ARGUMENT" && controls == before);
    }

    {
        auto secondary_read = request;
        secondary_read.erase("screens");
        secondary_read["screen"] = "secondary";
        auto saved = secondary;
        secondary = primary;
        secondary["surface"] = "media_secondary";
        secondary["display_id"] = 7;
        secondary["sdk_display_id"] = 1;
        secondary["screen"] = "secondary";
        auto dynamic = api.call(secondary_read);
        assert(dynamic.at("ok") == true &&
               dynamic.at("result").at("multimedia")[1].at("display_id") == 7 &&
               dynamic.at("result").at("multimedia")[1].at("sdk_display_id") == 1 &&
               !dynamic.at("result").at("multimedia")[1].contains("surface"));
        secondary = saved;
    }
    {
        auto interactive = request;
        interactive["scope"] = "interactive";
        assert(media_error(api.call(interactive)).at("code") == "TEST_UNSUPPORTED");
        observer_api = 30;
        interactive_capability = false;
        const auto before_reads = ui_reads;
        assert(media_error(api.call(interactive)).at("code") == "TEST_UNSUPPORTED" &&
               ui_reads == before_reads);
        interactive_capability = true;
        const auto saved_primary = primary, saved_secondary = secondary;
        primary["scope"] = "api30_interactive";
        primary["selection_coverage"] = "interactive_windows_only";
        primary["windows"][0]["display_id"] = 0;
        secondary = primary;
        secondary["surface"] = "media_secondary";
        secondary["display_id"] = 7;
        secondary["windows"][0]["display_id"] = 7;
        const auto both = api.call(interactive);
        assert(both.at("ok") == true && both.at("result").at("multimedia").size() == 2 &&
               last_ui.at("scope").at("windows") == "interactive");
        assert(both.at("result").at("multimedia")[1].at("display_id") == 7 &&
               both.at("result").at("multimedia")[1].at("scope") == "api30_interactive");
        secondary["windows"][0]["display_id"] = 0;
        assert(media_error(api.call(interactive)).at("code") == "TREE_PROTOCOL");
        secondary["windows"][0]["display_id"] = 7;
        secondary["scope"] = "all_attached";
        assert(media_error(api.call(interactive)).at("code") == "TREE_PROTOCOL");
        secondary["scope"] = "api30_interactive";
        secondary.erase("selection_coverage");
        assert(media_error(api.call(interactive)).at("code") == "TREE_PROTOCOL");
        primary = saved_primary;
        secondary = saved_secondary;
        observer_api = 28;
        auto invalid = request;
        invalid["scope"] = "interactive_extra";
        const auto before = controls;
        assert(api.call(invalid).at("error").at("code") == "TEST_ARGUMENT" && controls == before);
    }
    {
        auto read = request;
        read["screens"] = {"primary"};
        const auto saved = primary;
        primary["nodes"][0]["visible"] = false;
        primary["nodes"][0]["actionable"] = false;
        primary["nodes"][0]["bounds"] = {10, 750, 50, 720};
        primary["nodes"][0]["clipped_bounds"] = {10, 720, 10, 720};
        const auto offscreen = api.call(read);
        assert(offscreen.at("ok") == true);
        assert(offscreen.at("result").at("multimedia")[0].at("nodes")[0].at("bounds").at(
                       "height") == 0);
        primary["nodes"][0]["bounds"] = {1968, 20, 1920, 60};
        assert(api.call(read).at("result").at("multimedia")[0].at("nodes")[0].at("bounds").at(
                       "width") == 0);
        primary["nodes"][0]["visible"] = true;
        assert(media_error(api.call(read)).at("code") == "TREE_PROTOCOL");
        primary["nodes"][0]["visible"] = false;
        primary["nodes"][0]["bounds"][0] = UINT64_MAX;
        assert(media_error(api.call(read)).at("code") == "TREE_PROTOCOL");
        primary = saved;
    }
    {
        const auto saved = primary;
        const auto ownership = random_token();
        write_private_json(directory / "automation-owner.json",
                           {{"schema", 1}, {"session_id", id}, {"ownership_id", ownership}});
        primary["status"] = "partial";
        primary["complete"] = false;
        primary["width"] = 1920;
        primary["height"] = 720;
        primary["display_epoch"] = "display";
        const std::string phone = "com.qinggan.bluetoothphone";
        Json w = {{"id", 8},           {"type", 2010},     {"bounds", {0, 0, 1920, 720}},
                  {"title", "Phone"},  {"package", phone}, {"read_complete", true},
                  {"root_id", "root"}, {"node_count", 2}};
        Json binding = {{"schema", 1},
                        {"basis", "api28_wms_process_viewdump_v1"},
                        {"boot_id", boot},
                        {"observer_epoch", "observer"},
                        {"display_epoch", "display"},
                        {"display_id", 0},
                        {"window_id", 8},
                        {"session_id", "session"},
                        {"session_pid", 2449},
                        {"session_uid", 1000},
                        {"owner_uid", 1000},
                        {"package", phone},
                        {"process_name", phone},
                        {"window_token", "token"},
                        {"window_client", "client"},
                        {"root_id", "root"},
                        {"node_count", 2},
                        {"wm_order", 0},
                        {"wm_order_verified", true},
                        {"read_complete", true},
                        {"process_verified", true},
                        {"current_focus", true},
                        {"touchable", true},
                        {"focusable", true},
                        {"visible", true},
                        {"has_surface", true},
                        {"drawn", true},
                        {"view_visible", true},
                        {"obscured", false}};
        w["verified_binding"] = binding;
        primary["windows"] = Json::array({w});
        auto answer = node;
        answer["id"] = "answer";
        answer["parent"] = "root";
        answer["package"] = phone;
        answer["resource_id"] = phone + ":id/answer_iv";
        answer["actionable"] = false;
        auto root_node = answer;
        root_node["id"] = "root";
        root_node["parent"] = nullptr;
        root_node["resource_id"] = "";
        root_node["clickable"] = false;
        primary["nodes"] = Json::array({answer, root_node});
        auto read = request;
        read.erase("screens");
        read["screen"] = "primary";
        read["scope"] = "all_attached";
        // Public identity must come from an actual observation, not a fixture-only projection.
        auto fresh = api.call(read);
        assert(fresh.at("ok") == true);
        const auto &tree = fresh.at("result");
        auto tap = request;
        tap["op"] = "input.tap";
        tap.erase("screens");
        tap["session"]["ownership_id"] = ownership;
        tap["screen"] = "primary";
        tap["scope"] = "all_attached";
        tap["selector"] = {{"resource_id", phone + ":id/answer_iv"}, {"package", phone}};
        Json identity;

        for (const char *key :
             {"id", "window_id", "resource_id", "package", "class", "bounds", "clipped_bounds"}) {
            identity[key] = tree.at("multimedia")[0].at("nodes")[0].at(key);
        }

        tap["expected"] = {{"boot_id", boot},
                           {"source_instance", "observer"},
                           {"revision", "observed-tree"},
                           {"window", tree.at("multimedia")[0].at("windows")[0]},
                           {"target", identity}};
        const auto before = dispatches;
        const auto accepted = api.call(tap);
        assert(accepted.at("ok") == true && dispatches == before + 1 &&
               last_ui.at("scope") == Json({{"windows", "all_attached"}}));
        assert(accepted.at("result").at("fence_scope") ==
                       "target_identity_geometry_before_first_dispatch" &&
               accepted.at("result").at("occlusion") == "unknown" &&
               accepted.at("result").at("touchability") == "unknown" &&
               accepted.at("result").at("input_coverage") == "unknown" &&
               accepted.at("result").at("atomic_ui_input") == false);
        primary["windows"][0]["verified_binding"]["session_pid"] = 2450;
        tap["request_id"] = random_token();
        assert(api.call(tap).at("ok") == false && dispatches == before + 1);
        primary = saved;
    }
    changed_pid = true;
    assert(media_error(api.call(request)).at("code") == "TREE_STALE");
    changed_pid = false;
    changed_boot = true;
    assert(media_error(api.call(request)).at("code") == "TREE_STALE");
    changed_boot = false;
    primary["display_id"] = 1;
    assert(media_error(api.call(request)).at("code") == "TREE_PROTOCOL");
    primary["display_id"] = 0;
    primary["nodes"][0]["clipped_bounds"] = {10, 20, 10, 20};
    assert(media_error(api.call(request)).at("code") == "TREE_PROTOCOL");
    primary["nodes"][0] = node;
    primary["nodes"][0]["enabled"] = false;
    assert(media_error(api.call(request)).at("code") == "TREE_PROTOCOL");
    primary["nodes"][0] = node;
    primary["nodes"][0]["text_redacted"] = true;
    primary["nodes"][0]["text"] = "private-password";
    auto leak = api.call(request);
    assert(media_error(leak).at("code") == "TREE_PROTOCOL" &&
           leak.dump().find("private-password") == std::string::npos);
    primary["nodes"][0] = node;
    {
        auto attached = request;
        attached.erase("screens");
        attached["screen"] = "primary";
        attached["scope"] = "all_attached";
        primary["selection"] = "resource_id_with_tree_context";
        primary["matched_count"] = 1;
        primary["nodes"][0]["matched"] = true;
        primary["nodes"][0]["package"] = "example";
        auto context_node = node;
        context_node["id"] = "context";
        context_node["resource_id"] = "example:id/container";
        context_node["matched"] = false;
        context_node["actionable"] = false;
        context_node["clipped_bounds"] = nullptr;
        primary["nodes"].push_back(context_node);
        attached["selector"] = {{"resource_id", "example:id/switch"}, {"package", "example"}};
        auto context = api.call(attached);
        assert(context.at("ok") == true &&
               context.at("result").at("multimedia")[0].at("nodes").size() == 2);
        assert(context.at("result").at("multimedia")[0].at("nodes")[0].at("bounds") ==
               Json({{"x", 10}, {"y", 20}, {"width", 40}, {"height", 40}}));
        primary["nodes"][1]["text_redacted"] = true;
        primary["nodes"][1]["text"] = "must-not-leak-with-unknown-geometry";
        auto blocked = api.call(attached);
        assert(media_error(blocked).at("code") == "TREE_PROTOCOL" &&
               blocked.dump().find("must-not-leak") == std::string::npos);
        primary["nodes"] = Json::array({node});
        primary.erase("selection");
        primary.erase("matched_count");
    }
    Json metadata = {
            {"schema", 1},
            {"session_id", id},
            {"android_pid", 123},
            {"connection",
             {{"port", 23456u}, {"auth", auth}, {"boot_id", boot}, {"observer_pid", 777}}}};
    write_private_json(directory / "ui-observer.json", metadata);
    const auto cached_controls = controls;
    primary["nodes"][0]["text"] = "Fresh after metadata reuse";
    const auto cached = api.call(request);
    assert(cached.at("ok") == true && controls == cached_controls + 1);
    assert(cached.at("result").at("multimedia")[0].at("nodes")[0].at("text") ==
           "Fresh after metadata reuse");
    {
        const auto saved = secondary;
        secondary = primary;
        secondary["surface"] = "media_secondary";
        secondary["display_id"] = 1;
        const auto before = controls;

        for (const auto *screen : {"primary", "secondary", "primary"}) {
            auto read = request;
            read["screens"] = Json::array({screen});
            const auto observed = api.call(read);
            assert(observed.at("ok") == true &&
                   observed.at("result")
                                   .at("multimedia")[std::string_view(screen) == "primary" ? 0 : 1]
                                   .at("observer_pid") == 777);
        }

        assert(controls == before + 3); // Status fences only; no new ui_observer registration.
        secondary = saved;
    }
    changed_pid = true;
    assert(media_error(api.call(request)).at("code") == "TREE_STALE");
    changed_pid = false;
    changed_boot = true;
    assert(media_error(api.call(request)).at("code") == "TREE_STALE");
    changed_boot = false;
    metadata["session_id"] = random_token();
    write_private_json(directory / "ui-observer.json", metadata);
    const auto wrong_session_controls = controls;
    assert(media_error(api.call(request)).at("code") == "TREE_STALE" &&
           controls == wrong_session_controls);
    std::filesystem::remove(directory / "ui-observer.json");
    auto old = request;
    old["op"] = "output.hierarchy";
    assert(api.call(old).at("error").at("code") == "TEST_UNSUPPORTED");
    {
        write_private_json(
                directory / "ui-observer.json",
                {{"schema", 1},
                 {"session_id", id},
                 {"android_pid", 123},
                 {"connection",
                  {{"port", 23456u}, {"auth", auth}, {"boot_id", boot}, {"observer_pid", 777}}}});
        unsigned recoveries = 0;
        bool persistent = false, boot_switched = false;
        api::Transports recovery;
        auto reconnecting = std::make_shared<api::ScriptedGuestAndroid>();
        recovery.android = reconnecting;
        reconnecting->owner = [&](const Json &, bool recover, int timeout) {
            assert(recover && timeout > 0 && timeout <= 5000);
            ++recoveries;

            return Json{{"ok", true},
                        {"session_id", id},
                        {"android_pid", 123},
                        {"connection",
                         {{"port", 23457u},
                          {"auth", auth},
                          {"boot_id", boot_switched ? other_boot : boot},
                          {"observer_pid", 778}}}};
        };
        recovery.control = [&](const auto &, const Json &operation, int timeout) {
            assert(timeout > 0 && timeout <= 5000);
            assert(operation.at("op") == "status");
            auto value = session.handle(operation);
            value["android_pid"] = 123;

            return value;
        };
        reconnecting->exchange = [&](const Json &connection, const Json &operation, int) {
            if (connection.at("observer_pid") == 777 || persistent) {
                throw std::runtime_error("TREE_TRANSPORT_UNKNOWN: observer disconnected");
            }

            if (operation.at("op") == "hello") {
                return Json{{"ok", true}, {"boot_id", boot}, {"observer_pid", 778}};
            }

            return Json{{"schema", 1},
                        {"ok", true},
                        {"observation_id", "fresh-after-reconnect"},
                        {"boot_id", boot},
                        {"observer_pid", 778},
                        {"surfaces", Json::array({primary})}};
        };
        api::Api recovered(root, "unused", recovery);
        auto read = request;
        read["screens"] = {"primary"};
        auto repaired = recovered.call(read);
        assert(repaired.at("ok") == true &&
               repaired.at("result").at("multimedia")[0].at("observer_pid") == 778 &&
               recoveries == 1);
        assert(repaired.dump().find(auth) == std::string::npos);
        persistent = true;
        auto failed = recovered.call(read);
        assert(failed.at("ok") == true &&
               media_error(failed).at("code") == "TREE_TRANSPORT_UNKNOWN" &&
               media_error(failed).at("retryable") == true &&
               media_error(failed).at("outcome") == "unknown" && recoveries == 2);
        persistent = false;
        boot_switched = true;
        assert(media_error(recovered.call(read)).at("code") == "TREE_STALE" && recoveries == 3);
    }
    std::filesystem::remove_all(root);
    std::cout << "UI tree host contract: fences, surfaces, privacy and read-only access passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
