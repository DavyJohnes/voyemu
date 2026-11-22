#ifdef VOYEMU_ADB_ADAPTER_TEST
#include "android_embedded_adb.hpp"
#endif
#include "adb_host.hpp"
#include "adb_loopback.hpp"
#include "adb_loopback_test_host.hpp"
#include "os/platform.hpp"
#include "os/process.hpp"
#include "os/socket.hpp"
#include "transport.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <exception>
#include <fstream>
#include <future>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr uint32_t cnxn = 0x4e584e43, auth = 0x48545541, open_command = 0x4e45504f;
constexpr uint32_t okay = 0x59414b4f, wrte = 0x45545257, clse = 0x45534c43, stls = 0x534c5453;
constexpr uint32_t legacy = 0x01000000, modern = 0x01000001, max_payload = 1024 * 1024;

static void require(bool condition, const std::string &text) {
    if (!condition) {
        throw std::runtime_error(text);
    }
}

static AdbBytes view(const std::string &s) {
    return {reinterpret_cast<const uint8_t *>(s.data()), s.size()};
}

static AdbBytes view(const Bytes &b) {
    return {b.data(), b.size()};
}

static std::string copy(AdbBytes b) {
    return b.size ? std::string(reinterpret_cast<const char *>(b.data), b.size) : "";
}

static Bytes bytes(const std::string &s) {
    return {s.begin(), s.end()};
}

static void word(Bytes &b, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) {
        b.push_back(static_cast<uint8_t>(n >> (8 * i)));
    }
}

static uint32_t word(const uint8_t *b) {
    uint32_t n = 0;

    for (unsigned i = 0; i < 4; ++i) {
        n |= uint32_t(b[i]) << (8 * i);
    }

    return n;
}

static uint32_t sum(const Bytes &b) {
    uint32_t n = 0;

    for (auto c : b) {
        n += c;
    }

    return n;
}

using voyemu::os::SocketHandle;
constexpr SocketHandle invalid_socket = voyemu::os::no_socket;
// Small peer receive buffers create write backpressure. Linux keeps a sub-MSS window closed and
// probes it on a backoff timer, which would also delay the FIN behind the queued data.
constexpr int backpressure_receive_buffer = voyemu::os::platform_name == "Linux" ? 1 << 17 : 1024;

static bool retryable_socket_error() {
    return voyemu::os::socket_interrupted() || voyemu::os::socket_would_block();
}

struct Socket {
    SocketHandle fd = invalid_socket;
    Socket() = default;

    explicit Socket(SocketHandle value) : fd(value) {}

    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;

    Socket(Socket &&other) noexcept : fd(other.fd) {
        other.fd = invalid_socket;
    }

    Socket &operator=(Socket &&other) noexcept {
        close();
        fd = other.fd;
        other.fd = invalid_socket;

        return *this;
    }

    ~Socket() {
        close();
    }

    bool valid() const {
        return fd != invalid_socket;
    }

    void close() {
        if (!valid()) {
            return;
        }

        voyemu::os::close_socket(fd);
        fd = invalid_socket;
    }

    void configure() const {
        require(voyemu::os::make_private_socket(fd), "test socket flags failed");
    }

    // Whether the socket becomes writable, or readable when writing is unset, within
    // milliseconds; a pending socket error also counts as ready.
    bool ready(bool writing, int milliseconds) const {
        int n;

        do {
            n = voyemu::os::wait_socket(fd, writing, milliseconds * 1000LL);
        } while (n == -1 && voyemu::os::socket_interrupted());

        require(n != -1, "peer poll failed");

        return n != 0;
    }

    bool readable(int milliseconds) const {
        return ready(false, milliseconds);
    }

    bool writable(int milliseconds) const {
        return ready(true, milliseconds);
    }

    void send(const Bytes &b, bool fragmented = false) const {
        size_t offset = 0;
        auto deadline = Clock::now() + 5s;

        while (offset < b.size()) {
            require(Clock::now() < deadline, "peer write timed out");

            if (!writable(100)) {
                continue;
            }

            auto count = fragmented ? std::min<size_t>(offset % 7 + 1, b.size() - offset)
                                    : b.size() - offset;
            auto n = voyemu::os::send_socket(fd, reinterpret_cast<const char *>(b.data() + offset),
                                             count);

            if (n < 0 && retryable_socket_error()) {
                continue;
            }

            require(n > 0, "peer write failed");
            offset += static_cast<size_t>(n);

            if (fragmented) {
                std::this_thread::sleep_for(1ms);
            }
        }
    }

    Bytes receive(size_t count) const {
        Bytes b(count);
        size_t offset = 0;
        auto deadline = Clock::now() + 5s;

        while (offset < count) {
            require(Clock::now() < deadline, "peer read timed out");

            if (!readable(100)) {
                continue;
            }

            auto n = voyemu::os::receive_socket(fd, reinterpret_cast<char *>(b.data() + offset),
                                                count - offset);

            if (n < 0 && retryable_socket_error()) {
                continue;
            }

            require(n > 0, "unexpected peer EOF");
            offset += static_cast<size_t>(n);
        }

        return b;
    }

    void expect_closed(int milliseconds = 1500) const {
        auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        std::array<uint8_t, 4096> b{};

        while (Clock::now() < deadline) {
            if (!readable(50)) {
                continue;
            }

            auto n = voyemu::os::receive_socket(fd, reinterpret_cast<char *>(b.data()), b.size());

            if (n == 0 || (n < 0 && voyemu::os::socket_reset())) {
                return;
            }

            if (n < 0 && retryable_socket_error()) {
                continue;
            }

            require(n >= 0, "error waiting for peer close");
        }

        throw std::runtime_error("native socket did not close within bound");
    }
};

static Socket listener(uint16_t port) {
    Socket s(voyemu::os::open_stream_socket(AF_INET));

    if (!s.valid() || !voyemu::os::exclusive_address(s.fd)) {
        return {};
    }

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);

    if (!voyemu::os::bind_socket(s.fd, reinterpret_cast<sockaddr *>(&a), sizeof(a)) ||
        !voyemu::os::listen_socket(s.fd, 4)) {
        return {};
    }

    s.configure();

    return s;
}

struct Endpoints {
    uint32_t console = 0;
    Socket raw, console_trap, server_trap;

    Endpoints() {
        // Known incorrect mappings must also reach only listeners owned by this test.
        auto process = static_cast<std::uint64_t>(voyemu::os::current_process());

        for (unsigned i = 0; i < 10000; ++i) {
            auto n = static_cast<uint16_t>(16000 + 2 * ((process + i) % 10000));
            auto a = listener(n), b = listener(n + 1), c = listener(n + 20000);

            if (!a.valid() || !b.valid() || !c.valid()) {
                continue;
            }

            console = n;
            console_trap = std::move(a);
            raw = std::move(b);
            server_trap = std::move(c);

            return;
        }

        throw std::runtime_error("could not reserve owned endpoints");
    }

    Socket accept() const {
        require(raw.readable(5000), "native host did not connect to raw N+1 endpoint");
        sockaddr_in a{};
        voyemu::os::SocketLength size = sizeof(a);
        Socket s(voyemu::os::accept_socket(raw.fd));
        require(s.valid() && voyemu::os::peer_name(s.fd, reinterpret_cast<sockaddr *>(&a), &size) &&
                        a.sin_addr.s_addr == htonl(INADDR_LOOPBACK),
                "non-loopback peer");
        s.configure();

        return s;
    }

    void traps() const {
        require(!console_trap.readable(0), "host connected to console N");
        require(!server_trap.readable(0), "host connected to smart socket N+20000");
    }

    std::string serial() const {
        return "emulator-" + std::to_string(console);
    }
};
struct Packet {
    uint32_t command, first, second, checksum;
    Bytes payload;
};

