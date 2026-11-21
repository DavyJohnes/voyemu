#ifdef NDEBUG
#undef NDEBUG
#endif
#include "api.hpp"
#include "guest_qnx_fake.hpp"
#include "os/platform.hpp"
#include "profile_catalog.hpp"
#include <cassert>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using namespace voyemu;
namespace {
struct Owned {
    std::filesystem::path root;
    std::string profile =
            embedded_profile_names(ProfilePlatform::qinggan09, ProfileBoard::sa8155).at(0);
    std::string id = random_token(), owner = random_token();

    explicit Owned(const std::filesystem::path &base)
        : root(base / ("guest-qnx-" + random_token())) {
        const auto directory = root / ".local/machines" / profile / "sessions" / id;
        ensure_private_directory(directory);
        write_private_json(root / ".local/machines" / profile / "active" / (profile + ".json"),
                           {{"schema", 1},
                            {"session_id", id},
                            {"directory", directory.string()},
                            {"endpoint", "owned-guest-qnx-fixture"}});
        write_private_json(directory / "automation-owner.json",
                           {{"schema", 1}, {"session_id", id}, {"ownership_id", owner}});
    }

    Json request(const char *op) const {
        return {{"schema", 1},
                {"request_id", random_token()},
                {"op", op},
                {"profile", profile},
                {"session", {{"id", id}, {"ownership_id", owner}}}};
    }
};

Json lookup(api::Api &instance, const Json &response) {
    return instance.call({{"schema", 1},
                          {"request_id", random_token()},
                          {"op", "operation.get"},
                          {"target", {{"operation_id", response.at("operation_id")}}}});
}

void lifecycle(const std::filesystem::path &base) {
    const Owned owned(base);
    auto qnx = std::make_shared<api::FakeGuestQnx>();
    std::vector<Json> supervisor;
    api::Transports transports;

    transports.qnx = qnx;
    transports.control = [&](const auto &, const Json &operation, int) -> Json {
        supervisor.push_back(operation);
        throw std::runtime_error("IPC_CONNECT: fixture has no supervisor");
    };
    api::Api instance(owned.root, "unused", transports);

    auto reboot = owned.request("cluster.reboot");
    const auto unguarded = instance.call(reboot);
    assert(unguarded.at("error").at("code") == "TEST_ARGUMENT" && qnx->calls.empty());

    reboot["request_id"] = random_token();
    reboot["expected_generation"] = 1;
    const auto rebooted = instance.call(reboot);
    assert(rebooted.at("ok") == true && qnx->generation == 2 && qnx->running);
    assert(rebooted.at("result").at("deliveries").at("qnx").at("generation") == 2);
    assert((qnx->calls == std::vector<std::string>{"lifecycle_supported", "restart"}));
    // Durable admission replays the receipt instead of rebooting the cluster again.
    assert(instance.call(reboot) == rebooted && qnx->generation == 2 && qnx->calls.size() == 2);
    assert(lookup(instance, rebooted).at("result").at("outcome") == "completed");

    auto off = owned.request("cluster.power");
    off["expected_generation"] = 2;
    off["enabled"] = false;
    assert(instance.call(off).at("ok") == true && !qnx->running);
    auto on = owned.request("cluster.power");
    on["expected_generation"] = 2;
    on["enabled"] = true;
    const auto powered = instance.call(on);
    assert(powered.at("ok") == true && qnx->running && qnx->generation == 3);
    assert(powered.at("result").at("deliveries").at("qnx").at("status") == "running");

    // Without a prepared lifecycle only power-off reaches the guest.
    qnx->supported = false;
    qnx->calls.clear();
    auto unprepared = owned.request("cluster.reboot");
    unprepared["expected_generation"] = 3;
    assert(instance.call(unprepared).at("error").at("code") == "TEST_ARGUMENT");
    on["request_id"] = random_token();
    assert(instance.call(on).at("error").at("code") == "TEST_ARGUMENT");
    assert((qnx->calls == std::vector<std::string>{"lifecycle_supported", "lifecycle_supported"}));
    off["request_id"] = random_token();
    assert(instance.call(off).at("ok") == true && !qnx->running &&
           qnx->calls.back() == "power_off");
    qnx->supported = true;
    qnx->running = true;

    auto foreign = owned.request("cluster.reboot");
    foreign["expected_generation"] = 3;
    foreign["session"]["ownership_id"] = random_token();
    qnx->calls.clear();
    assert(instance.call(foreign).at("error").at("code") == "TEST_OWNERSHIP" && qnx->calls.empty());
    assert(supervisor.empty());
    std::filesystem::remove_all(owned.root);
}

// A flush the guest may already be executing must not be reported as not started: a client may
// retry not_started under a new request ID while /var is already read-only.
void flush_outcome(const std::filesystem::path &base) {
    const Owned owned(base);
    auto qnx = std::make_shared<api::FakeGuestQnx>();
    api::Transports transports;

    transports.qnx = qnx;
    api::Api instance(owned.root, "unused", transports);
    auto reboot = owned.request("cluster.reboot");
    reboot["expected_generation"] = 1;

    qnx->failure = "INTERGUEST_FLUSH_UNKNOWN: guest sync deadline";
    const auto published = instance.call(reboot);
    const auto &failure = published.at("error");
    assert(failure.at("code") == "INTERGUEST_FLUSH_UNKNOWN");
    assert(failure.at("outcome") == "unknown" && failure.at("retryable") == false);
    assert(lookup(instance, published).at("result").at("outcome") == "unknown");

    auto power = owned.request("cluster.power");
    power["expected_generation"] = 1;
    power["enabled"] = false;
    qnx->failure = "INTERGUEST_FLUSH_UNKNOWN: guest quiesce failed";
    assert(instance.call(power).at("error").at("outcome") == "unknown");

    // Rejection before the request is published keeps not_started.
    reboot["request_id"] = random_token();
    qnx->failure = "INTERGUEST_FLUSH: owned relay unavailable";
    const auto unpublished = instance.call(reboot).at("error");
    assert(unpublished.at("code") == "INTERGUEST_FLUSH" &&
           unpublished.at("outcome") == "not_started");
    std::filesystem::remove_all(owned.root);
}

// A new *_UNKNOWN guest failure code is an unknown outcome without listing it in the API; the same
// code without the suffix stays not_started.
void unknown_suffix(const std::filesystem::path &base) {
    const Owned owned(base);
    auto qnx = std::make_shared<api::FakeGuestQnx>();
    api::Transports transports;

    transports.qnx = qnx;
    api::Api instance(owned.root, "unused", transports);
    auto reboot = owned.request("cluster.reboot");
    reboot["expected_generation"] = 1;
    qnx->failure = "QNX_FIXTURE_UNKNOWN: guest effect unverified";
    const auto unknown = instance.call(reboot).at("error");
    assert(unknown.at("code") == "QNX_FIXTURE_UNKNOWN" && unknown.at("outcome") == "unknown" &&
           unknown.at("retryable") == false);
    reboot["request_id"] = random_token();
    qnx->failure = "QNX_FIXTURE: guest rejected before effect";
    assert(instance.call(reboot).at("error").at("outcome") == "not_started");
    reboot["request_id"] = random_token();
    qnx->failure = "QNX_FIXTURE_UNKNOWN_STATE: guest rejected before effect";
    assert(instance.call(reboot).at("error").at("outcome") == "not_started");
    std::filesystem::remove_all(owned.root);
}

// Cluster scene, diagnostics and frame reads go through GuestQnx; the supervisor only reports
// session status.
void observations(const std::filesystem::path &base) {
    const Owned owned(base);
    auto qnx = std::make_shared<api::FakeGuestQnx>();
    api::Transports transports;

    transports.qnx = qnx;
    transports.control = [&](const auto &, const Json &operation, int) -> Json {
        assert(operation.at("op") == "status");

        return {{"ok", true},
                {"session_id", owned.id},
                {"stopped", false},
                {"android_pid", 4242},
                {"qnx_pid", qnx->running ? 99 : 0},
                {"revision", 1},
                {"deliveries", {{"qnx", {{"generation", qnx->generation}}}}}};
    };
    api::Api instance(owned.root, "unused", transports);
    const auto cluster_ui = [&] {
        auto request = owned.request("session.ui");
        request["screens"] = {"cluster"};
        request["timeout_ms"] = 1000;
        const auto response = instance.call(request);
        assert(response.at("ok") == true);

        return response.at("result").at("cluster");
    };
    const auto scene = cluster_ui();
    assert(scene.at("status") == "ok" && scene.at("qnx_generation") == 1 &&
           scene.at("nodes").at(0).at("id") == "1" && scene.at("source") == "qnx.kanzi");
    qnx->failure = "IPC_OPERATION: unknown supervisor operation";
    assert(cluster_ui().at("error").at("code") == "TEST_UNSUPPORTED");
    qnx->nodes = Json::array();
    assert(cluster_ui().at("error").at("code") == "CLUSTER_UI_PROTOCOL");
    qnx->running = false;
    assert(cluster_ui().at("error").at("code") == "CLUSTER_UI_NOT_READY");
    qnx->running = true;
    qnx->nodes = Json::array({{{"id", 1u}, {"parent", nullptr}}});

    const auto system = [&] {
        auto request = owned.request("cluster.system");
        request["timeout_ms"] = 1000;

        return instance.call(request);
    };
    const auto diagnostics = system().at("result");
    assert(diagnostics.at("status") == "partial" && diagnostics.at("qnx_generation") == 1 &&
           diagnostics.at("gauge").at("node_count") == 1 &&
           diagnostics.at("gauge").at("guest_pid") == 4097);
    qnx->failure = "IPC_TIMEOUT_UNKNOWN: observation deadline";
    assert(system().at("result").at("reason") == "observation_deadline_elapsed");
    qnx->running = false;
    const auto off = system().at("result");
    assert(off.at("status") == "unavailable" && off.at("reason") == "cluster_off");
    qnx->running = true;
    qnx->nodes = Json::array();
    assert(system().at("error").at("code") == "CLUSTER_SYSTEM_PROTOCOL");

    const auto screenshot = [&](int timeout) {
        auto request = owned.request("session.screenshot");
        request["screens"] = {"cluster"};
        request["timeout_ms"] = timeout;
        request["artifact_dir"] = (owned.root / ".local/build/tmp" / random_token()).string();

        return instance.call(request);
    };
    qnx->exported = frames::Frame{.sequence = 5, .timestamp_ns = 77};
    qnx->exported->bgra.resize(frames::pixel_bytes);
    const auto captured = screenshot(1000).at("result");
    assert(captured.at("accepted") == true && captured.at("qnx_frame_verified") == true &&
           captured.at("frames").at(0).at("screen") == "cluster" &&
           captured.at("frames").at(0).at("sequence") == "5");
    qnx->exported->bgra.resize(16);
    assert(screenshot(1000).at("error").at("code") == "TEST_FRAME");
    qnx->exported.reset();
    assert(screenshot(50).at("error").at("code") == "TEST_FRAME_TIMEOUT");
    std::filesystem::remove_all(owned.root);
}

// The supervisor reports a stop that failed after lifecycle side effects as SESSION_STOP_UNKNOWN.
// Windows probes the named pipe before dispatch, so this fixture endpoint takes recovery there.
void stop_outcome(const std::filesystem::path &base) {
    const Owned owned(base);
    std::string reply;
    api::Transports transports;

    transports.qnx = std::make_shared<api::FakeGuestQnx>();
    transports.control = [&](const auto &, const Json &operation, int) -> Json {
        assert(operation.at("op") == "stop");

        return {{"ok", false}, {"session_id", owned.id}, {"error", reply}};
    };
    api::Api instance(owned.root, "unused", transports);
    reply = "SESSION_STOP_UNKNOWN: INTERGUEST_FLUSH_UNKNOWN: guest sync deadline";
    const auto partial = instance.call(owned.request("session.stop"));
    assert(partial.at("error").at("code") == "SESSION_STOP_UNKNOWN");
    assert(partial.at("error").at("outcome") == "unknown" &&
           partial.at("error").at("retryable") == false);
    assert(lookup(instance, partial).at("result").at("outcome") == "unknown");
    reply = "ANDROID_IDENTITY_MISMATCH: stop rejected before lifecycle side effects";
    assert(instance.call(owned.request("session.stop")).at("error").at("outcome") == "not_started");
    std::filesystem::remove_all(owned.root);
}
} // namespace

int main(int argc, char **argv) try {
    assert(argc == 2);
    const std::filesystem::path base = argv[1];
    ensure_private_directory(base);
    lifecycle(base);
    flush_outcome(base);
    unknown_suffix(base);
    observations(base);

    if (!os::host_windows) {
        stop_outcome(base);
    }

    std::cout << "QNX cluster API lifecycle and observations dispatch through GuestQnx; *_UNKNOWN "
                 "and uncertain flush and stop outcomes stay unknown\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
