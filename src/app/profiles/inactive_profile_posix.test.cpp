#include "inactive_profile.hpp"
#include "inactive_profile_test_host.hpp"
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
using namespace voyemu;

int inactive_profile_case(int argc, char **argv) {
    if (argc == 4 && std::string(argv[1]) == "--hold-file") {
        const int fd = ::open(argv[2], O_RDONLY | O_CLOEXEC);

        if (fd < 0) {
            return 2;
        }

        {
            std::ofstream ready(argv[3]);
            ready << "ready";
        }
        std::this_thread::sleep_for(std::chrono::seconds(20));
        ::close(fd);

        return 0;
    }

    if (argc == 5 && std::string(argv[1]) == "--inspect-owned-inactive") {
        const std::filesystem::path root = argv[2];
        const std::string profile = argv[3];
        const auto machine = machine_identity(profile, "");
        const auto active = read_private_json(
                native_current_active_directory(root, profile, machine) / (profile + ".json"),
                4096);
        const auto runtime = read_private_json(
                std::filesystem::path(active.at("directory").get<std::string>()) / "runtime.json");
        const auto launch = read_private_json(argv[4]);
        InactiveProfileLease lease(root, profile, machine, active, runtime,
                                   launch.at("persistent_storage"));
        std::cout
                << "verified dead stale epoch under supervisor lock; metadata and disks unchanged\n";

        return 0;
    }

    const auto root = std::filesystem::canonical(std::filesystem::path("/tmp")) /
                      ("inactive-profile-" + random_token().substr(0, 12));
    ensure_private_directory(root);
    const std::string profile = "test-profile", id = random_token();
    const auto machine = machine_identity(profile, "");
    const auto directory = native_session_directory(root, profile, machine, id),
               active_dir = native_current_active_directory(root, profile, machine),
               socket_dir = root / "sockets";

    for (const auto &p : {directory, active_dir, socket_dir}) {
        ensure_private_directory(p);
    }

    const auto endpoint = socket_dir / (id + ".sock"),
               lock_path = active_dir / (profile + ".supervisor.lock"),
               ram = directory / "qnx-ram.bin", system = root / "system",
               userdata = root / "userdata";

    for (const auto &p : {lock_path, ram, system, userdata}) {
        std::ofstream f(p);
        f << "retained";
        std::filesystem::permissions(p, std::filesystem::perms::owner_read |
                                                std::filesystem::perms::owner_write);
    }

    ProcessSpec ended;
    ended.executable = "/bin/sh";
    ended.arguments = {"-c", "exit 0"};
    auto child = ChildProcess::launch(ended);

    while (child.alive()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto dead = child.pid();
    Json active{{"schema", 1},
                {"profile", profile},
                {"session_id", id},
                {"directory", directory.string()},
                {"endpoint", endpoint.string()}},
            runtime{{"schema", 1},      {"session_id", id}, {"endpoint", endpoint.string()},
                    {"stopped", false}, {"android_pid", 0}, {"qnx_pid", dead}},
            owner{{"schema", 1},
                  {"session_id", id},
                  {"directory", directory.string()},
                  {"origin", "voyemu-native-supervisor"},
                  {"creator_pid", dead},
                  {"qnx_pid", dead}},
            storage{{"schema", 1}, {"system", system.string()}, {"userdata", userdata.string()}};
    const auto publish = [&] {
        write_private_json(active_dir / (profile + ".json"), active);
        write_private_json(directory / "runtime.json", runtime);
        write_private_json(directory / "session-owner.json", owner);
    };
    publish();
    const auto snapshot = read_private_json_bytes(directory / "runtime.json");
    const auto rejected = [&] {
        bool failed = false;
        try {
            InactiveProfileLease lease(root, profile, machine, active, runtime, storage);
        } catch (...) {
            failed = true;
        }

        check(failed, "unsafe stale epoch accepted");
    };
    owner["creator_pid"] = ::getpid();
    publish();
    rejected();
    owner["creator_pid"] = dead;
    publish();
    check(::chmod(lock_path.c_str(), 0660) == 0, "fixture unsafe lock mode failed");
    rejected();
    check(::chmod(lock_path.c_str(), 0600) == 0, "fixture restore lock mode failed");
    check(::chmod(active_dir.c_str(), 0770) == 0, "fixture unsafe active directory mode failed");
    rejected();
    check(::chmod(active_dir.c_str(), 0700) == 0, "fixture restore directory mode failed");
    const int held = ::open(lock_path.c_str(), O_RDWR);
    check(held >= 0 && ::flock(held, LOCK_EX | LOCK_NB) == 0, "fixture lock failed");
    rejected();
    ::flock(held, LOCK_UN);
    ::close(held);
    const int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    check(sock >= 0, "fixture socket failed");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto socket_text = endpoint.string();
    check(socket_text.size() < sizeof address.sun_path, "fixture endpoint too long");
    std::copy(socket_text.begin(), socket_text.end(), address.sun_path);
    check(::bind(sock, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0,
          "fixture socket bind failed");
    rejected();
    ::close(sock);
    std::filesystem::remove(endpoint);

    for (const auto &file : {ram, system, userdata}) {
        const auto ready = root / (file.filename().string() + ".ready");
        ProcessSpec worker;
        worker.executable = std::filesystem::canonical(argv[0]);
        worker.arguments = {"--hold-file", file.string(), ready.string()};
        auto live = ChildProcess::launch(worker);
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);

        while (!std::filesystem::exists(ready) && std::chrono::steady_clock::now() < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        check(std::filesystem::exists(ready), "fixture file holder unavailable");
        rejected();
        live.terminate(1000);
        check(!live.alive(), "owned holder not terminal");
    }

    auto wrong = active;
    wrong["session_id"] = random_token();
    bool bad = false;
    try {
        InactiveProfileLease lease(root, profile, machine, wrong, runtime, storage);
    } catch (...) {
        bad = true;
    }

    check(bad, "foreign active epoch accepted");
    {
        InactiveProfileLease lease(root, profile, machine, active, runtime, storage);
        const int contender = ::open(lock_path.c_str(), O_RDWR);
        check(contender >= 0 && ::flock(contender, LOCK_EX | LOCK_NB) != 0,
              "inactive lease did not retain supervisor lock");
        ::close(contender);
        check(read_private_json_bytes(directory / "runtime.json") == snapshot &&
                      !runtime.at("stopped").get<bool>() && std::filesystem::exists(ram),
              "inactive lease altered retained metadata/RAM");
    }
    { InactiveProfileLease again(root, profile, machine, active, runtime, storage); }
    std::filesystem::remove_all(root);
    std::cout
            << "inactive profile: dead stale epoch accepted; live PID, held supervisor lock, endpoint, RAM/disk holders and foreign epoch rejected; metadata retained\n";

    return 0;
}