static Bytes packet(uint32_t command, uint32_t first = 0, uint32_t second = 0,
                    const Bytes &payload = {}, bool zero = false) {
    Bytes b;

    for (auto n : {command, first, second, static_cast<uint32_t>(payload.size()),
                   zero ? 0u : sum(payload), command ^ UINT32_MAX}) {
        word(b, n);
    }

    b.insert(b.end(), payload.begin(), payload.end());

    return b;
}

static Packet read_packet(const Socket &s) {
    auto b = s.receive(24);
    auto command = word(b.data()), count = word(b.data() + 12);

    require(word(b.data() + 20) == (command ^ UINT32_MAX) && count <= max_payload,
            "invalid host packet header");

    return {command, word(b.data() + 4), word(b.data() + 8), word(b.data() + 16), s.receive(count)};
}

static void initial(const Socket &s) {
    auto p = read_packet(s);
    std::string text(p.payload.begin(), p.payload.end());

    require(p.command == cnxn && p.first == modern,
            "expected raw CNXN, not a smart socket request");
    require(p.checksum == sum(p.payload), "invalid host initial checksum");
    require(text.starts_with("host::") && text.find("shell_v2") != std::string::npos,
            "host CNXN features missing");
}

static std::string banner(bool shell = true) {
    return std::string(
                   "device::ro.product.name=loopback-proof;ro.product.model=OwnedPeer;features=") +
           (shell ? "shell_v2" : "");
}

struct Event {
    uint32_t type = 0, state = 0, has_exit = 0;
    uint64_t operation = 0, generation = 0;
    int status = 0, exit = 0;
    std::string serial, model, product;
    Bytes payload;
};
struct Result {
    bool opened = false, terminal = false, has_exit = false;
    int status = 0, exit = 0;
    Bytes output, error;
};
struct Host {
    AdbHost *value = nullptr;
    std::string serial;
    uint64_t generation = 0;
    bool stopped = false;

    explicit Host(uint64_t output_budget = 0) {
        AdbHostConfig c{};
        c.maximum_queued_input_bytes = 4096;
        c.maximum_queued_output_bytes = output_budget;
        require(adb_host_create(&c, &value) == ADB_STATUS_OK, "host creation failed");
        auto deadline = Clock::now() + 5s;

        for (;;) {
            require(Clock::now() < deadline, "HOST_READY missing");

            if (next().type == ADB_EVENT_HOST_READY) {
                break;
            }
        }
    }

    ~Host() {
        if (!value) {
            return;
        }

        if (!stopped && adb_host_shutdown(value, 5000) != ADB_STATUS_OK) {
            std::abort();
        }

        for (;;) {
            AdbEvent e{};
            auto status = adb_host_next_event(value, 0, &e);

            if (status == ADB_STATUS_OFFLINE) {
                break;
            }

            if (status != ADB_STATUS_OK) {
                std::abort();
            }

            adb_host_event_release(&e);
        }

        adb_host_destroy(value);
    }

    Event next(uint64_t timeout = 100) const {
        AdbEvent e{};
        auto status = adb_host_next_event(value, timeout, &e);

        if (status == ADB_STATUS_TIMEOUT) {
            return {};
        }

        require(status == ADB_STATUS_OK, "event read failed");
        Event out{e.type,
                  e.device_state,
                  e.has_remote_exit_code,
                  e.operation,
                  e.device_generation,
                  e.status,
                  e.remote_exit_code,
                  copy(e.serial),
                  copy(e.model),
                  copy(e.product),
                  {}};

        if (e.payload.size) {
            out.payload.assign(e.payload.data, e.payload.data + e.payload.size);
        }

        adb_host_event_release(&e);

        if (out.type == ADB_EVENT_DEVICE && !serial.empty()) {
            require(out.serial == serial, "unselected device event exposed");
        }

        return out;
    }

    void attach(const Endpoints &e) {
        serial = e.serial();
        require(adb_host_attach_emulator(value, e.console) == ADB_STATUS_OK,
                "owned emulator attach failed");
    }

    uint64_t device(uint32_t state, uint64_t previous = 0) {
        auto deadline = Clock::now() + 5s;

        while (Clock::now() < deadline) {
            auto e = next();

            if (e.type != ADB_EVENT_DEVICE || e.state != state) {
                continue;
            }

            if (state == ADB_DEVICE_REMOVED ? previous && e.generation != previous
                                            : e.generation == previous) {
                continue;
            }

            require(e.generation != 0, "zero device generation");

            if (state == 7) {
                require(e.model == "OwnedPeer" && e.product == "loopback-proof",
                        "CNXN metadata missing");
                generation = e.generation;
            }

            return e.generation;
        }

        throw std::runtime_error("expected device state missing: " + std::to_string(state));
    }

    uint64_t open(const std::string &command, uint32_t service = ADB_SERVICE_SHELL_V2_RAW) const {
        uint64_t id = 0;
        require(adb_host_open(value, view(serial), generation, service, view(command), 10000,
                              &id) == ADB_STATUS_OK &&
                        id != 0,
                "stream open failed");

        return id;
    }

    std::map<uint64_t, Result> wait(std::initializer_list<uint64_t> ids) const {
        std::map<uint64_t, Result> out;

        for (auto id : ids) {
            out.emplace(id, Result{});
        }

        auto deadline = Clock::now() + 10s;

        while (std::any_of(out.begin(), out.end(), [](const auto &p) {
            return !p.second.terminal;
        })) {
            require(Clock::now() < deadline, "terminal event timed out");
            auto e = next();
            auto it = out.find(e.operation);

            if (it == out.end()) {
                continue;
            }

            auto &r = it->second;

            if (e.type == ADB_EVENT_OPENED) {
                require(!r.opened, "duplicate OPENED");
                r.opened = true;
            }

            if (e.type == ADB_EVENT_STDOUT || e.type == ADB_EVENT_STDERR) {
                require(r.opened && !r.terminal, "data outside stream lifetime");
                auto &b = e.type == ADB_EVENT_STDOUT ? r.output : r.error;
                b.insert(b.end(), e.payload.begin(), e.payload.end());
            }

            if (e.type == ADB_EVENT_TERMINAL) {
                require(!r.terminal, "duplicate terminal");
                r.terminal = true;
                r.status = e.status;
                r.exit = e.exit;
                r.has_exit = e.has_exit;
            }
        }

        return out;
    }

    void stop() {
        auto start = Clock::now();
        auto first = adb_host_shutdown(value, 0);
        require((first == ADB_STATUS_OK || first == ADB_STATUS_TIMEOUT) &&
                        Clock::now() - start < 250ms,
                "zero-time shutdown blocked");
        require(adb_host_shutdown(value, 5000) == ADB_STATUS_OK, "shutdown retry failed");
        require(Clock::now() - start < 2500ms, "shutdown failed to interrupt socket I/O");
        stopped = true;
    }
};

static void online(Host &h, const Socket &s, uint32_t version = modern, bool shell = true,
                   bool zero = false) {
    initial(s);
    s.send(packet(cnxn, version, max_payload, bytes(banner(shell)), zero), true);
    h.device(7);
}

struct Stream {
    uint64_t operation;
    uint32_t local, remote;
};

static Stream begin(Host &h, const Socket &s, const std::string &command,
                    uint32_t service = ADB_SERVICE_SHELL_V2_RAW) {
    auto id = h.open(command, service);
    auto p = read_packet(s);
    std::string destination;

    switch (service) {
    case ADB_SERVICE_ROOT:
        destination = "root:";
        break;
    case ADB_SERVICE_REMOUNT:
        destination = "remount:";
        break;
    case ADB_SERVICE_REBOOT:
        destination = "reboot:";
        break;
    case ADB_SERVICE_EXEC_RAW:
        destination = "exec:" + command;
        break;
    case ADB_SERVICE_SYNC:
        destination = "sync:";
        break;
    case ADB_SERVICE_OBSERVER:
        destination = "localabstract:voyemu-ui-" + command;
        break;
    default:
        destination = "shell,v2,raw:" + command;
        break;
    }

    auto expected = bytes(destination);
    expected.push_back(0);
    require(p.command == open_command && p.first && p.second == 0 && p.payload == expected,
            "incorrect exact A_OPEN service");
    auto remote = p.first + 1000;
    s.send(packet(okay, remote, p.first));

    return {id, p.first, remote};
}

