#include "qnx_frame_grpc.hpp"
#include "frame_transport.hpp"
#include "media_rpc.hpp"
#include "model.hpp"
#include "process.hpp"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace voyemu;
using namespace std::chrono_literals;

namespace {
void expect(bool condition, const char *reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

void expect_failure(const media::Connection &connection, std::string_view reason) {
    try {
        (void)media::capture_image(connection, {0, 1920, 720}, 3000);
    } catch (const std::runtime_error &error) {
        expect(std::string_view(error.what()).find(reason) != std::string_view::npos, error.what());

        return;
    }

    throw std::runtime_error("unexpected gRPC capture success");
}

void expect_pixels(const media::Image &image, unsigned red) {
    const auto pixels = image.pixels();

    expect(image.width == frames::width && image.height == frames::height && image.display == 0,
           "gRPC dimensions or display differ");
    expect(pixels[0] == red && pixels[1] == 13 && pixels[2] == 5 && pixels[3] == 255,
           "gRPC top row did not convert BGRA to RGBA");
    expect(pixels[pixels.size() - 4] == 129, "gRPC bottom row differs");
}
} // namespace

int main(int argc, char **argv) {
    try {
        expect(argc == 2, "expected writable build directory");
        const auto directory = std::filesystem::path(argv[1]) /
                               ("qnx-frame-grpc-" + random_token());
        ensure_private_directory(directory);
        struct Cleanup {
            std::filesystem::path directory;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(directory, ignored);
            }
        } cleanup{directory};
        const auto backing = directory / "frame.bgra";
        const std::string token(32, 'a'), next_token(32, 'b');
        std::atomic<bool> cancel{false};
        std::promise<void> first, second, replacement;
        auto first_ready = first.get_future(), second_ready = second.get_future(),
             replacement_ready = replacement.get_future();
        // On a failing assertion, destroy the listener before joining the client.
        std::future<bool> stream;
        auto server = std::make_unique<frames::GrpcServer>(backing, token);
        const auto port = server->port();
        const media::Connection connection{port, token, directory};
        expect(!tcp_port_free(port), "gRPC endpoint did not bind");
        expect_failure({port, next_token, directory}, "grpc=16");
        expect_failure(connection, "TEST_FRAME_UNAVAILABLE");

        stream = std::async(std::launch::async, [&] {
            unsigned received = 0;
            try {
                media::stream_images(
                        connection, {0, 1920, 720}, cancel, [&](const media::Image &image) {
                            expect(image.sequence == received,
                                   "stream sequence should begin at zero and increase");
                            expect_pixels(image, received == 0 ? 29 : received == 1 ? 67 : 101);

                            if (++received == 1) {
                                first.set_value();
                            } else if (received == 2) {
                                second.set_value();
                            } else if (received == 3) {
                                replacement.set_value();
                            } else {
                                throw std::runtime_error("stream repeated an unchanged frame");
                            }
                        });
            } catch (const std::runtime_error &error) {
                if (received == 3 &&
                    std::string_view(error.what()).starts_with("MEDIA_RPC_UNKNOWN:")) {
                    return true;
                }

                throw;
            }

            return false;
        });

        auto writer = std::make_unique<frames::Writer>(backing);
        std::vector<std::byte> pixels(frames::pixel_bytes);
        pixels[0] = std::byte{5};
        pixels[1] = std::byte{13};
        pixels[2] = std::byte{29};
        pixels[3] = std::byte{255};
        pixels[pixels.size() - 2] = std::byte{129};
        const auto publish = [&](std::uint64_t sequence) {
            const auto now = std::chrono::steady_clock::now().time_since_epoch();
            writer->publish(pixels, sequence,
                            std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
        };
        publish(72);
        expect(first_ready.wait_for(4s) == std::future_status::ready,
               "first streamed frame timed out");
        const auto captured = media::capture_image(connection, {0, 1920, 720}, 3000);
        expect(captured.sequence == 0, "unary screenshot sequence differs");
        expect_pixels(captured, 29);
        pixels[2] = std::byte{67};
        publish(73);
        expect(second_ready.wait_for(4s) == std::future_status::ready,
               "second streamed frame timed out");

        writer.reset();
        frames::invalidate(backing);
        expect_failure(connection, "TEST_FRAME_UNAVAILABLE");
        std::filesystem::rename(backing, directory / "frame-previous.bgra");
        writer = std::make_unique<frames::Writer>(backing);
        pixels[2] = std::byte{101};
        publish(1);
        expect(replacement_ready.wait_for(4s) == std::future_status::ready,
               "replaced producer frame timed out");

        const auto close_started = std::chrono::steady_clock::now();
        server.reset();
        expect(std::chrono::steady_clock::now() - close_started < 2s,
               "listener teardown waited for a stream");
        expect(stream.wait_for(4s) == std::future_status::ready && stream.get(),
               "stream did not disconnect on teardown");
        expect_failure(connection, "curl=7");

        frames::GrpcServer restarted(backing, next_token);
        expect_failure({restarted.port(), token, directory}, "grpc=16");
        expect_pixels(media::capture_image({restarted.port(), next_token, directory},
                                           {0, 1920, 720}, 3000),
                      101);
        std::cout << "QNX gRPC capture, frame stream, credentials, teardown and restart passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
