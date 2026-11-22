#include "process.hpp"
#include "session.hpp"
#include "ui_observer_wire.hpp"
#include <exception>
#include <iostream>
#include <optional>
#include <sstream>

static void wire_fixture(int mode, bool private_operation) {
    using namespace voyemu;
    const auto require = [](bool condition) {
        if (!condition) {
            throw std::runtime_error("observer wire fixture");
        }
    };
    std::string received;
    struct Stream {
        int mode;
        std::string &received;
        bool requested = false;
        unsigned sent_frames = 0;
        struct Poll {
            bool opened;
            std::string output;
            std::optional<ProcessResult> terminal;
        };

        Poll poll() {
            if (!requested || sent_frames >= 2) {
                return {true, {}, std::nullopt};
            }

            switch (mode) {
            case 1:
                if (sent_frames++ == 0) {
                    return {true, "{", std::nullopt};
                }

                break;
            case 2:
                if (sent_frames++ == 0) {
                    return {true, "{\"ok\":true}\n", std::nullopt};
                }

                break;
            case 3:
                return {true, {}, ProcessResult{0, {}, false}};
            case 4:
                return {true, sent_frames++ == 0 ? "{\"ok\":" : "true}\n", std::nullopt};
            case 5:
                return {true, "{}\n{}\n", std::nullopt};
            case 6:
                return {true, "{\"value\":\"private-auth-fixture\"}\n", std::nullopt};
            case 7:
                return {true, std::string(observer_response_bytes + 2, 'x'), std::nullopt};
            default:
                break;
            }

            return {true, {}, std::nullopt};
        }

        std::size_t write(std::string_view bytes) {
            const auto count = std::min<std::size_t>(8, bytes.size());
            received.append(bytes.substr(0, count));
            requested = received.back() == '\n';

            return count;
        }
    };
    std::ostringstream diagnostic;
    auto *previous = std::cerr.rdbuf(diagnostic.rdbuf());
    constexpr int budget_ms = 1000;
    bool typed = false, success = false;
    try {
        const auto value = exchange_ui_observer(
                {{"observer_pid", 4835}},
                {{"op", private_operation ? "private-operation-fixture" : "ui"},
                 {"payload", "private-payload-fixture"}},
                "private-auth-fixture", budget_ms, [&] {
                    return Stream{mode, received};
                });
        success = value.value("ok", false);
    } catch (const std::runtime_error &error) {
        typed = std::string(error.what()) ==
                (mode == 7   ? "TREE_LIMIT: response exceeds bound"
                 : mode == 6 ? "TREE_PROTOCOL: private observer credential in response"
                 : mode == 5 ? "TREE_PROTOCOL: multiple frames rejected"
                 : mode == 3 ? "TREE_TRANSPORT_UNKNOWN: observer closed before response"
                             : "TREE_TIMEOUT_UNKNOWN: observer deadline elapsed");
    }

    std::cerr.rdbuf(previous);
    require(received.find("private-auth-fixture") != std::string::npos);

    if (mode == 2 || mode == 4) {
        require(success && diagnostic.str().empty());

        return;
    }

    require(typed);

    if (mode >= 3) {
        require(diagnostic.str().empty());

        return;
    }

    const auto record = Json::parse(diagnostic.str());
    require(record.at("level") == "error" && record.at("message") == "observer_wire_timeout");
    const auto &report = record.at("fields");
    require(report.at("phase") == "recv" &&
            report.at("op") == (private_operation ? "other" : "ui") &&
            report.at("budget_ms") == budget_ms && report.at("elapsed_ms").get<int>() >= 0 &&
            report.at("response_bytes") == (mode == 1 ? 1 : 0) &&
            report.at("observer_pid") == 4835);
    require(diagnostic.str().find("private") == std::string::npos && !report.contains("auth") &&
            !report.contains("payload"));
}