static void ack(const Socket &s, const Stream &stream) {
    auto p = read_packet(s);

    require(p.command == okay && p.first == stream.local && p.second == stream.remote,
            "stream ACK mismatch");
}

static void frame(Bytes &out, uint8_t id, const Bytes &data) {
    out.push_back(id);
    word(out, static_cast<uint32_t>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}

static void reply(const Socket &s, const Stream &stream, const Bytes &output,
                  const Bytes &error = {}, uint8_t exit = 0) {
    Bytes b;

    frame(b, 1, output);
    frame(b, 2, error);
    frame(b, 3, {exit});
    size_t offset = 0;

    while (offset < b.size()) {
        auto count = std::min<size_t>(offset < 10 ? 1 + offset % 3 : 8191, b.size() - offset);
        s.send(packet(wrte, stream.remote, stream.local,
                      {b.begin() + offset, b.begin() + offset + count}));
        offset += count;

        if (offset < b.size()) {
            ack(s, stream);
        }
    }
}

static Bytes stdin_eof(const Socket &s, const Stream &stream) {
    Bytes framing, input;

    for (;;) {
        auto p = read_packet(s);
        require(p.command == wrte && p.first == stream.local && p.second == stream.remote,
                "unexpected upload packet");
        s.send(packet(okay, stream.remote, stream.local));
        framing.insert(framing.end(), p.payload.begin(), p.payload.end());

        while (framing.size() >= 5) {
            auto n = word(framing.data() + 1);
            require(n <= max_payload, "oversized shell frame");

            if (framing.size() < n + 5) {
                break;
            }

            auto type = framing[0];
            require(type == 0 || (type == 4 && n == 0), "unexpected shell stdin frame");

            if (type == 0) {
                input.insert(input.end(), framing.begin() + 5, framing.begin() + 5 + n);
            }

            framing.erase(framing.begin(), framing.begin() + 5 + n);

            if (type == 4) {
                require(framing.empty(), "bytes after CloseStdin");

                return input;
            }
        }
    }
}

static void success(const Result &r, const Bytes &output = {}, const Bytes &error = {},
                    int exit = 0) {
    require(r.opened && r.terminal && r.status == ADB_STATUS_OK && r.has_exit && r.exit == exit &&
                    r.output == output && r.error == error,
            "shell result mismatch");
}

static void operation_deadline() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s);
    const auto start = Clock::now();
    uint64_t id = 0;
    require(adb_host_open(h.value, view(h.serial), h.generation, ADB_SERVICE_SHELL_V2_RAW,
                          view(std::string("held")), 150, &id) == ADB_STATUS_OK,
            "timed stream open failed");
    require(read_packet(s).command == open_command, "timed stream did not reach the peer");
    auto r = h.wait({id}).at(id);
    auto elapsed = Clock::now() - start;
    require(!r.opened && r.status == ADB_STATUS_TIMEOUT && elapsed >= 100ms && elapsed < 1500ms,
            "operation timeout did not end the pending stream");
    e.traps();
}

static void fenced_ownership() {
    Endpoints target, other;

    enum class Permission {
        Denied,
        Once,
        Allowed
    };
    std::atomic<Permission> permission = Permission::Denied;
    Host h;
    h.serial = target.serial();
    const auto fence = [](void *context) -> int32_t {
        auto &state = *static_cast<std::atomic<Permission> *>(context);
        auto once = Permission::Once;

        return state.compare_exchange_strong(once, Permission::Denied) ||
               once == Permission::Allowed;
    };
    require(adb_host_attach_owned_emulator(h.value, target.console, fence, &permission) ==
                    ADB_STATUS_OK,
            "fenced attach failed");
    require(!target.raw.readable(300), "fence admitted an unowned endpoint");
    permission = Permission::Once;
    auto rejected = target.accept();
    require(rejected.readable(1500), "post-connect fence did not close the endpoint");
    char byte{};
    const auto count = voyemu::os::receive_socket(rejected.fd, &byte, 1);
    require(count == 0 || (count < 0 && voyemu::os::socket_reset()),
            "ADB bytes escaped the post-connect fence");
    permission = Permission::Allowed;
    auto first = target.accept();
    online(h, first);
    const auto retired = h.generation;
    permission = Permission::Denied;
    require(adb_host_detach_emulator(h.value) == ADB_STATUS_OK, "fenced detach failed");
    first.expect_closed();
    require(!target.raw.readable(300), "detached connector reopened the endpoint");
    require(adb_host_attach_owned_emulator(h.value, other.console, fence, &permission) ==
                    ADB_STATUS_INVALID,
            "detached connector accepted a different endpoint");
    require(!other.raw.readable(0), "connector opened a different endpoint");
    permission = Permission::Allowed;
    require(adb_host_attach_owned_emulator(h.value, target.console, fence, &permission) ==
                    ADB_STATUS_OK,
            "same-port reattachment failed");
    auto second = target.accept();
    online(h, second);
    require(h.generation != retired, "same-port reattachment reused a retired generation");
    uint64_t operation = 0;
    const std::string command = "stale";
    require(adb_host_open(h.value, view(h.serial), retired, ADB_SERVICE_SHELL_V2_RAW, view(command),
                          1000, &operation) == ADB_STATUS_STALE_DEVICE,
            "retired generation admitted");
    target.traps();
}

static void selection() {
    AdbHostConfig c{};

    c.maximum_queued_output_bytes = 64 * 1024 * 1024 + 1;
    AdbHost *ignored = nullptr;
    require(adb_host_create(&c, &ignored) == ADB_STATUS_INVALID,
            "oversized output budget accepted");
    Endpoints e, other;
    Host h;
    require(adb_host_attach_emulator(nullptr, e.console) == ADB_STATUS_INVALID,
            "null host accepted");

    for (uint32_t n : {0u, 1u, 1023u, 1025u, 65535u, 65536u, UINT32_MAX}) {
        require(adb_host_attach_emulator(h.value, n) == ADB_STATUS_INVALID,
                "invalid console port accepted");
    }

    h.attach(e);
    require(adb_host_attach_emulator(h.value, e.console) == ADB_STATUS_OK,
            "same target is not idempotent");
    require(adb_host_attach_emulator(h.value, other.console) == ADB_STATUS_INVALID,
            "immutable target replacement did not return INVALID");
    auto s = e.accept();
    online(h, s);
    require(!other.raw.readable(0), "different target connected");
    uint64_t id = 0;
    std::string wrong = "emulator-" + std::to_string(e.console + 2), command = "x";
    require(adb_host_open(h.value, view(wrong), h.generation, 1, view(command), 1000, &id) ==
                    ADB_STATUS_OFFLINE,
            "different serial selected");
    require(adb_host_open(h.value, view(h.serial), h.generation + 1, 1, view(command), 1000, &id) ==
                    ADB_STATUS_STALE_DEVICE,
            "stale generation accepted");
    e.traps();
    h.stop();
    s.expect_closed();
    require(adb_host_attach_emulator(h.value, e.console) == ADB_STATUS_INVALID,
            "stopped host accepted attach");
}

static void shell_stream() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s);
    auto stream = begin(h, s, "binary-output");
    require(adb_host_close_stdin(h.value, stream.operation) == ADB_STATUS_OK,
            "CloseStdin rejected");
    require(stdin_eof(s, stream).empty(), "empty stdin changed");
    Bytes output{0, 1, 255, 13, 10, 0, 65}, error{'e', 0, 'r', 13, 10};
    reply(s, stream, output, error, 255);
    success(h.wait({stream.operation}).at(stream.operation), output, error, 255);
    e.traps();
}

