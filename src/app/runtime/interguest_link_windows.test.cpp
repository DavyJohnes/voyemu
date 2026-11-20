#include "interguest_link.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../core/private_state_security.hpp"
#include <winsock2.h>
#include <windows.h>
#include <winioctl.h>

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <exception>
#include <filesystem>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
using voyemu::InterguestLink;

void wait(const std::function<bool()> &ready) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);

    while (!ready()) {
        assert(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

template <class F> void fail(F action) {
    bool rejected = false;

    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
}

void store(std::uint32_t &word, std::uint32_t value) {
    std::atomic_ref<std::uint32_t>(word).store(value, std::memory_order_release);
}

std::uint32_t load(std::uint32_t &word) {
    return std::atomic_ref<std::uint32_t>(word).load(std::memory_order_acquire);
}

void write(HANDLE file, std::uint64_t offset, const void *data, DWORD bytes) {
    LARGE_INTEGER at{};

    at.QuadPart = offset;
    DWORD count = 0;
    assert(SetFilePointerEx(file, at, nullptr, FILE_BEGIN) &&
           WriteFile(file, data, bytes, &count, nullptr) && count == bytes);
}

int main(int argc, char **argv) try {
    assert(argc == 2);
    WSADATA sockets{};
    assert(WSAStartup(MAKEWORD(2, 2), &sockets) == 0);
    const auto root = std::filesystem::absolute(std::string(argv[1]) + "-" +
                                                std::to_string(GetCurrentProcessId()))
                              .lexically_normal();
    const std::string token = "0123456789abcdef0123456789abcdef";
    const auto dir = root / token;
    assert(!std::filesystem::exists(root));
    std::filesystem::create_directories(dir);
    const auto ram = dir / "qnx-ram.bin";
    voyemu::detail::PrivateStateSecurity security;
    HANDLE file = CreateFileW(ram.c_str(), GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              &security.attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    DWORD returned = 0;
    assert(DeviceIoControl(file, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr));
    LARGE_INTEGER size{};
    size.QuadPart = 8ULL << 30;
    assert(SetFilePointerEx(file, size, nullptr, FILE_BEGIN) && SetEndOfFile(file));
    CloseHandle(file);
    InterguestLink link;
    const auto port = link.start(dir, token);
    link.bind_android(GetCurrentProcessId());
    fail([&] {
        link.bind_ram(ram, 0x50011000);
    });
    const auto alias = dir / "alias";
    assert(CreateHardLinkW(alias.c_str(), ram.c_str(), nullptr));
    fail([&] {
        link.bind_ram(ram);
    });
    assert(DeleteFileW(alias.c_str()));
    file = CreateFileW(ram.c_str(), GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    assert(file != INVALID_HANDLE_VALUE);
    const HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    assert(mapping);
    auto *bytes = static_cast<unsigned char *>(
            MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, 16384));
    assert(bytes);
    auto *words = reinterpret_cast<std::uint32_t *>(bytes + 4096);
    words[0] = 0x54454e51;
    words[1] = 1;
    std::array<std::uint32_t, 8> ring{};
    ring[0] = 0xbeefeeee;
    ring[1] = 16352;
    const std::string marker = "QNET READY pid=42 paddr=0x40001000 length=4096 version=1\r\n";
    ring[2] = marker.size();
    write(file, 0x10010000, ring.data(), 32);
    write(file, 0x10010020, marker.data(), static_cast<DWORD>(marker.size()));
    BY_HANDLE_FILE_INFORMATION info{};
    assert(GetFileInformationByHandle(file, &info) && info.nNumberOfLinks == 1 &&
           ((std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow) == (8ULL << 30));
    link.bind_ram(ram);
    // The discovered page is 4KiB aligned but not 64KiB aligned: Windows must map a bounded aligned
    // view.
    wait([&] {
        return link.status().ready;
    });
    assert(words[12] == 1 && words[13] == 0);
    const auto epoch = link.status().utc_expected;
    assert(epoch >= 946684800 && epoch < 4102444800ULL);
    words[15] = static_cast<std::uint32_t>(epoch);
    words[14] = 0;
    store(words[13], 1);
    wait([&] {
        return link.status().utc_complete;
    });
    assert(link.status().utc_actual == epoch);
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(listener != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    assert(bind(listener, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0 &&
           listen(listener, 1) == 0);
    wait([&] {
        return link.status().connected;
    });
    SOCKET peer = accept(listener, nullptr, nullptr);
    assert(peer != INVALID_SOCKET);
    std::vector<unsigned char> frame(64);

    for (unsigned i = 0; i < frame.size(); ++i) {
        frame[i] = static_cast<unsigned char>(i);
    }

    std::memcpy(bytes + 4096 + 64, frame.data(), frame.size());
    store(words[4], 1);
    store(words[2], 64);
    std::vector<unsigned char> wire(68);
    std::size_t used = 0;

    while (used < wire.size()) {
        const int count = recv(peer, reinterpret_cast<char *>(wire.data() + used),
                               static_cast<int>(wire.size() - used), 0);
        assert(count > 0);
        used += count;
    }

    assert(wire[0] == 0 && wire[1] == 0 && wire[2] == 0 && wire[3] == 64 &&
           std::memcmp(wire.data() + 4, frame.data(), 64) == 0);
    wait([&] {
        return load(words[2]) == 0;
    });
    assert(send(peer, reinterpret_cast<const char *>(wire.data()), static_cast<int>(wire.size()),
                0) == static_cast<int>(wire.size()));
    wait([&] {
        return load(words[3]) == 64;
    });
    assert(std::memcmp(bytes + 4096 + 1600, frame.data(), 64) == 0);
    fail([&] {
        link.flush(0);
    });
    assert(load(words[784]) == 1 && load(words[785]) == 0 && load(words[6]) == 0);
    std::thread guest([&] {
        wait([&] {
            return load(words[784]) == 2;
        });
        words[786] = 0;
        store(words[785], 2);
    });
    link.flush(1000);
    guest.join();
    assert(load(words[785]) == 2);
    // An old success cannot release a new stop attempt.
    fail([&] {
        link.flush(0);
    });
    assert(load(words[784]) == 3 && load(words[785]) == 2 && load(words[6]) == 0);
    link.stop();
    assert(load(words[6]) == 1);
    char end = 0;
    assert(recv(peer, &end, 1, 0) == 0);
    closesocket(peer);
    closesocket(listener);
    // Free A11 and Passion announce the relay on the session UART that the owned QEMU writes, with
    // the A9 RAM ring absent.
    const auto uart = dir / "qnx-serial.log";
    ring[0] = 0;
    write(file, 0x10010000, ring.data(), 32);
    auto reset_page = [&] {
        std::memset(bytes + 4096, 0, 4096);
        words[0] = 0x54454e51;
        words[1] = 1;
    };
    // QEMU opens its file chardev for writing and shares only reads; truncation is a new QEMU.
    auto serial_write = [&](const std::string &value, bool append = false) {
        HANDLE out = CreateFileW(uart.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &security.attributes,
                                 append ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                 nullptr);
        assert(out != INVALID_HANDLE_VALUE);
        LARGE_INTEGER origin{};
        DWORD count = 0;
        assert(SetFilePointerEx(out, origin, nullptr, FILE_END) &&
               WriteFile(out, value.data(), static_cast<DWORD>(value.size()), &count, nullptr) &&
               count == value.size());
        CloseHandle(out);
    };
    reset_page();
    serial_write(std::string(20000, 'x') + "\nnoise " + marker + marker.substr(0, 20));
    link.start(dir, token);
    fail([&] {
        link.bind_ram(ram, 0x50010000, dir / "other.log");
    });
    const auto uart_alias = dir / "uart-alias";
    assert(CreateHardLinkW(uart_alias.c_str(), uart.c_str(), nullptr));
    fail([&] {
        link.bind_ram(ram, 0x50010000, uart);
    });
    assert(DeleteFileW(uart_alias.c_str()));
    link.bind_ram(ram, 0x50010000, uart);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    assert(!link.status().ready && link.status().fault.empty());
    serial_write(marker.substr(20), true);
    wait([&] {
        return link.status().ready;
    });
    assert(load(words[12]) == 1 && link.status().utc_expected >= 946684800ULL);
    // A published request that misses its deadline has an unknown guest outcome.
    bool unknown = false;

    try {
        link.flush(0);
    } catch (const std::exception &error) {
        unknown = std::string(error.what()).starts_with("INTERGUEST_FLUSH_UNKNOWN:");
    }

    assert(unknown && load(words[784]) == 1);
    std::thread uart_guest([&] {
        wait([&] {
            return load(words[784]) == 2;
        });
        words[786] = 0;
        store(words[785], 2);
    });
    link.flush(1000);
    uart_guest.join();
    serial_write("");
    wait([&] {
        return !link.status().fault.empty();
    });
    assert(link.status().fault.starts_with("INTERGUEST_UART:"));
    link.stop();

    for (const auto &invalid :
         {std::string("QNET READY pid=0 paddr=0x40001000 length=4096 version=1\n"),
          std::string("QNET READY pid=42 paddr=0x40001001 length=4096 version=1\n"),
          std::string("QNET READY pid=42 paddr=0x240000000 length=4096 version=1\n"),
          std::string("QNET READY pid=42 paddr=0x40001000 length=4096 version=1 junk\n"),
          "QNET READY " + std::string(300, 'x') + "\n", marker + marker}) {
        reset_page();
        serial_write(invalid);
        link.start(dir, token);
        link.bind_ram(ram, 0x50010000, uart);
        wait([&] {
            return !link.status().fault.empty();
        });
        assert(!link.status().ready);
        link.stop();
    }

    reset_page();
    UnmapViewOfFile(bytes);
    CloseHandle(mapping);
    // Passion backing includes OEM storage; only the boot-mapped prefix may own a mailbox.
    const auto passion_geometry = InterguestLink::RamGeometry::passion;
    auto resize = [&](std::uint64_t extent) {
        LARGE_INTEGER length{};
        length.QuadPart = static_cast<LONGLONG>(extent);
        assert(SetFilePointerEx(file, length, nullptr, FILE_BEGIN) && SetEndOfFile(file));
    };
    resize(11713ULL << 20);
    serial_write(marker);
    link.start(dir, token);
    fail([&] {
        link.bind_ram(ram, 0x50010000, uart);
    });
    link.bind_ram(ram, 0x50010000, uart, passion_geometry);
    wait([&] {
        return link.status().ready;
    });
    link.stop();
    std::array<std::uint32_t, 1024> fresh{};
    fresh[0] = 0x54454e51;
    fresh[1] = 1;
    write(file, 4096, fresh.data(), 4096);
    serial_write("QNET READY pid=42 paddr=0x180000000 length=4096 version=1\n");
    link.start(dir, token);
    link.bind_ram(ram, 0x50010000, uart, passion_geometry);
    wait([&] {
        return !link.status().fault.empty();
    });
    assert(!link.status().ready);
    link.stop();
    // Geometry must be explicit and UART-bound; exact extent and ownership fences remain.
    link.start(dir, token);
    fail([&] {
        link.bind_ram(ram, 0x50010000, {}, passion_geometry);
    });
    resize((11713ULL << 20) - 4096);
    fail([&] {
        link.bind_ram(ram, 0x50010000, uart, passion_geometry);
    });
    link.stop();
    CloseHandle(file);
    // Explicit Wine-to-Mac diagnostic keeps the Mac SDK client direction, without a Windows SDK
    // PID.
    InterguestLink diagnostic;
    const auto diagnostic_port = diagnostic.start(dir, token, true);
    SOCKET diagnostic_peer = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(diagnostic_peer != INVALID_SOCKET);
    address.sin_port = htons(static_cast<unsigned short>(diagnostic_port));
    assert(connect(diagnostic_peer, reinterpret_cast<sockaddr *>(&address), sizeof address) == 0);
    wait([&] {
        return diagnostic.status().connected;
    });
    assert(diagnostic.status().fault.empty());
    diagnostic.stop();
    closesocket(diagnostic_peer);
    WSACleanup();
    std::filesystem::remove_all(root);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
