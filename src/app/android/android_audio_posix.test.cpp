#include "android_audio.hpp"
#include "android_audio_test_host.hpp"
#include "android_engine.hpp"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void wine_volume_root_case(const std::filesystem::path &) {}

void linux_pulse_audio_case() {
    using namespace voyemu;
    namespace fs = std::filesystem;

    const auto check = [](bool ok, const char *message) {
        if (!ok) {
            throw std::runtime_error(message);
        }
    };
    const auto fixture = fs::temp_directory_path() /
                         ("voyemu-pulse-" + random_token().substr(0, 8));
    ensure_private_directory(fixture / "pulse");
    struct Cleanup {
        fs::path fixture;
        int fd = -1;

        ~Cleanup() {
            if (fd >= 0) {
                ::close(fd);
            }

            std::error_code error;
            fs::remove_all(fixture, error);
        }
    } cleanup{fixture};
    const auto pathname = (fixture / "pulse/native").string();
    const auto server = "unix:" + pathname;
    const auto expect_rejected = [&](const char *value) {
        check(::setenv("PULSE_SERVER", value, 1) == 0, "set explicit PulseAudio fixture");
        bool denied = false;
        try {
            ProcessSpec invalid;
            configure_android_audio(invalid, true);
        } catch (const VoyemuError &error) {
            denied = error.code() == "ANDROID_AUDIO_MODE";
        }

        check(denied, "Linux audio accepted a nonlocal or ambiguous PulseAudio endpoint");
    };
    check(::setenv("XDG_RUNTIME_DIR", fixture.c_str(), 1) == 0, "set desktop runtime fixture");
    expect_rejected("tcp:127.0.0.1:4713");
    const auto expect_missing_socket_fallback = [&]() {
        ProcessSpec missing;
        missing.arguments = {"-no-window"};
        configure_android_audio(missing, true);
        check(missing.arguments == std::vector<std::string>{"-no-window", "-no-audio"} &&
                      !missing.environment.contains("QEMU_AUDIO_OUT_DRV") &&
                      android_sdk_headless_backend(missing.arguments),
              "missing PulseAudio socket must retain headless SDK and disable audio");
    };
    check(::setenv("PULSE_SERVER", server.c_str(), 1) == 0, "select missing explicit socket");
    expect_missing_socket_fallback();
    check(::unsetenv("PULSE_SERVER") == 0, "select missing default socket");
    expect_missing_socket_fallback();
    ProcessSpec silent;
    silent.arguments = {"-no-window"};
    configure_android_audio(silent, false);
    check(silent.arguments == std::vector<std::string>{"-no-window", "-no-audio"},
          "silent mode must not require a PulseAudio socket");
    check(android_sdk_headless_backend(silent.arguments),
          "silent -no-window selection must expect headless QEMU");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    check(pathname.size() < sizeof(address.sun_path), "PulseAudio fixture path too long");
    std::memcpy(address.sun_path, pathname.c_str(), pathname.size() + 1);
    cleanup.fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    check(cleanup.fd >= 0 && ::bind(cleanup.fd, reinterpret_cast<const sockaddr *>(&address),
                                    sizeof(address)) == 0,
          "bind fixture PulseAudio socket without listening");
    expect_missing_socket_fallback();
    check(::setenv("PULSE_SERVER", server.c_str(), 1) == 0, "select stale explicit socket");
    expect_missing_socket_fallback();
    check(::listen(cleanup.fd, 4) == 0, "listen on fixture PulseAudio socket");
    const auto accepted = [&](const std::vector<std::string> &arguments,
                              const std::vector<std::string> &expected) {
        ProcessSpec output;
        output.arguments = arguments;
        output.environment["QEMU_AUDIO_IN_DRV"] = "pa";
        output.environment["QEMU_AUDIO_ADC_VOICES"] = "4";
        output.environment["XDG_RUNTIME_DIR"] = "/session/private";
        configure_android_audio(output, true);
        check(output.environment.at("PULSE_SERVER") == server &&
                      output.environment.at("QEMU_AUDIO_DRV") == "pa" &&
                      output.environment.at("QEMU_AUDIO_OUT_DRV") == "pa" &&
                      output.environment.at("QEMU_AUDIO_IN_DRV") == "none" &&
                      output.environment.at("QEMU_AUDIO_ADC_VOICES") == "0",
              "Linux output is routed to the host PulseAudio socket without recording");
        check(output.arguments == expected,
              "Linux playback must use Qt when hiding the SDK window");
        check(!android_sdk_headless_backend(output.arguments),
              "hidden Qt playback must expect the regular QEMU executable");
    };
    accepted({"-verbose", "-no-window"}, {"-verbose", "-qt-hide-window"});
    check(::unsetenv("PULSE_SERVER") == 0, "select default desktop PulseAudio socket");
    accepted({}, {});
    check(!android_sdk_headless_backend({"-qemu", "-no-window"}),
          "QEMU arguments must not select the headless SDK executable");
    const auto ambiguous = fixture / "local socket";
    fs::rename(pathname, ambiguous);
    expect_rejected(("unix:" + ambiguous.string()).c_str());
    const auto nonsocket = fixture / "not-a-socket";
    std::ofstream(nonsocket) << "ordinary file";
    expect_rejected(("unix:" + nonsocket.string()).c_str());
    check(::unsetenv("PULSE_SERVER") == 0, "clear explicit PulseAudio fixture");
}
