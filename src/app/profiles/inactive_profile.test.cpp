#include "inactive_profile.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#ifndef _WIN32
#include <sys/socket.h>
#include <sys/un.h>
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#include <exception>
#include <vector>
#endif
using namespace voyemu;

static void check(bool yes, const char *why) {
    if (!yes) {
        throw std::runtime_error(why);
    }
}

int main(int argc, char **argv) try {
#ifndef _WIN32
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
#else
    if (argc == 2 && std::string(argv[1]) == "--exit") {
        return 0;
    }

    if (argc == 4 && std::string(argv[1]) == "--hold-file") {
        const HANDLE file = CreateFileW(std::filesystem::path(argv[2]).c_str(), GENERIC_READ,
                                        FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (file == INVALID_HANDLE_VALUE) {
            return 2;
        }

        {
            std::ofstream ready(argv[3]);
            ready << "ready";
        }
        std::this_thread::sleep_for(std::chrono::seconds(20));
        CloseHandle(file);

        return 0;
    }

    // A private descriptor mirrors the macOS 0600/0700 mode the lease demands; a world descriptor
    // mirrors the 0660/0770 mode it must refuse. The descriptor must ride the creating call: Wine
    // synthesises the DACL at creation and ignores later mutation.
    struct Descriptor {
        std::vector<unsigned char> user;
        PACL acl = nullptr;
        SECURITY_DESCRIPTOR descriptor{};
        SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), &descriptor, FALSE};

        explicit Descriptor(bool foreign) {
            HANDLE token = INVALID_HANDLE_VALUE;
            check(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) != 0,
                  "fixture token failed");
            DWORD bytes = 0;
            GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
            check(bytes != 0, "fixture token size failed");
            user.resize(bytes);
            check(GetTokenInformation(token, TokenUser, user.data(), bytes, &bytes) != 0,
                  "fixture token identity failed");
            CloseHandle(token);
            BYTE world[SECURITY_MAX_SID_SIZE]{};
            DWORD size = sizeof world;
            check(CreateWellKnownSid(WinWorldSid, nullptr, world, &size) != 0,
                  "fixture world identity failed");
            PSID sid = foreign ? static_cast<PSID>(world)
                               : reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid;
            EXPLICIT_ACCESSW access{};
            access.grfAccessPermissions = FILE_ALL_ACCESS;
            access.grfAccessMode = SET_ACCESS;
            access.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
            access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
            access.Trustee.TrusteeType = foreign ? TRUSTEE_IS_WELL_KNOWN_GROUP : TRUSTEE_IS_USER;
            access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(sid);
            check(SetEntriesInAclW(1, &access, nullptr, &acl) == ERROR_SUCCESS,
                  "fixture ACL build failed");
            check(InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) &&
                          SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE) &&
                          SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED,
                                                       SE_DACL_PROTECTED),
                  "fixture descriptor failed");
        }

        ~Descriptor() {
            if (acl) {
                LocalFree(acl);
            }
        }
    };
    Descriptor privacy(false), exposed(true);
    const auto make_directory = [&](const std::filesystem::path &path, const Descriptor &security) {
        std::filesystem::create_directories(path.parent_path());
        check(CreateDirectoryW(path.c_str(),
                               const_cast<SECURITY_ATTRIBUTES *>(&security.attributes)) ||
                      GetLastError() == ERROR_ALREADY_EXISTS,
              "fixture directory creation failed");
    };
    const auto make_file = [&](const std::filesystem::path &path, const std::string &text,
                               const Descriptor &security) {
        const HANDLE file =
                CreateFileW(path.c_str(), GENERIC_WRITE, 0,
                            const_cast<SECURITY_ATTRIBUTES *>(&security.attributes), CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        check(file != INVALID_HANDLE_VALUE, "fixture file creation failed");
        const DWORD extent = static_cast<DWORD>(text.size());
        DWORD count = 0;
        const bool wrote = WriteFile(file, text.data(), extent, &count, nullptr) && count == extent;
        CloseHandle(file);
        check(wrote, "fixture file write failed");
    };
    const auto root = std::filesystem::temp_directory_path() /
                      ("inactive-profile-" + random_token().substr(0, 12));
    ensure_private_directory(root);
    const std::string profile = "test-profile", id = random_token();
    const auto machine = machine_identity(profile, "");
    const auto directory = native_session_directory(root, profile, machine, id),
               active_dir = native_current_active_directory(root, profile, machine);
    make_directory(active_dir, privacy);
    make_directory(directory, privacy);
    const std::string endpoint = "\\\\.\\pipe\\voyemu-" + id;
    const auto lock_path = active_dir / (profile + ".supervisor.lock"),
               owner_path = directory / "session-owner.json", ram = directory / "qnx-ram.bin",
               system = root / "system", userdata = root / "userdata";
    ProcessSpec ended;
    ended.executable = std::filesystem::canonical(argv[0]);
    ended.arguments = {"--exit"};
    ended.output = root / "ended.log";
    auto child = ChildProcess::launch(ended);

    while (child.alive()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const auto dead = child.pid();
    Json active{{"schema", 1},
                {"profile", profile},
                {"session_id", id},
                {"directory", directory.generic_string()},
                {"endpoint", endpoint}},
            runtime{{"schema", 1},      {"session_id", id}, {"endpoint", endpoint},
                    {"stopped", false}, {"android_pid", 0}, {"qnx_pid", dead}},
            owner{{"schema", 1},
                  {"session_id", id},
                  {"directory", directory.generic_string()},
                  {"origin", "voyemu-native-supervisor"},
                  {"creator_pid", dead},
                  {"qnx_pid", dead}},
            storage{{"schema", 1}, {"system", system.string()}, {"userdata", userdata.string()}};
    const auto publish = [&] {
        make_file(active_dir / (profile + ".json"), active.dump() + "\n", privacy);
        make_file(directory / "runtime.json", runtime.dump() + "\n", privacy);
        make_file(owner_path, owner.dump() + "\n", privacy);
    };
    publish();
    make_file(lock_path, "retained", privacy);
    make_file(ram, "retained", privacy);
    make_file(system, "retained", privacy);
    make_file(userdata, "retained", privacy);
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
    const auto accepted = [&] {
        check(!runtime.at("stopped").get<bool>(), "unblocking fixture must keep stopped:false");
        InactiveProfileLease lease(root, profile, machine, active, runtime, storage);
    };
    owner["creator_pid"] = static_cast<std::int64_t>(GetCurrentProcessId());
    publish();
    rejected();
    owner["creator_pid"] = dead;
    publish();
    std::filesystem::remove(lock_path);
    make_file(lock_path, "retained", exposed);
    rejected();
    std::filesystem::remove(lock_path);
    make_file(lock_path, "retained", privacy);
    const HANDLE held = CreateFileW(lock_path.c_str(), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                    FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    check(held != INVALID_HANDLE_VALUE, "fixture lock failed");
    rejected();
    CloseHandle(held);
    const HANDLE pipe = CreateNamedPipeW(std::filesystem::path(endpoint).c_str(),
                                         PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096,
                                         4096, 0, nullptr);
    check(pipe != INVALID_HANDLE_VALUE, "fixture control channel failed");
    rejected();
    CloseHandle(pipe);

    for (const auto &file : {ram, system, userdata}) {
        const auto ready = root / (file.filename().string() + ".ready");
        ProcessSpec worker;
        worker.executable = std::filesystem::canonical(argv[0]);
        worker.arguments = {"--hold-file", file.string(), ready.string()};
        worker.output = root / (file.filename().string() + ".log");
        auto live = ChildProcess::launch(worker);
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);

        while (!std::filesystem::exists(ready) && std::chrono::steady_clock::now() < limit) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        check(std::filesystem::exists(ready), "fixture file holder unavailable");
        rejected();
        live.terminate(1000);
        check(!live.alive(), "owned holder not terminal");
        std::filesystem::remove(ready);
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
    accepted();
    {
        InactiveProfileLease lease(root, profile, machine, active, runtime, storage);
        const HANDLE contender = CreateFileW(
                lock_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        check(contender == INVALID_HANDLE_VALUE, "inactive lease did not retain supervisor lock");

        if (contender != INVALID_HANDLE_VALUE) {
            CloseHandle(contender);
        }

        check(read_private_json_bytes(directory / "runtime.json") == snapshot &&
                      !runtime.at("stopped").get<bool>() && std::filesystem::exists(ram),
              "inactive lease altered retained metadata/RAM");
    }
    { InactiveProfileLease again(root, profile, machine, active, runtime, storage); }
    {
        BYTE world[SECURITY_MAX_SID_SIZE]{};
        DWORD size = sizeof world;
        check(CreateWellKnownSid(WinWorldSid, nullptr, world, &size) != 0,
              "fixture world identity failed");
        const auto changed = SetNamedSecurityInfoW(const_cast<LPWSTR>(ram.c_str()), SE_FILE_OBJECT,
                                                   OWNER_SECURITY_INFORMATION, world, nullptr,
                                                   nullptr, nullptr);
        PSID observed = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        const auto read = GetNamedSecurityInfoW(const_cast<LPWSTR>(ram.c_str()), SE_FILE_OBJECT,
                                                OWNER_SECURITY_INFORMATION, &observed, nullptr,
                                                nullptr, nullptr, &descriptor);
        const bool stranger = changed == ERROR_SUCCESS && read == ERROR_SUCCESS && observed &&
                              EqualSid(observed, world);

        if (descriptor) {
            LocalFree(descriptor);
        }

        if (stranger) {
            rejected();
        } else {
            std::cout << "SKIP foreign owner rejection: assignment not confirmed by SID readback\n";
        }
    }
    std::filesystem::remove_all(directory);
    make_directory(directory, exposed);
    rejected();
    std::filesystem::remove_all(root);
    std::cout
            << "inactive profile: stopped:false dead epoch accepted under a free supervisor lock and absent control channel; live PID, held lock, control channel, RAM/disk holders, foreign epoch, non-private lock/directory DACL and foreign owner rejected; metadata retained\n";

    return 0;
#endif

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
