#pragma once
#include "../core/model.hpp"
#include "../core/process.hpp"
namespace voyemu {
class WineNativeBackendWindows {
public:
    static std::unique_ptr<WineNativeBackendWindows> launch(const ProcessSpec &spec);
    bool alive();
    bool owns_tcp_listener(unsigned port);

    std::int64_t pid() const {
        return pid_;
    }

    int exit_code() const;
    void terminate(int grace_ms);

private:
    Json proof() const;
    mutable ChildProcess proxy_;
    std::filesystem::path proof_, stop_;
    std::string nonce_, executable_;
    Json identity_;
    std::int64_t pid_ = 0;
    mutable std::optional<int> terminal_exit_;
};
bool wine_native_host();
std::string wine_native_unix_path(const std::filesystem::path &path);
std::filesystem::path wine_native_windows_path(std::string_view path);
int wine_native_proxy_main(int argc, char **argv);
} // namespace voyemu
