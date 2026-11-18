#include "interguest_link.hpp"
#include "interguest_network.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <functional>
#ifndef _WIN32
#include <array>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#endif
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
std::atomic<unsigned> attempts = 0;
std::string transport_fault;
bool connected_mode = false;
std::function<void()> receive_hook, status_hook;

void wait_fault(voyemu::InterguestLink &link) {
    for (unsigned i = 0; i < 2000; ++i) {
        if (!link.status().fault.empty()) {
            return;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(false);
}
} // namespace
// Inject a transport ownership failure, without substituting the link worker or its lifecycle.
namespace voyemu {
struct InterguestNetwork::Impl {
    Status state;
};

InterguestNetwork::InterguestNetwork() : impl_(std::make_unique<Impl>()) {}

InterguestNetwork::~InterguestNetwork() = default;

unsigned InterguestNetwork::start() {
    impl_->state.port = 12345;
    impl_->state.connected = connected_mode;

    return impl_->state.port;
}

unsigned InterguestNetwork::start_client() {
    return start();
}

void InterguestNetwork::bind_peer(std::uint64_t) {}

void InterguestNetwork::stop() noexcept {
    impl_->state = {};
}

bool InterguestNetwork::accept(int) {
    ++attempts;
    throw std::runtime_error(transport_fault);
}

bool InterguestNetwork::connect(int timeout) {
    return accept(timeout);
}

bool InterguestNetwork::receive(std::vector<std::uint8_t> &, int) {
    assert(receive_hook);
    receive_hook();

    return false;
}

void InterguestNetwork::send(std::span<const std::uint8_t>, int) {
    assert(false);
}

// NOLINTNEXTLINE(bugprone-exception-escape): status_hook is called only when it is set.
InterguestNetwork::Status InterguestNetwork::status() const noexcept {
    if (status_hook) {
        status_hook();
    }

    return impl_->state;
}
} // namespace voyemu

int main(int argc, char **argv) try {
    assert(argc == 2);
    const auto root = std::filesystem::path(argv[1]) /
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string token = "0123456789abcdef0123456789abcdef";
    const auto dir = root / token;
    std::filesystem::create_directories(dir);

    for (const auto &fault :
         {"INTERGUEST_OWNER: foreign listener at selected endpoint",
          "INTERGUEST_STATE: invalid transport", "INTERGUEST_THREAD: unknown failure"}) {
        transport_fault = fault;
        attempts = 0;
        voyemu::InterguestLink link;
        const auto port = link.start(dir, token);
        wait_fault(link);
        assert(attempts == 1);

        for (unsigned n = 0; n < 2; ++n) {
            link.clear_ram();
            assert(link.status().fault == fault && link.status().port == port &&
                   !link.status().ready);
            bool rejected = false;
            try {
                link.check();
            } catch (const std::exception &error) {
                rejected = error.what() == transport_fault;
            }

            assert(rejected);
            rejected = false;
            try {
                link.bind_ram(dir / "qnx-ram.bin");
            } catch (const std::exception &error) {
                rejected = std::string(error.what()) == "INTERGUEST_STATE: inactive bridge";
            }

            assert(rejected);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        assert(attempts == 1);
        link.stop();
        link.clear_ram();
        assert(link.status().port == 0 && attempts == 1);
        link.stop();
    }

#ifndef _WIN32
    // Hold the real mailbox worker inside its network poll, then invalidate RAM
    // before it samples counters. This deterministically reproduces a guest reset.
    const auto ram = dir / "qnx-ram.bin";
    const int fd = open(ram.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(fd >= 0 && ftruncate(fd, 0x200000000ULL) == 0);
    auto *mapping = static_cast<unsigned char *>(
            mmap(nullptr, 16384, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
    assert(mapping != MAP_FAILED);
    auto *words = reinterpret_cast<std::uint32_t *>(mapping + 4096);
    std::array<std::uint32_t, 8> ring{};
    ring[0] = 0xbeefeeee;
    ring[1] = 16352;
    const std::string marker = "QNET READY pid=42 paddr=0x40001000 length=4096 version=1\r\n";
    ring[2] = marker.size();
    assert(pwrite(fd, ring.data(), 32, 0x10010000) == 32 &&
           pwrite(fd, marker.data(), marker.size(), 0x10010020) ==
                   static_cast<ssize_t>(marker.size()));
    connected_mode = true;

    for (unsigned invalid : {0U, 1U, 6U, 7U}) {
        std::memset(words, 0, 4096);
        words[0] = 0x54454e51;
        words[1] = 1;
        std::atomic<bool> entered = false, released = false, poll_returned = false,
                          restored = false;
        receive_hook = [&] {
            entered = true;

            for (unsigned i = 0; !released && i < 2000; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            assert(released);
            poll_returned = true;
        };
        // Restore the header at the first transport observation after publication.
        // A missing post-poll validation would then expose poisoned counters without
        // a later fault to clear them; checking only the final cache is insufficient.
        status_hook = [&] {
            if (!poll_returned.exchange(false)) {
                return;
            }

            std::atomic_ref<std::uint32_t>(words[invalid])
                    .store(invalid == 0   ? 0x54454e51U
                           : invalid == 1 ? 1U
                                          : 0U,
                           std::memory_order_release);
            restored = true;
        };
        voyemu::InterguestLink link;
        const auto port = link.start(dir, token);
        link.bind_ram(ram);

        for (unsigned i = 0; !entered && i < 2000; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        assert(entered);
        std::atomic_ref<std::uint32_t>(words[8]).store(2630172672U, std::memory_order_release);
        std::atomic_ref<std::uint32_t>(words[9]).store(1784781U, std::memory_order_release);
        std::atomic_ref<std::uint32_t>(words[invalid])
                .store(invalid == 0 ? 0U : 2U, std::memory_order_release);
        released = true;
        wait_fault(link);
        assert(link.status().fault == "INTERGUEST_PAGE: relay stopped or invalid" &&
               !link.status().ready && link.status().port == port && link.status().generation == 1);
        assert(!restored && link.guest_counters().sent == 0 && link.guest_counters().received == 0);
        link.stop();
        status_hook = {};
    }

    connected_mode = false;
    receive_hook = {};
    assert(munmap(mapping, 16384) == 0 && close(fd) == 0);
#endif
    std::filesystem::remove_all(root);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
