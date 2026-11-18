#include "process.hpp"
#ifndef _WIN32
#include <sys/stat.h>
#endif
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
using namespace voyemu;
#ifndef _WIN32
void closed_reusable_tcp_port(bool wildcard) {
    int listener = ::socket(AF_INET, SOCK_STREAM, 0);

    assert(listener >= 0);
    int enabled = 1;
    assert(::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof enabled) == 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(wildcard ? INADDR_ANY : INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    socklen_t size = sizeof address;
    assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) == 0);
    assert(::listen(listener, 8) == 0);
    auto port = ntohs(address.sin_port);
    int client = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(client >= 0);
    auto target = address;
    target.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(::connect(client, reinterpret_cast<sockaddr *>(&target), sizeof target) == 0);
    int accepted = ::accept(listener, nullptr, nullptr);
    assert(accepted >= 0);
    assert(!tcp_port_free(port)); // REUSEADDR must never hide a live listener.
    ::close(accepted);
    char byte{};
    assert(::recv(client, &byte, 1, 0) == 0);
    ::close(client);
    ::close(listener);
    // The completed server connection remains in TIME_WAIT, but a new SDK listener can bind.
    assert(tcp_port_free(port));
}
#endif

void ipv6_only_tcp_listener(bool wildcard) {
#ifdef _WIN32
    WSADATA data{};
    assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET listener = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    DWORD enabled = 1;
    assert(::setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY,
                        reinterpret_cast<const char *>(&enabled), sizeof enabled) == 0);
#else
    int listener = ::socket(AF_INET6, SOCK_STREAM, 0);
    assert(listener >= 0);
    int enabled = 1;
    assert(::setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &enabled, sizeof enabled) == 0);
#endif
    sockaddr_in6 address{};
    address.sin6_family = AF_INET6;
    address.sin6_addr = wildcard ? in6addr_any : in6addr_loopback;
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
#ifdef _WIN32
    int size = sizeof address;
#else
    socklen_t size = sizeof address;
#endif
    assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) == 0);
    assert(::listen(listener, 1) == 0);
    const auto port = ntohs(address.sin6_port);
    assert(!tcp_port_free(port));
#ifdef _WIN32
    TcpPortProbe diagnostic;
    assert(!tcp_port_free(port, &diagnostic));
    assert(diagnostic.port == port && diagnostic.family == AF_INET6 &&
           std::string_view(diagnostic.stage) == "listener" && diagnostic.error == 0);
    assert(diagnostic.listener_pid == GetCurrentProcessId() &&
           std::string_view(diagnostic.listener_address.data()) == (wildcard ? "::" : "::1"));
#endif
#ifdef _WIN32
    closesocket(listener);
    WSACleanup();
#else
    ::close(listener);
#endif
    assert(tcp_port_free(port));
}

#ifdef _WIN32
void ipv4_tcp_probe(bool wildcard) {
    WSADATA data{};

    assert(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    DWORD exclusive = 1;
    assert(::setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                        reinterpret_cast<const char *>(&exclusive), sizeof exclusive) == 0);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(wildcard ? INADDR_ANY : INADDR_LOOPBACK);
    assert(::bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    int size = sizeof address;
    assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address), &size) == 0);
    const auto port = ntohs(address.sin_port);
    TcpPortProbe diagnostic;
    assert(!tcp_port_free(port, &diagnostic));
    std::cout << "exclusive bound probe family=" << diagnostic.family
              << " stage=" << diagnostic.stage << " error=" << diagnostic.error << std::endl;
    assert(diagnostic.port == port && diagnostic.family == AF_INET &&
           std::string_view(diagnostic.stage) == "bind" &&
           (diagnostic.error == WSAEADDRINUSE || diagnostic.error == WSAEACCES) &&
           diagnostic.listener_pid == 0);
    assert(::listen(listener, 1) == 0);
    assert(!tcp_port_free(port, &diagnostic));
    assert(diagnostic.port == port && diagnostic.family == AF_INET &&
           std::string_view(diagnostic.stage) == "listener" && diagnostic.error == 0);
    assert(diagnostic.listener_pid == GetCurrentProcessId() &&
           std::string_view(diagnostic.listener_address.data()) ==
                   (wildcard ? "0.0.0.0" : "127.0.0.1"));
    closesocket(listener);
    assert(tcp_port_free(port, &diagnostic));
    assert(std::string_view(diagnostic.stage) == "free" && diagnostic.listener_pid == 0 &&
           diagnostic.error == 0);
    WSACleanup();
}
#endif

