#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace voyemu {
struct WineNativeDiagnostic {
    std::filesystem::path launcher;
    std::filesystem::path request_directory;
    std::vector<unsigned> ports;
    std::uint64_t timeout_ms = 7200000;
};
struct ProcessSpec {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::map<std::string, std::string> environment;
    std::filesystem::path directory;
    std::filesystem::path output;
    bool detached = false;
    std::optional<WineNativeDiagnostic> wine_native_diagnostic;
};
struct ProcessResult {
    int exit_code;
    std::string output;
    bool timed_out = false;
};
class ChildProcess {
public:
    struct Impl;
    ChildProcess();
    ~ChildProcess();
    ChildProcess(ChildProcess &&) noexcept;
    ChildProcess &operator=(ChildProcess &&) noexcept;
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;
    static ChildProcess launch(const ProcessSpec &spec);
    using OutputSanitizer = std::function<std::string(std::string_view, std::int64_t, bool)>;
    // Only sanitized bytes reach output; one bounded reader belongs to this child.
    static ChildProcess launch_sanitized(const ProcessSpec &spec, OutputSanitizer sanitizer,
                                         std::size_t output_limit = 4 * 1024 * 1024);
    bool sanitized_output_complete() const;
    bool sanitized_output_failed() const;
    bool alive();
    bool owns_tcp_listener(unsigned port);
    std::int64_t pid() const;
    std::string_view pid_namespace() const;
    int exit_code() const;
    void terminate(int grace_ms = 5000);
    void detach();

private:
    std::unique_ptr<Impl> impl_;
};
ProcessResult execute(const ProcessSpec &spec, int timeout_ms = 10000,
                      std::size_t output_limit = 128 * 1024);
// Explicit finite lifetime for source artifact preparation, up to the one-hour budget of the Java
// helper; ordinary capture keeps its shorter bound.
ProcessResult execute_artifact_preparation(const ProcessSpec &spec, int timeout_ms,
                                           std::size_t output_limit = 128 * 1024);
std::string random_token();
std::string sha256_bytes(std::string_view bytes);
std::string sha256_file(const std::filesystem::path &path);
// The same digest; advance receives the byte count of each block read, for a progress meter.
std::string sha256_file(const std::filesystem::path &path,
                        const std::function<void(std::uint64_t)> &advance);
std::string sha256_file_range(const std::filesystem::path &path, std::uint64_t offset,
                              std::uint64_t bytes);
std::string windows_quote(const std::string &argument);
std::string android_shell_quote(const std::string &argument);
bool tcp_port_free(unsigned port);
struct TcpPortProbe {
    unsigned port = 0;
    int family = 0;
    const char *stage = "unchecked";
    unsigned error = 0;
    std::uint32_t listener_pid = 0;
    std::array<char, 64> listener_address{};
};
// Optional metadata describes the same probe, never a second bind or ownership claim.
bool tcp_port_free(unsigned port, TcpPortProbe *diagnostic);
} // namespace voyemu
