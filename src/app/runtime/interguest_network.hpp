#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace voyemu {
// One owned loopback peer speaking QEMU socket Ethernet framing. No forwarding,
// host interfaces, NAT, IP stack or arbitrary outbound connections.
class InterguestNetwork {
public:
    struct Status {
        unsigned port = 0;
        bool connected = false;
        std::uint64_t received = 0, sent = 0;
    };
    static constexpr std::size_t maximum_frame = 2048;
    InterguestNetwork();
    ~InterguestNetwork();
    InterguestNetwork(const InterguestNetwork &) = delete;
    InterguestNetwork &operator=(const InterguestNetwork &) = delete;
    unsigned start();
    unsigned start_client();
    void bind_peer(std::uint64_t pid);
    void stop() noexcept;
    bool accept(int timeout_ms);
    bool connect(int timeout_ms);
    // Timeout keeps a partial receive for the next call. A complete frame is
    // consumed only once. Disconnect/malformed wire closes the peer and throws.
    bool receive(std::vector<std::uint8_t> &frame, int timeout_ms);
    // A send timeout closes the peer: a partially sent frame cannot be retried.
    void send(std::span<const std::uint8_t> frame, int timeout_ms);
    Status status() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace voyemu
