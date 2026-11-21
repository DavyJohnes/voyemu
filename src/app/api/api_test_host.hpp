#pragma once
// The host parts of api.test.cpp: api_macos.test.cpp and api_windows.test.cpp.
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// The bundled Android 9 profile on the 8155 board, read from the catalog.
const std::string &qinggan09_profile();
// A bundled Free A11 profile that declares the wheel provider.
const std::string &qinggan11_wheel_profile();
// Decodes the image at path with the image codec of the host and checks its size.
void image_dimensions(const std::filesystem::path &path, std::size_t width, std::size_t height);
// Recovery of an owned Windows session whose supervisor is gone; nothing to recover on macOS.
void owned_recovery_tests(const std::filesystem::path &base, const std::filesystem::path &self);
// Wheel release admission; it runs on POSIX hosts.
void wheel_release_admission_tests(const std::filesystem::path &base);

// A connected client of a new Windows named pipe at endpoint, alive until close().
class ConnectedPipe {
public:
    explicit ConnectedPipe(const std::filesystem::path &endpoint);
    ~ConnectedPipe();
    ConnectedPipe(const ConnectedPipe &) = delete;
    ConnectedPipe &operator=(const ConnectedPipe &) = delete;
    void close();

private:
    std::intptr_t server_ = -1, client_ = -1;
};
