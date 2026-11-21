#include "media_rpc.hpp"
#include "model.hpp"
#include "process.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "os/private_state_security.hpp"
#include <windows.h>
#include <aclapi.h>

#include <vector>
#else
#include <exception>
#include <sys/stat.h>
#include <unistd.h>
#endif
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
#ifdef _WIN32
        if (!std::filesystem::exists(descriptor)) {
            voyemu::detail::PrivateStateSecurity security;
            HANDLE owned = CreateFileW(descriptor.c_str(), GENERIC_WRITE, 0, &security.attributes,
                                       CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            assert(owned != INVALID_HANDLE_VALUE && CloseHandle(owned));
        }
#endif
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

#ifdef _WIN32
    // Wine may report successful owner assignment without changing the owner SID.
    HANDLE file = CreateFileW(descriptor.c_str(), WRITE_OWNER | READ_CONTROL,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    bool foreign = false;

    if (file != INVALID_HANDLE_VALUE) {
        BYTE sid[SECURITY_MAX_SID_SIZE]{};
        DWORD size = sizeof(sid);
        assert(CreateWellKnownSid(WinWorldSid, nullptr, sid, &size));
        const auto changed = SetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, sid,
                                             nullptr, nullptr, nullptr);
        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;
        const auto observed = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                              &owner, nullptr, nullptr, nullptr, &security);
        foreign = changed == ERROR_SUCCESS && observed == ERROR_SUCCESS && owner &&
                  EqualSid(owner, sid);

        if (security) {
            LocalFree(security);
        }

        CloseHandle(file);
    }

    if (foreign) {
        rejects([&] {
            media::read_connection(record, id);
        });
        std::filesystem::remove(descriptor);
        ini();
    } else {
        std::cout << "SKIP descriptor foreign owner: assignment not confirmed by SID readback\n";
    }
#endif
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
#ifdef _WIN32
    const auto owned_by_current_user = [&](const std::filesystem::path &path) {
        HANDLE file = CreateFileW(path.c_str(), READ_CONTROL,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }

        HANDLE token = nullptr;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            CloseHandle(file);

            return false;
        }

        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<std::byte> user(size);
        const bool loaded = size && GetTokenInformation(token, TokenUser, user.data(), size, &size);
        CloseHandle(token);

        if (!loaded) {
            CloseHandle(file);

            return false;
        }

        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR security = nullptr;
        const auto status = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                            &owner, nullptr, nullptr, nullptr, &security);
        const bool owned = status == ERROR_SUCCESS && owner &&
                           EqualSid(owner, reinterpret_cast<TOKEN_USER *>(user.data())->User.Sid);

        if (security) {
            LocalFree(security);
        }

        CloseHandle(file);

        return owned;
    };
    std::filesystem::path frame_path;
    {
        media::MappedFrame frame(root / "buffers", pixel_bytes);
        const auto handle = frame.handle();
        assert(handle.starts_with("file:///"));
        frame_path = handle.substr(std::string("file:///").size());
        assert(std::filesystem::file_size(frame_path) == pixel_bytes &&
               owned_by_current_user(frame_path));
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
    std::filesystem::create_symlink(root / "buffers", root / "linked-buffers", frame_symlink_error);

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
        std::filesystem::create_hard_link(frame_path, root / "frame-alias", frame_hardlink_error);

        if (frame_hardlink_error) {
            std::cout << "SKIP frame hardlink fixture: " << frame_hardlink_error.message() << '\n';
        } else {
            frame_alias = true;
            rejects([&] {
                frame.pixels();
            });
        }

        std::error_code frame_resize_error;
        std::filesystem::resize_file(frame_path, 8, frame_resize_error);

        if (frame_resize_error) {
            std::cout << "frame extent refused by pinned mapping: " << frame_resize_error.message()
                      << '\n';
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
#else
    std::filesystem::path frame_path;
    {
        media::MappedFrame frame(root / "buffers", pixel_bytes);
        frame_path = frame.handle().substr(7);
        struct stat st{};
        assert(::stat(frame_path.c_str(), &st) == 0 &&
               st.st_size == static_cast<off_t>(pixel_bytes) && (st.st_mode & 0077) == 0);
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
#endif

    std::filesystem::remove_all(root);
    std::cout << "owned SDK discovery, stale epoch and closed-method guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
