#include "media_rpc.hpp"
#include "media_rpc_test_host.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;

template <class Function> void rejects(Function f) {
    bool rejected = false;

    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
}

int main(int argc, char **argv) try {
    if (argc == 4 && std::string_view(argv[1]) == "--rpc-failure") {
        const auto port = static_cast<unsigned>(std::stoul(argv[2]));
        const std::string secret = "MEDIA_RPC_PRIVATE_SENTINEL_7941";
        std::string observed;
        try {
            media::capture_image({port, secret, {}}, {0, 1920, 720}, 1500);
        } catch (const std::exception &error) {
            observed = error.what();
        }

        const std::string expected =
                "MEDIA_RPC_UNKNOWN: local SDK request did not complete method=getScreenshot " +
                std::string(argv[3]);

        if (observed != expected) {
            std::cerr << "observed: " << observed << '\n';
        }

        assert(observed == expected);

        for (const auto forbidden :
             {secret, std::string("127.0.0.1"), std::string("authorization"),
              std::string("grpc-message"), std::string("/android.emulation.control")}) {
            assert(observed.find(forbidden) == std::string::npos);
        }

        std::cout << observed << '\n';

        return 0;
    }

    const auto project = std::filesystem::absolute(__FILE__)
                                 .parent_path()
                                 .parent_path()
                                 .parent_path()
                                 .parent_path();
    const auto base = argc == 2 ? std::filesystem::path(argv[1]) : project / ".local/build/tmp";
    const std::string id = "0123456789abcdef0123456789abcdef";
    // The rear screen region has its own extent.
    const auto pixel_bytes = media::GuestDisplay{2, 1902, 540}.pixel_bytes();
    const auto root = base / ("voyemu-media-rpc-" + random_token());
    ensure_private_directory(root);
    const auto descriptor = root / "pid_123.ini", record = root / "connection.json";
    auto ini = [&](const std::string &extra = "") {
        // The SDK descriptor of a Windows engine must be owned by the current user.
        if (voyemu::os::host_windows && !std::filesystem::exists(descriptor)) {
            assert(voyemu::os::create_private_file(descriptor));
        }

        std::ofstream file(descriptor);
        file << "port.serial=5566\ngrpc.port=45992\ngrpc.token=" << std::string(32, '1') << "\n"
             << extra;
    };
    ini();
    write_private_json(record, {{"schema", 1},
                                {"session_id", id},
                                {"vm_pid", 123},
                                {"grpc_port", 45992},
                                {"serial_port", 5566},
                                {"discovery", descriptor.string()}});
    auto connection = media::read_connection(record, id);
    assert(connection.port == 45992 && connection.bearer == std::string(32, '1'));
    rejects([&] {
        media::read_connection(record, "another-session");
    });
    ini("grpc.port=1234\n");
    rejects([&] {
        media::read_connection(record, id);
    });
    ini();
    { std::ofstream empty(descriptor); }
    rejects([&] {
        media::read_connection(record, id);
    });
    ini(std::string(32769, 'x'));
    rejects([&] {
        media::read_connection(record, id);
    });
    ini();
    std::filesystem::rename(descriptor, root / "saved.ini");
    std::error_code symlink_error;
    std::filesystem::create_symlink(root / "saved.ini", descriptor, symlink_error);

    if (symlink_error) {
        std::cout << "SKIP descriptor reparse fixture: " << symlink_error.message() << '\n';
    } else {
        rejects([&] {
            media::read_connection(record, id);
        });
        std::filesystem::remove(descriptor);
    }

    std::filesystem::rename(root / "saved.ini", descriptor);
    std::error_code hardlink_error;
    std::filesystem::create_hard_link(descriptor, root / "alias.ini", hardlink_error);

    if (hardlink_error) {
        std::cout << "SKIP descriptor hardlink fixture: " << hardlink_error.message() << '\n';
    } else {
        rejects([&] {
            media::read_connection(record, id);
        });
        std::filesystem::remove(root / "alias.ini");
    }

    // The foreign owner case assigns Everyone as the owner of a Windows file.
    if (voyemu::os::host_windows) {
        const bool foreign = assign_world_owner(descriptor);

        if (foreign) {
            rejects([&] {
                media::read_connection(record, id);
            });
            std::filesystem::remove(descriptor);
            ini();
        } else {
            std::cout
                    << "SKIP descriptor foreign owner: assignment not confirmed by SID readback\n";
        }
    }

    rejects([&] {
        media::send_input(connection, "arbitrary-method", {});
    });
    rejects([&] {
        media::capture_image(connection, {11, 1920, 720});
    });
    rejects([&] {
        media::capture_image(connection, {0, 3840, 2160});
    });
    rejects([&] {
        media::capture_image(connection, {0, 1920, 720}, 0);
    });
    rejects([&] {
        media::MappedFrame frame(root / "buffers", 0);
    });
    rejects([&] {
        media::MappedFrame frame(root / "buffers", media::maximum_pixel_bytes + 1);
    });
    assert(connection.frame_directory == root / "media-frames");
    assert(!std::filesystem::exists(connection.frame_directory));

    // Windows pins the mapped frame file against deletion and truncation and may refuse links.
    if (voyemu::os::host_windows) {
        std::filesystem::path frame_path;
        {
            media::MappedFrame frame(root / "buffers", pixel_bytes);
            const auto handle = frame.handle();
            assert(handle.starts_with("file:///"));
            frame_path = handle.substr(std::string("file:///").size());
            assert(private_frame_file(frame_path, pixel_bytes));
            {
                std::fstream file(frame_path, std::ios::in | std::ios::out | std::ios::binary);
                file.put(33);
            }
            assert(frame.pixels().front() == 33);
        }
        assert(!std::filesystem::exists(frame_path));
        rejects([&] {
            media::MappedFrame frame("relative-directory", pixel_bytes);
        });
        std::error_code frame_symlink_error;
        std::filesystem::create_symlink(root / "buffers", root / "linked-buffers",
                                        frame_symlink_error);

        if (frame_symlink_error) {
            std::cout << "SKIP frame reparse fixture: " << frame_symlink_error.message() << '\n';
        } else {
            rejects([&] {
                media::MappedFrame frame(root / "linked-buffers", pixel_bytes);
            });
            std::filesystem::remove(root / "linked-buffers");
        }

        // Windows pins a mapped file against deletion and truncation, so the hard link and the
        // extent change are attempted while the region is live and cleaned up after release.
        bool frame_alias = false;
        {
            media::MappedFrame frame(root / "buffers", pixel_bytes);
            frame_path = frame.handle().substr(std::string("file:///").size());
            std::error_code frame_hardlink_error;
            std::filesystem::create_hard_link(frame_path, root / "frame-alias",
                                              frame_hardlink_error);

            if (frame_hardlink_error) {
                std::cout << "SKIP frame hardlink fixture: " << frame_hardlink_error.message()
                          << '\n';
            } else {
                frame_alias = true;
                rejects([&] {
                    frame.pixels();
                });
            }

            std::error_code frame_resize_error;
            std::filesystem::resize_file(frame_path, 8, frame_resize_error);

            if (frame_resize_error) {
                std::cout << "frame extent refused by pinned mapping: "
                          << frame_resize_error.message() << '\n';
            } else {
                rejects([&] {
                    frame.pixels();
                });
            }
        }

        if (frame_alias) {
            assert(std::filesystem::exists(frame_path));
            std::filesystem::remove(root / "frame-alias");
            std::filesystem::remove(frame_path);
        }

        std::atomic<bool> stopped{true};
        media::ScreenshotStream raw(connection, {0, 1920, 720}, true);
        raw.run(stopped, [](media::Image) {
            assert(false);
        });
        assert(!std::filesystem::exists(connection.frame_directory));
    } else {
        std::filesystem::path frame_path;
        {
            media::MappedFrame frame(root / "buffers", pixel_bytes);
            frame_path = frame.handle().substr(7);
            assert(private_frame_file(frame_path, pixel_bytes));
            {
                std::fstream file(frame_path, std::ios::in | std::ios::out | std::ios::binary);
                file.put(33);
            }
            assert(frame.pixels().front() == 33);
            std::filesystem::create_hard_link(frame_path, root / "frame-alias");
            rejects([&] {
                frame.pixels();
            });
            std::filesystem::remove(root / "frame-alias");
            std::filesystem::resize_file(frame_path, 8);
            rejects([&] {
                frame.pixels();
            });
            std::filesystem::resize_file(frame_path, pixel_bytes);
        }
        assert(!std::filesystem::exists(frame_path));
        rejects([&] {
            media::MappedFrame frame("relative-directory", pixel_bytes);
        });
        std::filesystem::create_symlink(root / "buffers", root / "linked-buffers");
        rejects([&] {
            media::MappedFrame frame(root / "linked-buffers", pixel_bytes);
        });
        std::filesystem::remove(root / "linked-buffers");
    }

    std::filesystem::remove_all(root);
    std::cout << "owned SDK discovery, stale epoch and closed-method guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