static void ipc_fixture() {
    using namespace voyemu;
    const auto owner = random_token();
#ifdef _WIN32
    const std::filesystem::path endpoint = "\\\\.\\pipe\\voyemu-" + owner;
#else
    const auto endpoint = native_socket_root() / (owner + "-ui.sock");
#endif
    const std::string large_observation(observer_response_bytes - 64, 'a');
    ControlServer server(endpoint, [&](const Json &op) {
        if (op.at("op") == "ui_observer_exchange") {
            return Json{{"ok", true}, {"observation", {{"payload", large_observation}}}};
        }

        return Json{{"payload", large_observation}};
    });
    std::atomic<bool> stop = false;
    std::thread worker([&] {
        server.serve(stop);
    });
    struct Join {
        std::atomic<bool> &stop;
        std::thread &worker;

        ~Join() {
            stop = true;
            worker.join();
        }
    } join{stop, worker};
    const auto response = request(endpoint, {{"op", "ui_observer_exchange"}}, 5000);

    if (response.at("observation").at("payload") != large_observation) {
        throw std::runtime_error("observer IPC truncated a bounded reply");
    }

    const auto rejects = [&](const Json &op) {
        try {
            (void)request(endpoint, op, 1000);
        } catch (const std::runtime_error &) {
            return;
        }

        throw std::runtime_error("observer IPC expanded an unrelated message limit");
    };
    rejects({{"op", "status"}});
    // A request above the 1 MiB session message limit stays rejected for the observer exchange.
    rejects({{"op", "ui_observer_exchange"}, {"payload", std::string(2 * 1024 * 1024, 'x')}});
}

int main() try {
    using namespace voyemu;
    const auto require = [](bool condition) {
        if (!condition) {
            throw std::runtime_error("observer diagnostics regression");
        }
    };

    for (const auto *phase : {"connect", "send", "recv"}) {
        const auto report = Json::parse(observer_wire_timeout("ui", phase, 10000, 10000, 7, 4835));
        require(report.at("phase") == phase && report.at("response_bytes") == 7 &&
                report.at("observer_pid") == 4835);
    }

    const auto sanitized = observer_wire_timeout("private-operation", "private-phase", 10000, 10000,
                                                 0, -1);
    require(sanitized.find("private") == std::string::npos &&
            Json::parse(sanitized).at("observer_pid").is_null());
    ipc_fixture();
    wire_fixture(0, false);
    wire_fixture(1, false);
    wire_fixture(0, true);
    wire_fixture(2, false);
    wire_fixture(3, false);
    wire_fixture(4, false);
    wire_fixture(5, false);
    wire_fixture(6, false);
    wire_fixture(7, false);
    Json hello = {{"ok", false},
                  {"reason", "observer_unavailable"},
                  {"error_type", "InvocationTargetException"},
                  {"cause_type", "IllegalStateException"},
                  {"failure",
                   {{"causes",
                     Json::array({{{"code", "automation_service_already_registered"},
                                   {"message", "private token"},
                                   {"frames", Json::array({{{"class", "android.app.UiAutomation"},
                                                            {"method", "connect"},
                                                            {"line", 241}},
                                                           {{"class", "private.application"},
                                                            {"method", "secret"}}})}}})}}}};
    const auto value = observer_failure_summary(hello);
    require(value.find("reason=observer_unavailable") != std::string::npos);
    require(value.find("InvocationTargetException") != std::string::npos);
    require(value.find("automation_service_already_registered") != std::string::npos);
    require(value.find("android.app.UiAutomation.connect:241") != std::string::npos);
    require(value.find("private") == std::string::npos &&
            value.find("secret") == std::string::npos);
    hello["reason"] = "private\nrequest";
    hello["error_type"] = Json::array();
    hello["failure"]["causes"][0]["code"] = "private_payload";
    const auto invalid = observer_failure_summary(hello);
    require(invalid.find("reason=unknown") != std::string::npos &&
            invalid.find("error_type=unknown") != std::string::npos);
    require(invalid.find("private") == std::string::npos);

    for (const auto &malformed : {Json(), Json::array(), Json(123)}) {
        require(!observer_failure_summary(malformed).empty());
    }

    hello["failure"]["causes"] = Json::array();

    for (int i = 0; i < 100; ++i) {
        hello["failure"]["causes"].push_back({{"code", "unclassified"}, {"frames", Json::array()}});
    }

    require(observer_failure_summary(hello).size() < 1000);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
