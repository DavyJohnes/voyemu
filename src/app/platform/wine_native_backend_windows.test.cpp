#include "../core/model.hpp"
#include "../core/process.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include "../runtime/media_discovery.hpp"
#include "wine_native_backend_windows.hpp"
#include "wine_native_proof_reader.hpp"

#include <windows.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace voyemu;

static void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

#ifdef _WIN32
int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "_wine-native") {
            return wine_native_proxy_main(argc - 1, argv + 1);
        }

        if (argc > 1 && std::string(argv[1]) == "_windows-child") {
            Sleep(200);

            return 9;
        }

        if (argc == 5 && std::string(argv[1]) == "_native-parent") {
            ProcessSpec owned;
            owned.executable = argv[4];
            owned.directory = argv[3];
            owned.output = owned.directory / "parent-native.log";
            owned.arguments = {"--listen", "49284", "argument with spaces"};
            owned.environment["VOYEMU_DIAG_PATH"] = owned.executable.string();
            owned.wine_native_diagnostic = WineNativeDiagnostic{
                    argv[2], owned.directory / "parent-requests", {49284}, 30000};
            auto native = ChildProcess::launch(owned);
            write_private_json(owned.directory / "parent-ready.json",
                               {{"native_pid", native.pid()}});
            Sleep(30000);

            return 1;
        }

        check(argc == 4, "launcher root native-helper required");
        const std::filesystem::path root = argv[2];
        ensure_private_directory(root);
        const auto snapshot = root / "atomic-proof.json",
                   replacement = root / "atomic-proof-next.json";
        write_private_json(snapshot, {{"nonce", "original"}});
        {
            detail::WineNativeProofReader retained(snapshot);
            write_private_json(replacement, {{"nonce", "replacement"}});
            using Spawn = LONG(WINAPI *)(char *const *, int);
            auto spawn = reinterpret_cast<Spawn>(
                    GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "__wine_unix_spawnvp"));
            check(spawn, "native atomic publisher available");
            std::vector<std::string> args = {"/bin/mv", "-f", wine_native_unix_path(replacement),
                                             wine_native_unix_path(snapshot)};
            std::vector<char *> pointers;

            for (auto &arg : args) {
                pointers.push_back(arg.data());
            }

            pointers.push_back(nullptr);
            check(spawn(pointers.data(), 1) == 0, "native replace retained snapshot");
            check(Json::parse(retained.text()).at("nonce") == "original",
                  "unlinked retained snapshot remains readable");
        }
        check(Json::parse(detail::WineNativeProofReader(snapshot).text()).at("nonce") ==
                      "replacement",
              "new incarnation independently read");
        check(CreateHardLinkW(replacement.c_str(), snapshot.c_str(), nullptr), "hardlink fixture");
        bool alias_rejected = false;
        try {
            detail::WineNativeProofReader reader(snapshot);
            reader.text();
        } catch (const std::exception &) {
            alias_rejected = true;
        }

        check(alias_rejected, "aliased native snapshot rejected");
        std::filesystem::remove(replacement);
        write_private_json(snapshot, Json::object());
        { std::ofstream(snapshot) << std::string(16385, 'x'); }
        bool extent_rejected = false;
        try {
            detail::WineNativeProofReader reader(snapshot);
            reader.text();
        } catch (const std::exception &) {
            extent_rejected = true;
        }

        check(extent_rejected, "oversize native snapshot rejected");
        bool directory_rejected = false;
        try {
            detail::WineNativeProofReader reader(root);
            reader.text();
        } catch (const std::exception &) {
            directory_rejected = true;
        }

        check(directory_rejected, "directory snapshot rejected");
        const auto descriptor = root / "pid_98762.ini", discovery_log = root / "discovery.log";
        {
            std::ofstream(descriptor)
                    << "grpc.port=49286\nport.serial=5574\ngrpc.token=private-fixture\n";
            std::ofstream(discovery_log)
                    << "Advertising in: " << wine_native_unix_path(descriptor) << '\n';
        }
        check(!sdk_discovery(discovery_log, root, 98762),
              "ordinary Windows rejects Unix advertisement");
        check(sdk_discovery(discovery_log, root, 98762, true) == descriptor,
              "native Unix advertisement accepted");
        check(!sdk_discovery(discovery_log, root, 98763, true), "native advertisement PID fence");
        ensure_private_directory(root / "wrong-root");
        check(!sdk_discovery(discovery_log, root / "wrong-root", 98762, true),
              "native advertisement root fence");
        ensure_private_directory(root / "sdk-private");
        SdkDescriptorCache discovery_cache;
        check(discovery_cache
                      .resolve(discovery_log, root, root / "sdk-private", 98762, 5574, 49286, false,
                               true)
                      .has_value(),
              "native descriptor authenticated copy");
        { std::ofstream(discovery_log) << "Advertising in: " << descriptor.string() << '\n'; }
        check(sdk_discovery(discovery_log, root, 98762) == descriptor,
              "ordinary Windows advertisement unchanged");
        check(wine_native_unix_path(std::filesystem::path(argv[3])).starts_with("/") &&
                      wine_native_unix_path(std::filesystem::path(argv[3]))
                              .ends_with("/native-test-helper"),
              "exported native path conversion");
        ProcessSpec spec;
        spec.executable = argv[3];
        spec.directory = root;
        spec.output = root / "native.log";
        spec.arguments = {"--sleep", "500"};
        spec.wine_native_diagnostic = WineNativeDiagnostic{argv[1], root / "requests", {}, 5000};
        const auto ram_path = root / "qnx RAM.bin", system_path = root / "persistent system.img",
                   user_path = root / "persistent user.img";
        spec.arguments.insert(
                spec.arguments.end(),
                {"memory-backend-file,id=low,size=1128M,mem-path=" + ram_path.string() +
                         ",offset=0,share=on",
                 "memory-backend-file,id=user,size=32M,mem-path=" + user_path.string() +
                         ",share=on",
                 "memory-backend-file,id=disk,size=1025M,mem-path=" + system_path.string() +
                         ",share=on",
                 "memory-backend-file,id=qnxmem,size=8192M,mem-path=" + ram_path.string() +
                         ",share=on",
                 "unchanged,mem-path=C:/not-a-QEMU-backend",
                 "memory-backend-file,id=user,size=32M,mem-path=/already/unix,share=on"});
        auto child = ChildProcess::launch(spec);
        check(child.pid() > 1 && child.pid_namespace() == "macos-diagnostic",
              "native PID namespace");
        Json recorded;

        for (const auto &entry : std::filesystem::directory_iterator(root / "requests")) {
            if (std::filesystem::exists(entry.path() / "request.json")) {
                recorded = read_private_json(entry.path() / "request.json");
            }
        }

        const auto &arguments = recorded.at("arguments");
        const bool mapped =
                arguments.at(2) == "memory-backend-file,id=low,size=1128M,mem-path=" +
                                           wine_native_unix_path(ram_path) + ",offset=0,share=on" &&
                arguments.at(3) == "memory-backend-file,id=user,size=32M,mem-path=" +
                                           wine_native_unix_path(user_path) + ",share=on" &&
                arguments.at(4) == "memory-backend-file,id=disk,size=1025M,mem-path=" +
                                           wine_native_unix_path(system_path) + ",share=on" &&
                arguments.at(5) == "memory-backend-file,id=qnxmem,size=8192M,mem-path=" +
                                           wine_native_unix_path(ram_path) + ",share=on" &&
                arguments.at(6) == spec.arguments.at(6) && arguments.at(7) == spec.arguments.at(7);

        if (!mapped) {
            child.terminate(1000);
            throw std::runtime_error("persistent NUMA memory path conversion differs");
        }

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (child.alive() && std::chrono::steady_clock::now() < deadline) {
            Sleep(20);
        }

        check(!child.alive() && child.exit_code() == 7, "native normal exit");
        Sleep(2200);
        check(child.exit_code() == 7, "verified terminal exit retained");
        auto delayed = ChildProcess::launch(spec);
        Sleep(2800);
        check(!delayed.alive() && delayed.exit_code() == 7, "late first terminal poll");
        delayed.terminate(1000);
        auto delayed_stop = ChildProcess::launch(spec);
        Sleep(2800);
        delayed_stop.terminate(1000);
        check(delayed_stop.exit_code() == 7, "late first terminal stop");
        spec.environment = {{"VOYEMU_DIAG_PATH", spec.executable.string()}, {"PATH", "C:\\bad"}};
        spec.arguments = {"--listen", "49283", "argument with spaces"};
        spec.wine_native_diagnostic->ports = {49283};
        auto listener = ChildProcess::launch(spec);
        const auto listen_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);

        while (!listener.owns_tcp_listener(49283) &&
               std::chrono::steady_clock::now() < listen_deadline) {
            Sleep(20);
        }

        check(listener.owns_tcp_listener(49283) && !listener.owns_tcp_listener(49284),
              "native listener ownership");
        std::filesystem::path proof;

        for (const auto &entry : std::filesystem::directory_iterator(root / "requests")) {
            auto candidate = entry.path() / "proof.json";

            if (std::filesystem::exists(candidate)) {
                auto p = read_private_json(candidate);

                if (p.at("native_pid") == listener.pid()) {
                    proof = candidate;
                }
            }
        }

        check(!proof.empty(), "owned proof found");
        auto p = read_private_json(proof);
        p["nonce"] = "00000000000000000000000000000000";
        write_private_json(proof, p);
        check(!listener.owns_tcp_listener(49283), "nonce fail closed");
        Sleep(200);
        check(listener.owns_tcp_listener(49283), "proof recovery");
        p = read_private_json(proof);
        p["observed_utc_ms"] = 0;
        write_private_json(proof, p);
        check(!listener.owns_tcp_listener(49283), "stale proof fail closed");
        Sleep(200);
        listener.terminate(2000);
        check(!listener.alive(), "native stopped");
        std::wstring current(32768, L'\0');
        auto length = GetModuleFileNameW(nullptr, current.data(),
                                         static_cast<DWORD>(current.size()));
        current.resize(length);
        ProcessSpec ordinary;
        ordinary.executable = current;
        ordinary.directory = root;
        ordinary.output = root / "ordinary.log";
        ordinary.arguments = {"_windows-child"};
        auto windows = ChildProcess::launch(ordinary);
        check(windows.pid_namespace() == "windows", "ordinary Windows namespace");

        while (windows.alive()) {
            Sleep(20);
        }

        check(windows.exit_code() == 9, "ordinary Windows exit");
        ProcessSpec parent = ordinary;
        parent.arguments = {"_native-parent", argv[1], root.string(), argv[3]};
        auto owner = ChildProcess::launch(parent);
        const auto ready = root / "parent-ready.json";
        const auto parent_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (!std::filesystem::exists(ready) && owner.alive() &&
               std::chrono::steady_clock::now() < parent_deadline) {
            Sleep(20);
        }

        check(std::filesystem::exists(ready), "parent owns native child");
        owner.terminate(1000);
        bool reaped = false;
        const auto reap_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (!reaped && std::chrono::steady_clock::now() < reap_deadline) {
            for (const auto &entry :
                 std::filesystem::directory_iterator(root / "parent-requests")) {
                try {
                    auto terminal = read_private_json(entry.path() / "proof.json");
                    reaped = terminal.at("alive") == false && terminal.at("state") == "terminal" &&
                             terminal.at("terminal_reason") == "parent_exit";
                } catch (const std::exception &) {}
            }

            Sleep(20);
        }

        check(reaped, "owned PE parent death reaps native child");
        std::cout
                << "PASS native PID, listener, nonce, freshness, stop, memory rejection, ordinary Windows, PE parent death, native SDK discovery fences\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
#else
int main(int argc, char **argv) {
    if (argc < 3) {
        return 64;
    }

    if (std::string(argv[1]) == "--sleep") {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(argv[2])));

        return 7;
    }

    if (argc != 4 || std::string(argv[1]) != "--listen" ||
        std::string(argv[3]) != "argument with spaces") {
        return 65;
    }

    auto path = std::getenv("VOYEMU_DIAG_PATH");

    if (!path || path[0] != '/' ||
        (std::getenv("PATH") && std::string(std::getenv("PATH")).starts_with("C:"))) {
        return 67;
    }

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(std::stoi(argv[2]));

    if (bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) || listen(fd, 1)) {
        return 66;
    }

    std::this_thread::sleep_for(std::chrono::seconds(30));
    close(fd);

    return 0;
}
#endif
