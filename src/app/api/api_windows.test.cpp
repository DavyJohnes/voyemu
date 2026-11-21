#include "api.hpp"
#include "api_test_host.hpp"
#include "guest_android_fake.hpp"
#include "model.hpp"
#include "platform_file.hpp"
#include "profile_catalog.hpp"
#include "session.hpp"
#include "session_recovery.hpp"
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <wincodec.h>

#include "os/private_state_security.hpp"
using namespace voyemu;

void image_dimensions(const std::filesystem::path &path, std::size_t width, std::size_t height) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    assert(SUCCEEDED(initialized) || initialized == RPC_E_CHANGED_MODE);
    IWICImagingFactory *factory = nullptr;
    assert(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IWICImagingFactory,
                                      reinterpret_cast<void **>(&factory))));
    IWICBitmapDecoder *decoder = nullptr;
    assert(SUCCEEDED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnLoad, &decoder)));
    IWICBitmapFrameDecode *frame = nullptr;
    assert(SUCCEEDED(decoder->GetFrame(0, &frame)));
    UINT w = 0, h = 0;
    assert(SUCCEEDED(frame->GetSize(&w, &h)) && w == width && h == height);
    frame->Release();
    decoder->Release();
    factory->Release();

    if (SUCCEEDED(initialized)) {
        CoUninitialize();
    }
}