static void upload() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s);
    auto stream = begin(h, s, "upload");
    Bytes input(256 * 1024 + 7);

    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<uint8_t>((i * 73) ^ (i >> 7));
    }

    std::exception_ptr error;
    std::thread peer([&] {
        try {
            auto actual = stdin_eof(s, stream);
            require(actual == input, "upload bytes changed before EOF");
            reply(s, stream, actual, {}, 7);
        } catch (...) {
            error = std::current_exception();
        }
    });
    size_t offset = 0;
    bool partial = false;
    auto deadline = Clock::now() + 10s;
    try {
        while (offset < input.size()) {
            require(Clock::now() < deadline, "upload backpressure did not clear");
            size_t accepted = 0;
            auto status = adb_host_write(h.value, stream.operation,
                                         {input.data() + offset, input.size() - offset}, &accepted);
            require((status == ADB_STATUS_OK || status == ADB_STATUS_WOULD_BLOCK) &&
                            accepted <= 4096,
                    "input admission bound broken");
            partial |= accepted < input.size() - offset;
            offset += accepted;

            if (!accepted) {
                std::this_thread::sleep_for(1ms);
            }
        }

        require(partial && adb_host_close_stdin(h.value, stream.operation) == ADB_STATUS_OK,
                "upload EOF failed");
        auto result = h.wait({stream.operation}).at(stream.operation);
        peer.join();

        if (error) {
            std::rethrow_exception(error);
        }

        success(result, input, {}, 7);
    } catch (...) {
        voyemu::os::shutdown_socket(s.fd, true);

        if (peer.joinable()) {
            peer.join();
        }

        throw;
    }
}

static void cancel() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s);
    auto a = begin(h, s, "held"), b = begin(h, s, "survivor");
    require(adb_host_cancel(h.value, a.operation) == ADB_STATUS_OK, "cancel failed");
    auto p = read_packet(s);
    require(p.command == clse && p.first == a.local && p.second == a.remote,
            "cancel did not close exact stream");
    require(adb_host_close_stdin(h.value, b.operation) == ADB_STATUS_OK, "survivor EOF rejected");
    stdin_eof(s, b);
    reply(s, b, bytes("alive"));
    auto results = h.wait({a.operation, b.operation});
    require(results.at(a.operation).status == ADB_STATUS_CANCELLED, "cancel terminal mismatch");
    success(results.at(b.operation), bytes("alive"));
}

static void unauthorized() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    initial(s);
    s.send(packet(auth, 1, 0, Bytes(20, 0x43)), true);
    h.generation = h.device(2);
    uint64_t id = 0;
    std::string command = "forbidden";
    require(adb_host_open(h.value, view(h.serial), h.generation, 1, view(command), 1000, &id) ==
                    ADB_STATUS_UNAUTHORIZED,
            "unauthorized transport accepted stream");
    require(!s.readable(100), "keyless AUTH emitted data or service request");
}

static void missing_shell() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s, modern, false);
    uint64_t id = 0;
    std::string command = "unsupported";
    require(adb_host_open(h.value, view(h.serial), h.generation, 1, view(command), 1000, &id) ==
                    ADB_STATUS_UNSUPPORTED,
            "missing shell_v2 ignored");
    require(!s.readable(100), "unsupported shell sent A_OPEN");
}

static void malformed(const std::string &name) {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    initial(s);
    auto p = packet(cnxn, legacy, max_payload, bytes(banner()));

    if (name == "malformed-magic") {
        p[20] ^= 1;
    }

    if (name == "malformed-size") {
        p.resize(24);

        for (unsigned i = 12; i < 16; ++i) {
            p[i] = 255;
        }
    }

    if (name == "malformed-checksum") {
        p[16] ^= 1;
    }

    if (name == "truncated-header" || name == "packet-deadline") {
        p.resize(9);
    }

    if (name == "truncated-payload") {
        p.resize(29);
    }

    if (name == "stls") {
        p = packet(stls, legacy);
    }

    s.send(p);

    if (name == "truncated-header" || name == "truncated-payload") {
        require(voyemu::os::shutdown_socket(s.fd, false), "peer EOF failed");
    }

    s.expect_closed(name == "packet-deadline" ? 6500 : 1500);
    require(adb_host_scan(h.value) == ADB_STATUS_OK, "host failed after malformed packet");
    uint64_t id = 0;
    std::string command = "forbidden";
    require(adb_host_open(h.value, view(h.serial), 1, 1, view(command), 1000, &id) != ADB_STATUS_OK,
            "malformed peer became usable");
    e.traps();
}

static void reconnect() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s);
    auto first = h.generation;
    s.close();
    h.device(ADB_DEVICE_REMOVED, first);
    auto next = e.accept();
    initial(next);
    next.send(packet(cnxn, modern, max_payload, bytes(banner())));
    h.device(7, first);
    require(h.generation != first, "reconnect reused generation");
    uint64_t id = 0;
    std::string command = "stale";
    require(adb_host_open(h.value, view(h.serial), first, 1, view(command), 1000, &id) ==
                    ADB_STATUS_STALE_DEVICE,
            "stale reconnect generation accepted");
    auto stream = begin(h, next, "fresh");
    reply(next, stream, bytes("fresh"));
    success(h.wait({stream.operation}).at(stream.operation), bytes("fresh"));
    e.traps();
}

static void shutdown_io(const std::string &name) {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();

    if (name == "shutdown-write") {
        require(voyemu::os::set_socket_buffer(s.fd, false, backpressure_receive_buffer),
                "peer receive bound failed");
        online(h, s);
        auto stream = begin(h, s, "blocked-writer");
        Bytes input(65536, 0x65);
        bool blocked = false;
        // Read the first packet but withhold its ACK so the peer cannot drain queued input.
        size_t accepted = 0;
        require(adb_host_write(h.value, stream.operation, view(input), &accepted) ==
                                ADB_STATUS_OK &&
                        accepted > 0,
                "blocked writer did not accept initial input");
        const auto pending = read_packet(s);
        require(pending.command == wrte && pending.first == stream.local &&
                        pending.second == stream.remote,
                "blocked writer did not send stream data");
        const auto deadline = Clock::now() + 2s;

        while (Clock::now() < deadline) {
            auto status = adb_host_write(h.value, stream.operation, view(input), &accepted);
            require(status == ADB_STATUS_OK || status == ADB_STATUS_WOULD_BLOCK,
                    "blocked write failed with status " + std::to_string(status));

            if (status == ADB_STATUS_WOULD_BLOCK) {
                blocked = true;
                break;
            }
        }

        require(blocked, "blocked-writer scenario did not produce backpressure");
    } else {
        initial(s);

        if (name == "shutdown-partial") {
            auto p = packet(cnxn, legacy);
            p.resize(9);
            s.send(p);
        }
    }

    h.stop();
    s.expect_closed();
    e.traps();
}

static void checksums() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s, legacy);
    auto stream = begin(h, s, "legacy-checksum");
    reply(s, stream, bytes("checked"));
    success(h.wait({stream.operation}).at(stream.operation), bytes("checked"));
}

bool adb_host_test_bound_stream_socket(AdbHost *, uint64_t);
size_t adb_host_test_queued_stream_input(AdbHost *, uint64_t);
static void on_loop(std::function<void()> function);

