#pragma once
#include "media_wire.hpp"
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>

namespace voyemu::media {
// HTTP/2 uses system libcurl on macOS and pinned static libcurl on Windows.
struct Connection {
    unsigned port{};
    std::string bearer;
    std::filesystem::path frame_directory;
};
Connection read_connection(const std::filesystem::path &record, std::string_view session_id);
// Private client-owned region of one screen frame shared with the local SDK on macOS and Windows.
class MappedFrame {
public:
    MappedFrame(const std::filesystem::path &directory, std::size_t bytes);
    ~MappedFrame();
    MappedFrame(const MappedFrame &) = delete;
    MappedFrame &operator=(const MappedFrame &) = delete;
    std::string handle() const;
    std::span<const std::uint8_t> pixels() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Keeps the SDK handle alive across reconnects; mapped pixels are copied after a completed unary
// reply.
class ScreenshotStream {
public:
    ScreenshotStream(Connection connection, GuestDisplay display, bool mapped);
    ~ScreenshotStream();
    void run(const std::atomic<bool> &stop, std::function<void(Image)> receiver);

private:
    Connection connection_;
    GuestDisplay display_;
    bool mapped_;
    std::unique_ptr<MappedFrame> frame_, notification_;
};
void stream_images(const Connection &, GuestDisplay display, const std::atomic<bool> &stop,
                   std::function<void(Image)>);
Image capture_image(const Connection &, GuestDisplay display, int timeout_ms = 1500);
void send_input(const Connection &, std::string_view method, std::span<const std::uint8_t> payload,
                int timeout_ms = 1500);
} // namespace voyemu::media
