#pragma once
#include "model.hpp"
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>

namespace voyemu {
// The local stream to the QMP monitor of the owned QNX engine: a Unix-domain socket in a private
// directory on POSIX (qnx_shutdown_channel_posix.cpp), a loopback TCP port on Windows
// (qnx_shutdown_channel_windows.cpp). Every unverified step throws QNX_SHUTDOWN_MONITOR.
class QnxShutdownChannel {
public:
    using Deadline = std::chrono::steady_clock::time_point;

    explicit QnxShutdownChannel(const Json &endpoint);
    ~QnxShutdownChannel();
    QnxShutdownChannel(const QnxShutdownChannel &) = delete;
    QnxShutdownChannel &operator=(const QnxShutdownChannel &) = delete;

    // Whether the endpoint names a channel this host can own before the engine creates it.
    bool valid() const;
    // Connects to the engine pid before deadline; POSIX also proves the peer and the socket file.
    void connect(std::int64_t pid, Deadline deadline);
    // Proves the channel between protocol steps: POSIX checks that the socket file is unchanged,
    // Windows calls verify_owner when it is set.
    void verify(const std::function<void()> &verify_owner);
    void wait(bool writable, Deadline deadline);
    // One receive or send: the byte count, 0 at the end of the stream, -1 when it would block.
    int receive(char *bytes, int count);
    int send(const char *bytes, int count);
    // Closes the connection; finish() also removes the POSIX socket file of the exited engine.
    void close() noexcept;
    void finish();

private:
    struct State;
    std::unique_ptr<State> state_;
};

void qnx_shutdown_channel_require(bool yes);
} // namespace voyemu