static void controls() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s, modern, true, true);

    for (uint32_t service : {ADB_SERVICE_ROOT, ADB_SERVICE_REMOUNT, ADB_SERVICE_REBOOT}) {
        auto stream = begin(h, s, "", service);
        size_t accepted = 0;
        Bytes input{1};
        require(adb_host_write(h.value, stream.operation, view(input), &accepted) ==
                                ADB_STATUS_UNSUPPORTED &&
                        adb_host_close_stdin(h.value, stream.operation) == ADB_STATUS_UNSUPPORTED,
                "control accepted stdin");
        Bytes output{0, 1, 255, 13, 10};
        s.send(packet(wrte, stream.remote, stream.local, output, true));
        ack(s, stream);
        s.send(packet(clse, stream.remote, stream.local, {}, true));
        auto p = read_packet(s);
        require(p.command == clse, "control close not acknowledged");
        auto result = h.wait({stream.operation}).at(stream.operation);
        require(result.opened && result.status == ADB_STATUS_OK && !result.has_exit &&
                        result.output == output,
                "raw control result mismatch");
    }

    const auto finish = [&](const Stream &stream, const Bytes &output) {
        s.send(packet(clse, stream.remote, stream.local));
        require(read_packet(s).command == clse, "raw close not acknowledged");
        const auto result = h.wait({stream.operation}).at(stream.operation);
        require(result.opened && result.status == ADB_STATUS_OK && !result.has_exit &&
                        result.output == output,
                "raw service result mismatch");
    };
    const Bytes binary{0, 1, 255, 13, 10};
    auto exec = begin(h, s, "cat /data/test", ADB_SERVICE_EXEC_RAW);
    s.send(packet(wrte, exec.remote, exec.local, binary));
    ack(s, exec);
    finish(exec, binary);

    uint64_t rejected = 0;
    const std::string invalid = "other-socket";
    require(adb_host_open(h.value, view(h.serial), h.generation, ADB_SERVICE_OBSERVER,
                          view(invalid), 1000, &rejected) == ADB_STATUS_INVALID,
            "arbitrary observer socket admitted");
    auto observer = begin(h, s, "0123456789abcdef0123456789abcdef", ADB_SERVICE_OBSERVER);
    size_t accepted = 0;
    require(adb_host_write(h.value, observer.operation, view(binary), &accepted) == ADB_STATUS_OK &&
                    accepted == binary.size(),
            "observer write admission");
    auto upload = read_packet(s);
    require(upload.command == wrte && upload.first == observer.local &&
                    upload.second == observer.remote && upload.payload == binary,
            "observer write was framed or corrupted");
    s.send(packet(okay, observer.remote, observer.local));
    s.send(packet(wrte, observer.remote, observer.local, binary));
    ack(s, observer);
    finish(observer, binary);

    auto sync = begin(h, s, "", ADB_SERVICE_SYNC);
    const auto opened = h.next();
    require(opened.operation == sync.operation && opened.type == ADB_EVENT_OPENED,
            "sync did not open");

    for (unsigned i = 0; i < 16; ++i) {
        s.send(packet(wrte, sync.remote, sync.local, Bytes(4096, 0x76)));
        ack(s, sync);
    }

    std::size_t received = 0;

    while (received < 65536) {
        const auto data = h.next(1000);
        require(data.operation == sync.operation && data.type == ADB_EVENT_STDOUT &&
                        std::all_of(data.payload.begin(), data.payload.end(),
                                    [](auto byte) {
                                        return byte == 0x76;
                                    }),
                "sync first window corrupted");
        received += data.payload.size();
    }

    require(received == 65536, "sync initial window incorrect");
    s.send(packet(wrte, sync.remote, sync.local, binary));
    ack(s, sync);
    require(h.next(100).type == 0, "sync escaped output credit window");
    require(adb_host_grant_sync_output(h.value, sync.operation, binary.size()) == ADB_STATUS_OK,
            "sync credit rejected");
    const auto data = h.next(1000);
    require(data.operation == sync.operation && data.type == ADB_EVENT_STDOUT &&
                    data.payload == binary,
            "sync did not resume on credit");
    require(adb_host_grant_sync_output(h.value, sync.operation, 1) == ADB_STATUS_OK,
            "sync EOF credit rejected");
    s.send(packet(clse, sync.remote, sync.local));
    require(read_packet(s).command == clse, "sync close not acknowledged");
    const auto terminal = h.next(1000);
    require(terminal.operation == sync.operation && terminal.type == ADB_EVENT_TERMINAL &&
                    terminal.status == ADB_STATUS_OK && !terminal.has_exit,
            "sync terminal status");

    const auto exhaust_credit = [&] {
        auto stream = begin(h, s, "", ADB_SERVICE_SYNC);
        const auto opened = h.next(1000);
        require(opened.operation == stream.operation && opened.type == ADB_EVENT_OPENED,
                "credit-starvation sync did not open");

        for (unsigned i = 0; i < 16; ++i) {
            s.send(packet(wrte, stream.remote, stream.local, Bytes(4096, 0x76)));
            ack(s, stream);
        }

        std::size_t drained = 0;

        while (drained < 65536) {
            const auto event = h.next(1000);
            require(event.operation == stream.operation && event.type == ADB_EVENT_STDOUT,
                    "credit-starvation sync window missing");
            drained += event.payload.size();
        }

        require(drained == 65536, "credit-starvation sync window incorrect");

        return stream;
    };
    const auto blocked = exhaust_credit();

    // The macOS Unix socketpair reliably short-writes. Wine's TCP pair can accept even the full
    // negotiated packet despite the small test buffers, so Windows skips the partial write.
    if (!voyemu::os::host_windows) {
        on_loop([&] {
            require(adb_host_test_bound_stream_socket(h.value, blocked.operation),
                    "sync test socket limit failed");
        });
        s.send(packet(wrte, blocked.remote, blocked.local, Bytes(max_payload, 0x88)));
        std::size_t queued = 0;
        const auto partial_deadline = Clock::now() + 1s;

        do {
            on_loop([&] {
                queued = adb_host_test_queued_stream_input(h.value, blocked.operation);
            });

            if (queued > 0 && queued < max_payload) {
                break;
            }

            std::this_thread::sleep_for(5ms);
        } while (Clock::now() < partial_deadline);

        require(queued > 0 && queued < max_payload,
                "test socket did not partially accept the legacy WRTE: queued=" +
                        std::to_string(queued) +
                        ", peer readable=" + std::to_string(s.readable(0)));
        require(!s.readable(100),
                "legacy WRTE acknowledged before the blocked socket accepted its payload");
    }

    require(adb_host_cancel(h.value, blocked.operation) == ADB_STATUS_OK,
            "credit-starved sync cancel failed");
    require(read_packet(s).command == clse, "credit-starved sync did not close");
    require(h.wait({blocked.operation}).at(blocked.operation).status == ADB_STATUS_CANCELLED,
            "credit-starved sync did not cancel");

    const auto disconnected = exhaust_credit();
    const auto generation = h.generation;
    s.close();
    require(h.wait({disconnected.operation}).at(disconnected.operation).status ==
                    ADB_STATUS_IO_ERROR,
            "credit-starved sync ignored transport loss");
    auto reconnected = e.accept();
    initial(reconnected);
    reconnected.send(packet(cnxn, modern, max_payload, bytes(banner())));
    h.device(ADB_DEVICE_ONLINE, generation);

    constexpr auto shell_command_limit = 128 * 1024 - std::string_view("shell,v2,raw:").size() - 1;
    const std::string long_command(shell_command_limit, 'x');
    auto long_shell = begin(h, reconnected, long_command);
    reply(reconnected, long_shell, bytes("accepted"));
    success(h.wait({long_shell.operation}).at(long_shell.operation), bytes("accepted"));
    const std::string oversized(shell_command_limit + 1, 'x');
    require(adb_host_open(h.value, view(h.serial), h.generation, ADB_SERVICE_SHELL_V2_RAW,
                          view(oversized), 1000, &rejected) == ADB_STATUS_INVALID,
            "oversized command admitted");
}