int sanitized_child(const std::string &mode) {
    const std::string secret = "RAW_PRIVATE_KEY_NONCE_BODY_NEVER_LOG";

#ifdef _WIN32
    const auto output = GetStdHandle(STD_OUTPUT_HANDLE), error = GetStdHandle(STD_ERROR_HANDLE);
    DWORD count = 0;
    auto write = [&](HANDLE handle, std::string_view text) {
        return WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &count, nullptr) !=
               0;
    };
#else
    constexpr int output = 1, error = 2;
    auto write = [](int channel, std::string_view text) {
        auto &stream = channel == 1 ? std::cout : std::cerr;
        stream << text << std::flush;

        return bool(stream);
    };
#endif

    if (mode == "held") {
        write(output, secret);
        std::this_thread::sleep_for(std::chrono::seconds(30));

        return 0;
    }

    const auto repeats = mode == "flood" ? 20000 : 2;

    for (int i = 0; i < repeats; ++i) {
        if (!write(i % 2 ? error : output, secret + "\n")) {
            return 3;
        }
    }

    return 0;
}

void sanitized_process_tests(const std::filesystem::path &self) {
    const auto root = std::filesystem::temp_directory_path() /
                      ("voyemu-sanitized-" + random_token());

    std::filesystem::create_directory(root);
    const auto make = [&](const char *mode) {
        ProcessSpec spec;
        spec.executable = self;
        spec.arguments = {"--sanitized-child", mode};
        spec.directory = root;
        spec.output = root / (random_token() + ".jsonl");

        return spec;
    };
    auto decoder = [](std::string_view bytes, std::int64_t, bool eof) {
        return bytes.empty() ? (eof ? std::string("{\"eof\":true}\n") : std::string{})
                             : std::string("{\"bytes\":") + std::to_string(bytes.size()) + "}\n";
    };
    auto spec = make("eof");
    auto child = ChildProcess::launch_sanitized(spec, decoder);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while ((child.alive() || !child.sanitized_output_complete()) &&
           std::chrono::steady_clock::now() < end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(!child.alive() && child.exit_code() == 0 && child.sanitized_output_complete() &&
           !child.sanitized_output_failed());
    std::ifstream file(spec.output);
    std::string safe((std::istreambuf_iterator<char>(file)), {});
    file.close();
    assert(safe.find("RAW_PRIVATE") == std::string::npos && safe.find("eof") != std::string::npos);
    auto flood = make("flood");
    auto flooding = ChildProcess::launch_sanitized(
            flood,
            [](std::string_view, std::int64_t, bool) {
                return std::string(1024, 's');
            },
            1024);
    const auto flood_end = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while ((flooding.alive() || !flooding.sanitized_output_complete()) &&
           std::chrono::steady_clock::now() < flood_end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(!flooding.alive() && flooding.sanitized_output_complete() &&
           flooding.sanitized_output_failed() && std::filesystem::file_size(flood.output) <= 1024);
#ifndef _WIN32
    struct stat metadata{};
    assert(::stat(spec.output.c_str(), &metadata) == 0 && (metadata.st_mode & 0777) == 0600 &&
           metadata.st_nlink == 1);
#endif
    auto held = make("held");
    const auto cancel_start = std::chrono::steady_clock::now();
    {
        auto holding = ChildProcess::launch_sanitized(held, decoder);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        bool detach_refused = false;
        try {
            holding.detach();
        } catch (const std::runtime_error &) {
            detach_refused = true;
        }

        assert(detach_refused);
        holding.terminate(1000);
        assert(!holding.alive());
    }
    assert(std::chrono::steady_clock::now() - cancel_start < std::chrono::seconds(3));
#ifndef _WIN32
    // Only the corrected binary runs this regression: a failed spawn must never
    // wait for or signal an unrelated retained child through a sentinel PID.
    auto survivor_spec = make("held");
    auto survivor = ChildProcess::launch_sanitized(survivor_spec, decoder);
    assert(survivor.pid() > 1 && survivor.alive());

    for (bool sanitized : {true, false}) {
        auto missing = make("eof");
        missing.executable = root / "missing-executable";
        bool spawn_refused = false;
        try {
            if (sanitized) {
                auto absent = ChildProcess::launch_sanitized(missing, decoder);
            } else {
                auto absent = ChildProcess::launch(missing);
            }
        } catch (const std::runtime_error &error) {
            spawn_refused = std::string_view(error.what()).starts_with("PROCESS_SPAWN:");
        }

        assert(spawn_refused && survivor.alive());
    }

    survivor.terminate(100);
    assert(!survivor.alive());
    survivor = ChildProcess{};
#endif
    auto throwing = make("eof");
    auto failure = ChildProcess::launch_sanitized(
            throwing, [](std::string_view, std::int64_t, bool) -> std::string {
                throw std::runtime_error("private payload");
            });
    const auto failure_end = std::chrono::steady_clock::now() + std::chrono::seconds(10);

    while ((failure.alive() || !failure.sanitized_output_complete()) &&
           std::chrono::steady_clock::now() < failure_end) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    assert(!failure.alive() && failure.sanitized_output_complete() &&
           failure.sanitized_output_failed() && std::filesystem::file_size(throwing.output) == 0);
    // Existing evidence is never opened for append by the diagnostic path.
    bool rejected = false;
    try {
        ChildProcess::launch_sanitized(spec, decoder);
    } catch (const std::runtime_error &) {
        rejected = true;
    }

    assert(rejected);
    child = ChildProcess{};
    flooding = ChildProcess{};
    failure = ChildProcess{};
    std::filesystem::remove_all(root);
}

#ifdef _WIN32
void console_free_child_tests(const std::filesystem::path &executable) {
    const auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                           ("voyemu-console-test-" + random_token());

    if (!std::filesystem::create_directory(directory)) {
        throw std::runtime_error("console test directory unexpectedly exists");
    }

    for (bool detached : {false, true}) {
        ProcessSpec spec;
        spec.executable = executable;
        spec.arguments = {"--console-child"};
        spec.directory = directory;
        spec.output = directory / (detached ? "detached.log" : "ordinary.log");
        spec.detached = detached;
        auto child = ChildProcess::launch(spec);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

        while (child.alive() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        assert(!child.alive() && child.exit_code() == 0);
        std::ifstream file(spec.output);
        std::string line;
        std::getline(file, line);
        assert(line == "console_absent=1");
    }

    std::filesystem::remove_all(directory);
}
#endif

bool capture_bounds_tests(const std::filesystem::path &self) {
    ProcessSpec capture;

    capture.executable = self;
    capture.arguments = {"--capture-exit"};
    capture.directory = std::filesystem::canonical(std::filesystem::temp_directory_path());
    ProcessResult completed{-1, {}};
    try {
        completed = execute(capture, 600000, 1024);
    } catch (const std::exception &error) {
        std::cerr << "600000ms child capture refused: " << error.what() << "\n";

        return false;
    }

    assert(!completed.timed_out && completed.exit_code == 0 && completed.output == "captured");

    for (const int timeout : {0, 600001}) {
        bool rejected = false;
        try {
            execute(capture, timeout, 1024);
        } catch (const std::exception &error) {
            rejected = std::string_view(error.what()) == "PROCESS_BOUNDS: invalid capture bounds";
        }

        assert(rejected);
    }

    capture.arguments = {"--capture-exit"};
    const auto prepared = execute_artifact_preparation(capture, 3600000, 1024);
    assert(!prepared.timed_out && prepared.exit_code == 0 && prepared.output == "captured");
    bool preparation_refused = false;
    try {
        execute_artifact_preparation(capture, 3600001, 1024);
    } catch (const std::exception &) {
        preparation_refused = true;
    }

    assert(preparation_refused);
    capture.arguments = {"--capture-sleep"};
    const auto started = std::chrono::steady_clock::now();
    const auto expired = execute(capture, 25, 1024);
    assert(expired.timed_out &&
           std::chrono::steady_clock::now() - started < std::chrono::seconds(5));
    capture.arguments = {"--capture-sleep"};
    const auto preparation_started = std::chrono::steady_clock::now();
    const auto preparation_expired = execute_artifact_preparation(capture, 25, 1024);
    assert(preparation_expired.timed_out &&
           std::chrono::steady_clock::now() - preparation_started < std::chrono::seconds(5));
    capture.arguments = {"--capture-exit"};
    bool output_refused = false;
    try {
        execute_artifact_preparation(capture, 1200000, 1);
    } catch (const std::exception &error) {
        output_refused = std::string_view(error.what()) == "PROCESS_EXTENT: capture exceeded bound";
    }

    assert(output_refused);
    std::cout
            << "ordinary600000/preparation3600000 capture accepted; next millisecond refused; owned deadlines/output bounds enforced\n";

    return true;
}

int main(int argc, char **argv) try {
    if (argc == 2 && std::string_view(argv[1]) == "--capture-tests-only") {
        return capture_bounds_tests(std::filesystem::absolute(argv[0])) ? 0 : 1;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--capture-exit") {
        std::cout << "captured";

        return 0;
    }

    if (argc == 2 && std::string_view(argv[1]) == "--capture-sleep") {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        return 0;
    }

#ifdef _WIN32
    if (argc == 2 && std::string_view(argv[1]) == "--console-child") {
        const bool absent = GetConsoleWindow() == nullptr;
        std::cout << "console_absent=" << absent << "\n";

        return absent ? 0 : 9;
    }

    console_free_child_tests(std::filesystem::absolute(argv[0]));

    if (argc == 2 && std::string_view(argv[1]) == "--console-tests-only") {
        return 0;
    }
#endif

    if (argc == 3 && std::string_view(argv[1]) == "--sanitized-child") {
        return sanitized_child(argv[2]);
    }

    sanitized_process_tests(std::filesystem::absolute(argv[0]));

    if (!capture_bounds_tests(std::filesystem::absolute(argv[0]))) {
        return 1;
    }

    ipv6_only_tcp_listener(false);
    ipv6_only_tcp_listener(true);
#ifdef _WIN32
    ipv4_tcp_probe(false);
    ipv4_tcp_probe(true);
#endif
    assert(sha256_bytes("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(sha256_bytes("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::string memory_vector;

    for (unsigned i = 0; i < 65536 + 73; ++i) {
        memory_vector.push_back(char(i % 251));
    }

    assert(sha256_bytes(memory_vector) ==
           "b6933c257fdd2981c437f20ac42ec6f4647832fdad1757dc85f2ef3bd9b87ae1");
#ifndef _WIN32
    closed_reusable_tcp_port(false);
    closed_reusable_tcp_port(true);
#endif
    assert(windows_quote("x y") == "\"x y\"");
    assert(windows_quote("C:\\x\\") == "\"C:\\x\\\\\"");
    assert(windows_quote("a\"b") == "\"a\\\"b\"");
    assert(android_shell_quote("a'b") == "'a'\\''b'");
    bool threw = false;
    try {
        android_shell_quote("a\nb");
    } catch (...) {
        threw = true;
    }

    assert(threw);
    auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                     ("voyemu-native-hash-test-" + random_token());
    auto created = std::filesystem::create_directory(directory);
    assert(created);

    if (!created) {
        throw std::runtime_error("test directory unexpectedly exists");
    }

    auto path = directory / "input.bin";
    {
        std::ofstream file(path, std::ios::binary);
        file << "abc";
    }
    assert(sha256_file(path) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    { std::ofstream file(path, std::ios::binary | std::ios::trunc); }
    assert(sha256_file(path) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);

        for (unsigned i = 0; i < 65536 + 73; ++i) {
            file.put(char(i % 251));
        }
    }
    assert(sha256_file(path) == "b6933c257fdd2981c437f20ac42ec6f4647832fdad1757dc85f2ef3bd9b87ae1");
    assert(sha256_file_range(path, 0, memory_vector.size()) == sha256_bytes(memory_vector));
    assert(sha256_file_range(path, 13, 65540) ==
           sha256_bytes(std::string_view(memory_vector).substr(13, 65540)));
    assert(sha256_file_range(path, memory_vector.size(), 0) == sha256_bytes(""));

    for (const auto range : std::vector<std::pair<std::uint64_t, std::uint64_t>>{
                 {memory_vector.size() + 1, 0}, {memory_vector.size(), 1}, {1, UINT64_MAX}}) {
        bool denied = false;
        try {
            sha256_file_range(path, range.first, range.second);
        } catch (...) {
            denied = true;
        }

        assert(denied);
    } // crosses the64 KiB read boundary with embedded NUL bytes

#ifndef _WIN32
    auto linked = directory / "symlink.bin";
    std::filesystem::create_symlink(path, linked);
    threw = false;
    try {
        sha256_file(linked);
    } catch (...) {
        threw = true;
    }

    assert(threw);
#endif
    std::filesystem::remove_all(directory);
    assert(random_token().size() == 32 && random_token() != random_token());
    std::cout << "argument quoting and SHA256 vectors passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
