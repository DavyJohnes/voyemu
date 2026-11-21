// AOSP sysdeps.h must precede the other ADB headers.
#include "sysdeps.h"

#include "adb_loopback.hpp"
#include "adb_loopback_socket.hpp"
#include "transport.h"

#include "adb_utils.h"
#include <algorithm>
#include <chrono>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
// The descriptor stays owned until both adapter threads have joined. Close only
// shuts it down, so concurrent read/write cannot act on a reused descriptor.
class LoopbackConnection final : public BlockingConnection {
public:
    explicit LoopbackConnection(unique_fd fd) : fd_(std::move(fd)) {}

    ~LoopbackConnection() override {
        Close();
    }

    bool Read(apacket *packet) override {
        auto deadline = Clock::time_point::max();

        if (!Transfer(&packet->msg, sizeof(packet->msg), false, deadline)) {
            return false;
        }

        if (packet->msg.magic != (packet->msg.command ^ 0xffffffffu) ||
            packet->msg.data_length > MAX_PAYLOAD || packet->msg.data_length > max_payload_) {
            return false;
        }

        packet->payload.resize(packet->msg.data_length);

        if (!Transfer(packet->payload.data(), packet->payload.size(), false, deadline)) {
            return false;
        }

        const auto version = packet->msg.command == A_CNXN ? packet->msg.arg0 : version_;

        if (version < A_VERSION_SKIP_CHECKSUM &&
            packet->msg.data_check != calculate_apacket_checksum(packet)) {
            return false;
        }

        if (packet->msg.command == A_CNXN) {
            version_ = std::min(packet->msg.arg0, static_cast<uint32_t>(A_VERSION));
            max_payload_ = std::min(static_cast<size_t>(packet->msg.arg1), MAX_PAYLOAD);
        }

        return true;
    }

    bool Write(apacket *packet) override {
        if (packet->msg.data_length != packet->payload.size() ||
            packet->payload.size() > MAX_PAYLOAD) {
            return false;
        }

        auto deadline = Clock::now() + 5s;

        return Transfer(&packet->msg, sizeof(packet->msg), true, deadline) &&
               Transfer(packet->payload.data(), packet->payload.size(), true, deadline);
    }

    bool DoTlsHandshake(RSA *, std::string *) override {
        return false;
    }

    void Reset() override {
        Close();
    }

    void Close() override {
        if (!closed_.exchange(true)) {
            adb_shutdown(fd_.get(), SHUT_RDWR);
        }
    }

private:
    bool Transfer(void *data, size_t size, bool writing, Clock::time_point &deadline) {
        auto *cursor = static_cast<uint8_t *>(data);

        while (size && !closed_.load()) {
            if (Clock::now() >= deadline) {
                return false;
            }

            adb_pollfd wait{fd_.get(), static_cast<short>(writing ? POLLOUT : POLLIN), 0};
            int ready = adb_loopback_poll(&wait, 100);

            if (ready < 0 && errno == EINTR) {
                continue;
            }

            if (ready < 0) {
                return false;
            }

            if (!ready) {
                continue;
            }

            // Read retained bytes even with POLLHUP so an exact final packet is
            // delivered before EOF. No blocking syscall follows the poll.
            ssize_t count = adb_loopback_transfer(fd_, cursor, size, writing);

            if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }

            if (count <= 0) {
                return false;
            }

            if (deadline == Clock::time_point::max()) {
                deadline = Clock::now() + 5s;
            }

            cursor += count;
            size -= static_cast<size_t>(count);
        }

        return size == 0 && !closed_.load();
    }

    unique_fd fd_;
    std::atomic<bool> closed_{false};
    uint32_t version_ = A_VERSION_MIN;
    size_t max_payload_ = MAX_PAYLOAD;
};
} // namespace

std::unique_ptr<BlockingConnection> adb_host_connect_emulator(uint32_t console_port,
                                                              const std::atomic<bool> &stopping) {
    if (console_port < 1024 || console_port > 65534 || console_port % 2 || stopping.load()) {
        return {};
    }

    unique_fd fd = adb_loopback_connect(static_cast<uint16_t>(console_port + 1), stopping);

    if (!fd.ok()) {
        return {};
    }

    if (stopping.load()) {
        return {};
    }

    return std::make_unique<LoopbackConnection>(std::move(fd));
}