static void transport_io(bool deadline_test) {
    Endpoints e;
    std::atomic<bool> stopping{false};
    auto connection = adb_host_connect_emulator(e.console, stopping);

    require(connection != nullptr, "direct owned connection failed");
    auto s = e.accept();
    require(voyemu::os::set_socket_buffer(s.fd, false, backpressure_receive_buffer),
            "direct peer receive bound failed");
    std::atomic<unsigned> completed{0};
    std::atomic<bool> finished{false}, write_result{true}, read_result{true};
    auto start = Clock::now();
    std::thread writer([&] {
        apacket p;
        p.msg = {wrte, 1, 2, max_payload, 0, wrte ^ UINT32_MAX};
        p.payload.resize(max_payload);
        std::fill(p.payload.begin(), p.payload.end(), 0x65);

        for (unsigned i = 0; i < 128; ++i) {
            if (!connection->Write(&p)) {
                write_result = false;
                break;
            }

            completed++;
        }

        finished = true;
    });

    if (deadline_test) {
        writer.join();
        require(!write_result && Clock::now() - start < 7500ms, "native write deadline failed");
        require(Clock::now() - start >= 4s, "write deadline scenario did not block");
        connection->Close();
    } else {
        std::thread reader([&] {
            apacket p;
            read_result = connection->Read(&p);
        });
        unsigned stable = 0, previous = completed.load();

        while (stable < 20 && !finished && Clock::now() - start < 2s) {
            std::this_thread::sleep_for(5ms);
            auto current = completed.load();
            stable = current == previous ? stable + 1 : 0;
            previous = current;
        }

        bool was_blocked = stable >= 20 && !finished;
        auto close_start = Clock::now();
        connection->Close();
        connection->Close();
        writer.join();
        reader.join();
        require(was_blocked, "native write did not block before Close");
        require(!write_result && !read_result && Clock::now() - close_start < 1500ms,
                "Close did not interrupt concurrent native read/write");
    }

    s.expect_closed();
    e.traps();
}

static void packet_eintr_deadline() {
    Endpoints e;
    std::atomic<bool> stopping{false};
    auto connection = adb_host_connect_emulator(e.console, stopping);

    require(connection != nullptr, "EINTR owned connection failed");
    auto s = e.accept();
    s.send({static_cast<uint8_t>(cnxn)});
    std::atomic<bool> read_result{true};
    std::atomic<int64_t> elapsed{0};
    auto call = interrupted_call(
            [&] {
                auto read_start = Clock::now();
                apacket p;
                read_result = connection->Read(&p);
                elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() -
                                                                                read_start)
                                  .count();
            },
            [&] {
                connection->Close();
            });
    bool forced_close = !call.finished;
    std::printf("packet EINTR read elapsed_ms=%lld signal_requests=%u forced_close=%d\n",
                static_cast<long long>(elapsed.load()), call.signals, forced_close);
    require(call.signal_error == 0 && call.mask_error == 0 && call.signals >= 20,
            "targeted EINTR setup failed");
    require(!forced_close && !read_result && elapsed >= 4000 && elapsed < 5800,
            "EINTR delayed absolute packet deadline");
    s.expect_closed();
    e.traps();
}

static void on_loop(std::function<void()> function) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    fdevent_run_on_looper([promise, function = std::move(function)] {
        try {
            function();
            promise->set_value();
        } catch (...) {
            promise->set_exception(std::current_exception());
        }
    });
    future.get();
}

struct PausedLoop {
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false, released = false;
    };
    std::shared_ptr<State> state = std::make_shared<State>();

    PausedLoop() {
        auto shared = state;
        fdevent_run_on_looper([shared] {
            std::unique_lock lock(shared->mutex);
            shared->entered = true;
            shared->changed.notify_all();
            shared->changed.wait(lock, [&] {
                return shared->released;
            });
        });
        std::unique_lock lock(state->mutex);
        bool entered = state->changed.wait_for(lock, 5s, [&] {
            return state->entered;
        });

        if (!entered) {
            state->released = true;
            state->changed.notify_all();
        }

        require(entered, "owned looper pause failed");
    }

    ~PausedLoop() {
        release();
    }

    void release() {
        std::lock_guard lock(state->mutex);
        state->released = true;
        state->changed.notify_all();
    }
};
struct SinkConnection final : Connection {
    std::atomic<bool> stopped{false};

    bool Start() override {
        return true;
    }

    void Stop() override {
        stopped = true;
    }

    bool Write(std::unique_ptr<apacket>) override {
        return !stopped;
    }

    bool DoTlsHandshake(RSA *, std::string *) override {
        return false;
    }
};

static std::unique_ptr<apacket> ignored_packet(size_t size) {
    auto p = std::make_unique<apacket>();

    p->msg = {wrte, 0, 0, static_cast<uint32_t>(size), 0, wrte ^ UINT32_MAX};
    p->payload.resize(size);
    std::fill(p->payload.begin(), p->payload.end(), 0x65);

    return p;
}

static void packet_backlog() {
    Host host;
    size_t large = 0, small = 0;
    bool large_rejected = false, small_rejected = false, recovered = false;
    auto make_transport = [] {
        auto *t = new atransport(kTransportLocal, kCsOffline);
        t->SetConnection(std::make_shared<SinkConnection>());

        return t;
    };
    auto *first = make_transport();
    {
        PausedLoop pause;

        for (unsigned i = 0; i < 8; ++i) {
            if (!first->HandleRead(ignored_packet(max_payload))) {
                large_rejected = true;
                break;
            }

            large++;
        }
    }

    on_loop([] {});
    on_loop([&] {
        delete first;
    });
    auto *second = make_transport();
    {
        PausedLoop pause;

        for (unsigned i = 0; i < 66; ++i) {
            if (!second->HandleRead(ignored_packet(1))) {
                small_rejected = true;
                break;
            }

            small++;
        }
    }
    on_loop([] {});
    on_loop([&] {
        delete second;
    });
    auto *fresh = make_transport();
    bool first_read = fresh->HandleRead(ignored_packet(max_payload));
    on_loop([] {});
    bool second_read = fresh->HandleRead(ignored_packet(max_payload));
    on_loop([] {});
    recovered = first_read && second_read;
    on_loop([&] {
        delete fresh;
    });
    std::printf(
            "packet backlog large_accepted=%zu small_accepted=%zu rejected=%d/%d recovered=%d\n",
            large, small, large_rejected, small_rejected, recovered);
    require(large_rejected && large == 4 && small_rejected && small == 64 && recovered,
            "native packet admission exceeded 4MiB/64 packets or did not recover");
}

static void malformed_ack_leak() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s);
    auto stream = begin(h, s, "ack-allocation");
    Bytes warm{'w'};
    size_t accepted = 0;
    require(adb_host_write(h.value, stream.operation, view(warm), &accepted) == ADB_STATUS_OK &&
                    accepted == 1,
            "ACK warmup failed");
    auto warm_packet = read_packet(s);
    require(warm_packet.command == wrte, "ACK warmup did not reach peer");
    s.send(packet(okay, stream.remote, stream.local));
    on_loop([] {});
    auto before = heap_bytes_in_use();
    Bytes bad_payload(64 * 1024, 0x65);

    for (unsigned i = 0; i < 32; ++i) {
        s.send(packet(okay, stream.remote, stream.local, bad_payload));
        std::this_thread::sleep_for(2ms);
    }

    s.send(packet(okay, stream.remote, stream.local));
    s.send(packet(open_command, 9000, 0, {'x', 0}));
    auto marker = read_packet(s);
    require(marker.command == clse && marker.first == 0 && marker.second == 9000,
            "final valid ACK processing marker missing");
    on_loop([] {});
    auto after = heap_bytes_in_use();
    size_t retained = after > before ? after - before : 0;
    require(adb_host_cancel(h.value, stream.operation) == ADB_STATUS_OK,
            "ACK allocation stream cleanup failed");
    h.wait({stream.operation});
    std::printf("ACK allocation before=%zu after=%zu retained=%zu malformed_payload_bytes=%u\n",
                before, after, retained, 32u * 64u * 1024u);
    require(retained < 1024 * 1024, "malformed ACK packets retain payload allocations");
    e.traps();
}