void owned_recovery_tests(const std::filesystem::path &base, const std::filesystem::path &self) {
    const auto root = base / ("test-owned-recovery-" + random_token());

    ensure_private_directory(root);
    const std::string profile = qinggan09_profile(), id = random_token(),
                      ownership = random_token();
    const auto directory = root / ".local/machines" / profile / "sessions" / id,
               active = root / ".local/machines" / profile / "active" / (profile + ".json"),
               scoped = root / ".local/machines" / profile;
    ensure_private_directory(directory);
    ensure_private_directory(scoped);
    auto put = [](const auto &file) {
        ensure_private_directory(file.parent_path());
        std::ofstream out(file);
        out << "fixture";
    };
    const auto sdk_root = root / "sdk", sdk = android_sdk_program(sdk_root, true),
               frontend = root / "voyemu.exe", wrapper = sdk_root / "emulator/emulator.exe";
    put(sdk);
    put(frontend);
    put(wrapper);
    const unsigned port = 5626, grpc = 42626;
    const auto endpoint = "\\\\.\\pipe\\voyemu-" + id;
    const Json selection{{"schema", 1},
                         {"session_id", id},
                         {"profile", profile},
                         {"directory", directory.string()},
                         {"endpoint", endpoint}};
    const Json owner{
            {"schema", 1}, {"session_id", id}, {"profile", profile}, {"ownership_id", ownership}};
    const Json supervisor{{"schema", 1},
                          {"session_id", id},
                          {"directory", directory.string()},
                          {"origin", "voyemu-native-supervisor"},
                          {"creator_pid", 0xfffffff0u}};
    const std::string source_pin(64, 'f');
    const auto runtime_root = root / ".local/machines" / profile / "portable" /
                              ("portable-" + source_pin);
    ensure_private_directory(runtime_root / "android-machine/base");
    const Json launch{
            {"machine", (runtime_root / "android-machine").string()},
            {"base", (runtime_root / "android-machine/base").string()},
            {"android_sdk_root", sdk_root.string()},
            {"pins", Json::array({{{"path", sdk.string()}, {"sha256", sha256_file(sdk)}},
                                  {{"path", wrapper.string()}, {"sha256", sha256_file(wrapper)}}})},
            {"port", port},
            {"sdk_control", {{"grpc_port", grpc}}},
            {"executable", wrapper.string()},
            {"adb_transport", "direct"},
            {"frontend", {{"executable", frontend.string()}}}};
    const Json runtime{{"schema", 1},
                       {"session_id", id},
                       {"endpoint", endpoint},
                       {"android_pid", 0},
                       {"qnx_pid", 0},
                       {"stopped", false},
                       {"deliveries",
                        {{"android", {{"generation", 1}, {"status", "booting"}}},
                         {"qnx", {{"status", "not_started"}}}}}};
    write_private_json(active, selection);
    write_private_json(directory / "automation-owner.json", owner);
    write_private_json(directory / "session-owner.json", supervisor);
    write_private_json(directory / "runtime.json", runtime);
    write_private_json(scoped / "android-launch.json", launch);
    const Json activation{{"profile", profile},
                          {"manifest_sha256", source_pin},
                          {"runtime", runtime_root.string()},
                          {"android_launch_sha256", sha256_file(scoped / "android-launch.json")}};
    write_private_json(scoped / "activation.json", activation);
    auto refused = [&] {
        bool rejected = false;
        try {
            api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id,
                                           ownership);
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected && read_private_json(directory / "runtime.json") == runtime);
    };
    {
        auto bad = owner;
        bad["ownership_id"] = random_token();
        write_private_json(directory / "automation-owner.json", bad);
        refused();
        write_private_json(directory / "automation-owner.json", owner);
    }
    {
        auto bad = runtime;
        bad["android_pid"] = GetCurrentProcessId();
        write_private_json(directory / "runtime.json", bad);
        bool rejected = false;
        try {
            api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id,
                                           ownership);
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected && read_private_json(directory / "runtime.json") == bad);
        write_private_json(directory / "runtime.json", runtime);
    }
    {
        auto bad = supervisor;
        bad["android_processes"] = {{"generation", 1}, {"phase", "launch_pending"}};
        write_private_json(directory / "session-owner.json", bad);
        refused();
        write_private_json(directory / "session-owner.json", supervisor);
    }
    {
        auto bad = activation;
        bad["android_launch_sha256"] = std::string(64, '0');
        write_private_json(scoped / "activation.json", bad);
        refused();
        write_private_json(scoped / "activation.json", activation);
    }
    {
        const auto pipe = CreateNamedPipeW(std::filesystem::path(endpoint).c_str(),
                                           PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096,
                                           4096, 0, nullptr);
        assert(pipe != INVALID_HANDLE_VALUE);
        refused();
        CloseHandle(pipe);
    }
    WSADATA data{};
    assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);

    for (const auto family : {AF_INET, AF_INET6}) {
        const auto listener = socket(family, SOCK_STREAM, IPPROTO_TCP);
        assert(listener != INVALID_SOCKET);
        BOOL yes = TRUE;
        assert(setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                          reinterpret_cast<const char *>(&yes), sizeof(yes)) == 0);

        if (family == AF_INET) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_port = htons(port);
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            assert(bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ==
                   0);
        } else {
            assert(setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY,
                              reinterpret_cast<const char *>(&yes), sizeof(yes)) == 0);
            sockaddr_in6 address{};
            address.sin6_family = AF_INET6;
            address.sin6_port = htons(port);
            address.sin6_addr = in6addr_loopback;
            assert(bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ==
                   0);
        }

        assert(listen(listener, 1) == 0);
        refused();
        closesocket(listener);
    }

    WSACleanup();
    {
        api::FileLock lock(root / ".local/machines" / profile / "active" /
                           (profile + ".supervisor.lock"));
        refused();
    }
    {
        // The launcher is absent and runtime has no backend PID. A backend-only
        // child with differently cased Windows spelling must still block recovery.
        std::filesystem::remove(sdk);
        const auto upper = sdk.parent_path() / "QEMU-SYSTEM-AARCH64.EXE";
        std::filesystem::copy_file(self, upper);
        auto backend_launch = launch;
        backend_launch["pins"][0]["sha256"] = sha256_file(sdk);
        write_private_json(scoped / "android-launch.json", backend_launch);
        auto backend_activation = activation;
        backend_activation["android_launch_sha256"] = sha256_file(scoped / "android-launch.json");
        write_private_json(scoped / "activation.json", backend_activation);
        ProcessSpec child;
        child.executable = upper;
        child.arguments = {"--recovery-child"};
        child.directory = root;
        child.output = root / "backend-child.log";
        auto retained = ChildProcess::launch(child);
        assert(retained.alive());
        refused();
        retained.terminate(100);
        assert(!retained.alive());
        std::filesystem::remove(upper);
        put(sdk);
        write_private_json(scoped / "android-launch.json", launch);
        write_private_json(scoped / "activation.json", activation);
    }
    {
        api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id, ownership);
        auto changed = runtime;
        changed["revision"] = 99;
        write_private_json(directory / "runtime.json", changed);
        bool rejected = false;
        try {
            lease.publish_terminal();
        } catch (const std::exception &) {
            rejected = true;
        }

        assert(rejected && read_private_json(directory / "runtime.json") == changed);
        write_private_json(directory / "runtime.json", runtime);
    }
    {
        api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id, ownership);

        for (const auto value : {port, port + 1, grpc}) {
            assert(!tcp_port_free(value));
        }

        const auto terminal = lease.publish_terminal();
        assert(terminal.at("stopped") == true &&
               terminal.at("deliveries")
                               .at("android")
                               .at("shutdown")
                               .at("guest_shutdown_unverified") == true);
    }
    {
        write_private_json(directory / "runtime.json", runtime);
        unsigned dispatched = 0;
        api::Transports absent_transport;
        absent_transport.control = [&](const auto &, const Json &, int) -> Json {
            ++dispatched;
            throw std::runtime_error(
                    "TEST_CONTROL_DISPATCHED: absent endpoint must recover before IPC");
        };
        api::Api absent_api(root, self, absent_transport);
        const Json request{{"schema", 1},
                           {"request_id", random_token()},
                           {"op", "session.stop"},
                           {"profile", profile},
                           {"session", {{"id", id}, {"ownership_id", ownership}}}};
        const auto recovered = absent_api.call(request);
        assert(recovered.at("ok") == true && dispatched == 0);
        const auto &result = recovered.at("result");
        assert(result.at("accepted") == true && result.at("stopped") == true &&
               result.at("cleanup").at("guest_shutdown_unverified") == true &&
               result.at("host_recovery").at("guest_shutdown_unverified") == true);
    }

    for (const auto value : {port, port + 1, grpc}) {
        assert(tcp_port_free(value));
    }

    {
        // A recorded process receipt names the adbd port and no adb server.
        auto direct_owner = supervisor;
        direct_owner["android_processes"] = {{"phase", "recorded"},  {"generation", 1},
                                             {"sdk_pid", 0},         {"frontend_pid", 0},
                                             {"console_port", port}, {"grpc_port", grpc},
                                             {"adbd_port", port + 1}};
        write_private_json(directory / "session-owner.json", direct_owner);
        write_private_json(directory / "runtime.json", runtime);
        {
            api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id,
                                           ownership);
            lease.verify();

            for (const auto value : {port, port + 1, grpc}) {
                assert(!tcp_port_free(value));
            }
        }
        auto inconsistent = direct_owner;
        inconsistent["android_processes"]["adb_pid"] = 0;
        write_private_json(directory / "session-owner.json", inconsistent);
        refused();
        write_private_json(directory / "session-owner.json", supervisor);
    }
    {
        // Ordinary runtime configurations intentionally have no persisted host
        // executable hashes. Maintenance still requires the exact seed and all
        // existing process, owner, endpoint and listener fences.
        write_private_json(directory / "runtime.json", runtime);
        const auto seed = root / "machines" / profile;
        ensure_private_directory(seed / "android");
        const Json manifest{
                {"schema", 1},
                {"kind", "portable-guest-runtime"},
                {"profile", profile},
                {"android",
                 {{"api", 28}, {"abi", "arm64-v8a"}, {"base_archive_sha256", source_pin}}}};
        write_private_json(seed / "runtime.json", manifest);
        auto configured_launch = launch;
        configured_launch["machine"] = (seed / "android").string();
        configured_launch["base"] = (seed / "android").string();
        configured_launch["executable"] = sdk.string();
        configured_launch["pins"] = Json::array();
        write_private_json(scoped / "android-launch.json", configured_launch);
        const Json configured{{"schema", 1},
                              {"kind", "runtime-configuration"},
                              {"profile", profile},
                              {"machine", ""},
                              {"seed", seed.string()},
                              {"manifest_sha256", sha256_file(seed / "runtime.json")},
                              {"integrity_audited", false}};
        write_private_json(scoped / "activation.json", configured);
        {
            api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id,
                                           ownership);
            lease.verify();

            for (const auto value : {port, port + 1, grpc}) {
                assert(!tcp_port_free(value));
            }
        }
        {
            auto bad = configured;
            const auto foreign = root / "machines/foreign";
            ensure_private_directory(foreign);
            bad["seed"] = foreign.string();
            write_private_json(scoped / "activation.json", bad);
            refused();
            write_private_json(scoped / "activation.json", configured);
        }
        {
            auto bad = configured;
            bad["manifest_sha256"] = std::string(64, '0');
            write_private_json(scoped / "activation.json", bad);
            refused();
            write_private_json(scoped / "activation.json", configured);
        }
        {
            auto bad = runtime;
            bad["android_pid"] = GetCurrentProcessId();
            write_private_json(directory / "runtime.json", bad);
            bool rejected = false;
            try {
                api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id,
                                               ownership);
            } catch (const std::exception &) {
                rejected = true;
            }

            assert(rejected);
            write_private_json(directory / "runtime.json", runtime);
        }
        {
            api::StoppedAndroidLease lease(root, profile, machine_identity(profile, {}), id,
                                           ownership);
            auto updated = configured_launch;
            updated["config_template"] = "source-refresh";
            write_private_json(scoped / "android-launch.json", updated);
            lease.verify_host_absence();
            bool rejected = false;
            try {
                lease.verify();
            } catch (const std::exception &) {
                rejected = true;
            }

            assert(rejected);
        }
        // A portable positional machine outside root/machines keeps its exact
        // registered seed/selector; recovery must not invent a root-relative seed.
        const auto external = root.parent_path() / ("external-recovery-" + random_token());
        ensure_private_directory(external / "android");
        write_private_json(external / "runtime.json", manifest);
        const auto external_name = machine_path_name(root, {external, profile});
        const auto external_scoped = profile_launch_directory(
                root, profile, voyemu::machine_identity(profile, external_name));
        ensure_private_directory(external_scoped);
        auto external_launch = configured_launch;
        external_launch["machine"] = (external / "android").string();
        external_launch["base"] = (external / "android").string();
        write_private_json(external_scoped / "android-launch.json", external_launch);
        auto external_activation = configured;
        external_activation["machine"] = external_name;
        external_activation["seed"] = external.string();
        external_activation["manifest_sha256"] = sha256_file(external / "runtime.json");
        write_private_json(external_scoped / "activation.json", external_activation);
        const Json external_selection{
                {"machine", external_name},
                {"seed", external.string()},
                {"manifest_sha256", external_activation.at("manifest_sha256")}};
        const auto external_directory = native_session_directory(
                root, profile, voyemu::machine_identity(profile, external_name), id);
        ensure_private_directory(external_directory);

        for (const auto &entry : std::filesystem::directory_iterator(directory)) {
            if (entry.is_regular_file()) {
                std::filesystem::copy_file(entry.path(),
                                           external_directory / entry.path().filename());
            }
        }

        write_private_json(external_directory / "machine-selection.json", external_selection);
        auto external_active = selection;
        external_active["directory"] = external_directory.string();
        const auto external_active_path =
                native_current_active_directory(root, profile,
                                                voyemu::machine_identity(profile, external_name)) /
                (profile + ".json");
        write_private_json(external_active_path, external_active);
        auto external_supervisor = supervisor;
        external_supervisor["directory"] = external_directory.string();
        write_private_json(external_directory / "session-owner.json", external_supervisor);
        {
            api::StoppedAndroidLease lease(root, profile, machine_identity(profile, external_name),
                                           id, ownership);
            lease.verify();
        }
        auto external_refused = [&] {
            bool rejected = false;
            try {
                api::StoppedAndroidLease lease(
                        root, profile, machine_identity(profile, external_name), id, ownership);
            } catch (const std::exception &) {
                rejected = true;
            }

            assert(rejected);
        };
        {
            auto bad = external_selection;
            bad["machine"] = "external-wrong";
            write_private_json(external_directory / "machine-selection.json", bad);
            external_refused();
            write_private_json(external_directory / "machine-selection.json", external_selection);
        }
        {
            auto bad = external_selection;
            bad["manifest_sha256"] = std::string(64, '0');
            write_private_json(external_directory / "machine-selection.json", bad);
            external_refused();
            write_private_json(external_directory / "machine-selection.json", external_selection);
        }
        {
            auto bad = external_activation;
            bad["seed"] = seed.string();
            write_private_json(external_scoped / "activation.json", bad);
            external_refused();
            write_private_json(external_scoped / "activation.json", external_activation);
        }
        std::filesystem::remove(external_active_path);
        std::filesystem::remove(external_directory / "machine-selection.json");
        std::filesystem::remove_all(external);
        write_private_json(scoped / "android-launch.json", launch);
        write_private_json(scoped / "activation.json", activation);
    }
    {
        api::Transports offline;
        offline.control = [](const auto &, const Json &, int) -> Json {
            throw std::runtime_error("IPC_CONNECT: exact fixture endpoint is absent");
        };
        api::Api recovery_api(root, frontend, offline);
        Json stop{{"schema", 1},
                  {"request_id", random_token()},
                  {"op", "session.stop"},
                  {"profile", profile},
                  {"session", {{"id", id}, {"ownership_id", ownership}}}};
        const auto stopped = recovery_api.call(stop);
        assert(stopped.at("ok") == true &&
               stopped.at("result").at("cleanup").at("verified") == true &&
               stopped.at("result").at("cleanup").at("guest_shutdown_unverified") == true);
        const auto present = CreateNamedPipeW(std::filesystem::path(endpoint).c_str(),
                                              PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1,
                                              4096, 4096, 0, nullptr);
        assert(present != INVALID_HANDLE_VALUE);
        offline.control = [](const auto &, const Json &, int) -> Json {
            throw std::runtime_error(
                    "TEST_SESSION: ownership rejection is not a transport failure");
        };
        api::Api rejected_api(root, frontend, offline);
        stop["request_id"] = random_token();
        const auto rejected = rejected_api.call(stop);
        assert(rejected.at("ok") == false && rejected.at("error").at("code") == "TEST_SESSION");
        CloseHandle(present);
    }
}

void wheel_release_admission_tests(const std::filesystem::path &) {}

ConnectedPipe::ConnectedPipe(const std::filesystem::path &endpoint) {
    const HANDLE server = CreateNamedPipeW(endpoint.c_str(), PIPE_ACCESS_DUPLEX,
                                           PIPE_TYPE_BYTE | PIPE_NOWAIT, 1, 4096, 4096, 0, nullptr);

    assert(server != INVALID_HANDLE_VALUE);
    const HANDLE client = CreateFileW(endpoint.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, 0, nullptr);
    assert(client != INVALID_HANDLE_VALUE);
    server_ = reinterpret_cast<std::intptr_t>(server);
    client_ = reinterpret_cast<std::intptr_t>(client);
}

ConnectedPipe::~ConnectedPipe() {
    close();
}

void ConnectedPipe::close() {
    for (auto *handle : {&client_, &server_}) {
        if (*handle != -1) {
            CloseHandle(reinterpret_cast<HANDLE>(*handle));
            *handle = -1;
        }
    }
}