static void device_event_budget() {
    constexpr size_t budget = 4096;
    Endpoints e;
    Host h(budget);

    h.attach(e);
    auto s = e.accept();
    initial(s);
    h.generation = h.device(5);
    std::string huge = "device::ro.product.name=" + std::string(64 * 1024, 'p') +
                       ";ro.product.model=" + std::string(64 * 1024, 'm') + ";features=shell_v2";
    auto data = packet(cnxn, modern, max_payload, bytes(huge));
    auto marker = packet(open_command, 9001, 0, {'x', 0});
    data.insert(data.end(), marker.begin(), marker.end());
    s.send(data);
    try {
        auto response = read_packet(s);
        require(response.command == clse && response.second == 9001,
                "oversized metadata processing marker missing");
    } catch (const std::runtime_error &error) {
        if (std::string(error.what()) != "unexpected peer EOF") {
            throw;
        }
    }

    // The marker may already be queued by the time the looper runs the kick.
    s.expect_closed();
    on_loop([] {});
    size_t metadata = 0, events = 0;
    bool oversized_online = false;

    for (;;) {
        auto event = h.next(0);

        if (!event.type) {
            break;
        }

        events++;
        metadata += event.serial.size() + event.model.size() + event.product.size() +
                    event.payload.size();
        oversized_online |= event.type == ADB_EVENT_DEVICE && event.state == 7 &&
                            event.model.size() + event.product.size() > budget;
    }

    s.close();
    auto next = e.accept();
    online(h, next);
    auto generation = h.generation;
    auto stream = begin(h, next, "", ADB_SERVICE_REMOUNT);

    for (;;) {
        auto event = h.next();

        if (event.operation == stream.operation && event.type == ADB_EVENT_OPENED) {
            break;
        }
    }

    next.send(packet(wrte, stream.remote, stream.local, Bytes(budget, 0x65)));
    ack(next, stream);
    std::this_thread::sleep_for(100ms);
    next.close();
    bool producer_removed = false;
    auto removal_deadline = Clock::now() + 5s;

    while (!producer_removed && Clock::now() < removal_deadline) {
        on_loop([&] {
            producer_removed = true;
            iterate_transports([&](const atransport *t) {
                if (t->id == generation) {
                    producer_removed = false;
                }

                return true;
            });
        });

        if (!producer_removed) {
            std::this_thread::sleep_for(5ms);
        }
    }

    require(producer_removed, "transport EOF was not processed while event consumer was held");
    size_t output = 0;
    bool removed = false, terminal = false;
    auto deadline = Clock::now() + 5s;

    while (Clock::now() < deadline && (!removed || !terminal)) {
        auto event = h.next();

        if (event.operation == stream.operation && event.type == ADB_EVENT_STDOUT) {
            output += event.payload.size();
        }

        if (event.operation == stream.operation && event.type == ADB_EVENT_TERMINAL) {
            terminal = true;
        }

        if (event.type == ADB_EVENT_DEVICE && event.generation == generation &&
            event.state == ADB_DEVICE_REMOVED) {
            removed = true;
        }
    }

    std::printf(
            "device events drained_bytes=%zu events=%zu oversized_online=%d full_payload=%zu removed=%d\n",
            metadata, events, oversized_online, output, removed);
    require(metadata <= budget + 1024 && events <= 4096 && !oversized_online,
            "device metadata bypassed output admission or left stale online state");
    require(output == budget && removed && terminal,
            "full output budget lost device removal or stream terminal");
    e.traps();
}

static std::pair<Socket, Socket> local_pair() {
    auto [first, second] = local_socket_pair();
    Socket a(first), b(second);
    a.configure();
    b.configure();
    require(voyemu::os::set_socket_buffer(a.fd, true, 1024), "owned local send buffer failed");

    return {std::move(a), std::move(b)};
}

static void socket_backlog() {
    Host host;
    auto a = local_pair(), b = local_pair(), survivor = local_pair(), tiny = local_pair();
    bool overflow = false, survived = false, block_overflow = false;
    size_t peak = 0, peak_blocks = 0;
    std::array<uint8_t, 4096> preload{};

    for (;;) {
        auto n = voyemu::os::send_socket(
                tiny.first.fd, reinterpret_cast<const char *>(preload.data()), preload.size());

        if (n < 0 && voyemu::os::socket_would_block()) {
            break;
        }

        require(n > 0, "tiny socket backlog preload failed");
    }

    on_loop([&] {
        atransport transport(kTransportLocal, kCsOffline);
        auto create = [&](Socket &socket) {
            auto *local = create_local_socket(unique_fd(static_cast<int>(socket.fd)));
            socket.fd = invalid_socket;
            local->transport = &transport;

            return local;
        };
        auto *first = create(a.first);
        auto *second = create(b.first);
        auto *third = create(survivor.first);
        auto second_id = second->id, third_id = third->id;

        for (unsigned i = 0; i < 3; ++i) {
            Block block(max_payload);
            std::fill(block.begin(), block.end(), 0x65);
            first->enqueue(first, std::move(block));
        }

        first->close(first);

        for (unsigned i = 0; i < 4; ++i) {
            auto *current = find_local_socket(second_id, 0);

            if (!current) {
                overflow = true;
                break;
            }

            Block block(max_payload);
            std::fill(block.begin(), block.end(), 0x65);
            current->enqueue(current, std::move(block));
            current = find_local_socket(second_id, 0);

            if (!current) {
                overflow = true;
                break;
            }

            peak = std::max(peak, first->packet_queue.size() + current->packet_queue.size());
        }

        auto *current = find_local_socket(third_id, 0);

        if (current) {
            Block block(std::string("alive"));
            current->enqueue(current, std::move(block));
            survived = find_local_socket(third_id, 0) != nullptr;
        }

        first->close(first);

        for (auto id : {second_id, third_id}) {
            if (auto *local = find_local_socket(id, 0)) {
                local->has_write_error = true;
                local->close(local);
            }
        }

        auto *small = create(tiny.first);
        auto small_id = small->id;

        for (unsigned i = 0; i < 258; ++i) {
            auto *current = find_local_socket(small_id, 0);

            if (!current) {
                block_overflow = true;
                break;
            }

            Block block(1);
            block[0] = 't';
            current->enqueue(current, std::move(block));
            current = find_local_socket(small_id, 0);

            if (!current) {
                block_overflow = true;
                break;
            }

            peak_blocks = std::max(peak_blocks, current->packet_queue.iovecs().size());
        }

        if (auto *current = find_local_socket(small_id, 0)) {
            current->has_write_error = true;
            current->close(current);
        }
    });
    auto actual = survivor.second.receive(5);
    std::printf(
            "socket backlog aggregate_peak=%zu overflow=%d blocks_peak=%zu block_overflow=%d survivor=%d\n",
            peak, overflow, peak_blocks, block_overflow, survived);
    require(overflow && peak <= 4 * 1024 * 1024 && block_overflow && peak_blocks <= 256 &&
                    survived && actual == bytes("alive"),
            "local socket aggregate exceeded byte/block bound or closed unrelated stream");
}

bool adb_host_test_event_replacement(AdbHost *);
bool adb_host_test_pending_device_retry(AdbHost *);
bool adb_host_test_control_overflow(AdbHost *, uint64_t);

static void event_replacement_admission() {
    Host h(4096);

    require(adb_host_test_event_replacement(h.value),
            "rejected metadata replacement erased truthful removal or accounting");
    auto removal = h.next(0);
    require(removal.type == ADB_EVENT_DEVICE && removal.state == ADB_DEVICE_REMOVED &&
                    removal.generation == 1,
            "retained removal was not delivered");
}

static void shutdown_device_retry() {
    Host h;

    require(adb_host_test_pending_device_retry(h.value), "pending device retry setup failed");
    require(adb_host_shutdown(h.value, 5000) == ADB_STATUS_OK, "retry shutdown failed");
    h.stopped = true;
    auto removal = h.next(0);
    require(removal.type == ADB_EVENT_DEVICE && removal.state == ADB_DEVICE_REMOVED,
            "post-shutdown removal drain failed");
}

#ifdef VOYEMU_ADB_ADAPTER_TEST
static void reboot_generation() {
    Endpoints endpoints;
    std::atomic<bool> owned = true;
    voyemu::AndroidEmbeddedAdb client(endpoints.console);

    client.attach([&] {
        return owned.load();
    });
    const auto identify = [](const Socket &socket) {
        initial(socket);
        socket.send(packet(cnxn, modern, max_payload, bytes(banner())));
    };
    const auto open = [](const Socket &socket, std::string_view destination) {
        const auto request = read_packet(socket);
        auto expected = bytes(std::string(destination));
        expected.push_back(0);
        require(request.command == open_command && request.first && request.second == 0 &&
                        request.payload == expected,
                "unexpected service on reboot peer");

        return Stream{0, request.first, request.first + 1000};
    };
    const auto finish = [](const Socket &socket, const Stream &stream) {
        socket.send(packet(clse, stream.remote, stream.local));
        const auto closed = read_packet(socket);
        require(closed.command == clse && closed.first == stream.local &&
                        closed.second == stream.remote,
                "reboot service CLSE was not acknowledged");
    };

    auto first = endpoints.accept();
    identify(first);
    require(!client.run({"wait-for-device"}, 1000, 128).exit_code, "first transport not online");
    const auto stale = client.lease();
    owned = false;
    auto reboot = std::async(std::launch::async, [&] {
        return client.run({"reboot"}, 1000, 128, {}, stale);
    });
    const auto accepted = open(first, "reboot:");
    first.send(packet(okay, accepted.remote, accepted.local));
    first.send(packet(clse, accepted.remote, accepted.local));
    const auto result = reboot.get();
    require(!result.exit_code && !result.timed_out, "accepted reboot failed");
    // Keep the old TCP peer open after the service CLSE. The host must retire it.
    first.expect_closed();
    require(!endpoints.raw.readable(350), "reboot reconnect ignored ownership fence");
    require(client.run({"wait-for-device"}, 100, 128, {}, stale).timed_out,
            "retired generation stayed ready");
    owned = true;
    auto second = endpoints.accept();
    identify(second);
    require(!client.run({"wait-for-device"}, 1000, 128, {}, stale).exit_code,
            "fresh transport not online");
    require(client.run({"reboot"}, 1000, 128, {}, stale).exit_code != 0,
            "stale reboot lease admitted");
    require(!second.readable(100), "stale reboot opened a service on the new transport");

    const auto current = client.lease();
    auto rejected = std::async(std::launch::async, [&] {
        return client.run({"reboot"}, 1000, 128, {}, current);
    });
    const auto refused = open(second, "reboot:");
    second.send(packet(clse, 0, refused.local));
    const auto refusal = rejected.get();
    require(refusal.exit_code != 0 && !refusal.timed_out,
            "reboot rejected before OPENED succeeded");
    auto failed = std::async(std::launch::async, [&] {
        return client.run({"reboot"}, 1000, 128, {}, current);
    });
    const auto error = open(second, "reboot:");
    second.send(packet(okay, error.remote, error.local));
    const auto failure = bytes("reboot (reboot,adb) failed\n");
    second.send(packet(wrte, error.remote, error.local, failure));
    ack(second, error);
    finish(second, error);
    const auto error_result = failed.get();
    require(error_result.exit_code != 0 && !error_result.timed_out &&
                    error_result.output == std::string(failure.begin(), failure.end()),
            "reboot error response lost");
    require(!endpoints.raw.readable(350), "rejected reboot advanced transport");
    auto probe = std::async(std::launch::async, [&] {
        return client.run({"shell", "probe"}, 1000, 128, {}, current);
    });
    const auto stream = open(second, "shell,v2,raw:probe");
    second.send(packet(okay, stream.remote, stream.local));
    require(stdin_eof(second, stream).empty(), "unexpected probe input");
    reply(second, stream, bytes("same transport"));
    const auto unchanged = probe.get();
    require(!unchanged.exit_code && unchanged.output == "same transport",
            "rejected reboot invalidated the lease");
    require(client.shutdown(), "reboot fixture shutdown failed");
    endpoints.traps();
}
#endif

static void control_overflow_terminal() {
    Endpoints e;
    Host h;

    h.attach(e);
    auto s = e.accept();
    online(h, s);
    auto stream = begin(h, s, "", ADB_SERVICE_ROOT);

    for (;;) {
        auto event = h.next();

        if (event.operation == stream.operation && event.type == ADB_EVENT_OPENED) {
            break;
        }
    }

    bool overflow = false;
    on_loop([&] {
        overflow = adb_host_test_control_overflow(h.value, stream.operation);
    });
    require(overflow, "owned control queue did not reach aggregate overflow");
    auto terminal = h.wait({stream.operation}).at(stream.operation);
    require(terminal.status == ADB_STATUS_TRUNCATED && !terminal.has_exit,
            "control overflow was reported as successful EOF");
    e.traps();
}

int main(int argc, char **argv) {
    try {
        voyemu::os::SocketLibrary sockets;
        require(sockets.error() == 0, "test socket library initialization failed");
        // POSIX runs each case through adb_test.cmake, which also passes its owned fixture
        // directory.
        require(argc == (voyemu::os::host_windows ? 2 : 3),
                voyemu::os::host_windows ? "usage: loopback CASE"
                                         : "usage: loopback CASE OWNED_FIXTURE_DIRECTORY");
        std::string name = argv[1];
        adb_host_set_log_callback(nullptr);

        if (name == "target-selection") {
            selection();
        } else if (name == "shell-stream") {
            shell_stream();
        } else if (name == "upload-eof") {
            upload();
        } else if (name == "cancel-isolation") {
            cancel();
        } else if (name == "packet-eintr-deadline") {
            packet_eintr_deadline();
        } else if (name == "malformed-ack-leak") {
            malformed_ack_leak();
        } else if (name == "socket-backlog-bound") {
            socket_backlog();
        }
#ifdef VOYEMU_ADB_ADAPTER_TEST
        else if (name == "reboot-generation") {
            reboot_generation();
        }
#endif
        else if (name == "operation-deadline") {
            operation_deadline();
        } else if (name == "fenced-ownership") {
            fenced_ownership();
        } else if (name == "auth-unauthorized") {
            unauthorized();
        } else if (name == "missing-shell-v2") {
            missing_shell();
        } else if (name == "reconnect-generation") {
            reconnect();
        } else if (name == "legacy-checksum") {
            checksums();
        } else if (name == "control-services") {
            controls();
        } else if (name == "transport-close-io") {
            transport_io(false);
        } else if (name == "transport-write-deadline") {
            transport_io(true);
        } else if (name == "device-event-budget") {
            device_event_budget();
        } else if (name == "packet-backlog-bound") {
            packet_backlog();
        } else if (name == "event-replacement-admission") {
            event_replacement_admission();
        } else if (name == "shutdown-device-retry") {
            shutdown_device_retry();
        } else if (name == "control-overflow-terminal") {
            control_overflow_terminal();
        } else if (name.starts_with("shutdown-")) {
            shutdown_io(name);
        } else if (name.starts_with("malformed-") || name.starts_with("truncated-") ||
                   name == "stls" || name == "packet-deadline") {
            malformed(name);
        } else {
            throw std::runtime_error("unknown scenario");
        }
        std::printf("PASS %s\n", argv[1]);

        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL %s: %s\n", argc > 1 ? argv[1] : "arguments", error.what());

        return 1;
    }
}
