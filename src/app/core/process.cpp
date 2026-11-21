#include "process.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/digest.hpp"
#include "os/random.hpp"
#include "os/socket.hpp"
#include "process_internal.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
namespace voyemu {
void check_process_string(const std::string &value) {
    if (value.find('\0') != std::string::npos) {
        throw VoyemuError("PROCESS_ARGUMENT", "embedded NUL");
    }
}

std::string windows_quote(const std::string &value) {
    check_process_string(value);
    std::string result = "\"";
    std::size_t slashes = 0;

    for (auto character : value) {
        if (character == '\\') {
            ++slashes;
            continue;
        }

        if (character == '"') {
            result.append(slashes * 2 + 1, '\\');
            result += '"';
        } else {
            result.append(slashes, '\\');
            result += character;
        }

        slashes = 0;
    }

    result.append(slashes * 2, '\\');
    result += '"';

    return result;
}

std::string android_shell_quote(const std::string &value) {
    check_process_string(value);

    if (value.find_first_of("\r\n") != std::string::npos) {
        throw VoyemuError("ANDROID_ARGUMENT", "newline rejected");
    }

    std::string output = "'";

    for (auto ch : value) {
        if (ch == '\'') {
            output += "'\\''";
        } else {
            output += ch;
        }
    }

    return output + "'";
}

std::string random_token() {
    std::array<unsigned char, 16> bytes{};

    os::random_bytes(bytes.data(), bytes.size());
    std::ostringstream output;
    output << std::hex << std::setfill('0');

    for (auto byte : bytes) {
        output << std::setw(2) << unsigned(byte);
    }

    return output.str();
}

// Portable SHA-256 retains exact source pins while hashing large firmware in chunks.
std::string sha256_bytes(std::string_view bytes) {
    os::Sha256 hash;

    hash.update(bytes.data(), bytes.size());

    return os::hex_digest(hash.finish());
}

std::string sha256_file(const std::filesystem::path &path) {
    return sha256_file(path, {});
}

std::string sha256_file(const std::filesystem::path &path,
                        const std::function<void(std::uint64_t)> &advance) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) {
        throw VoyemuError("PIN_PATH", "regular nonsymlink input required");
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw VoyemuError("PIN_READ", "cannot open input");
    }

    std::array<char, 64 * 1024> buffer{};
    os::Sha256 hash;

    while (file) {
        interruption::check();
        file.read(buffer.data(), buffer.size());
        hash.update(buffer.data(), std::size_t(file.gcount()));

        if (advance && file.gcount() > 0) {
            advance(static_cast<std::uint64_t>(file.gcount()));
        }
    }

    if (!file.eof()) {
        throw VoyemuError("PIN_READ", "input read failed");
    }

    return os::hex_digest(hash.finish());
}

std::string sha256_file_range(const std::filesystem::path &path, std::uint64_t offset,
                              std::uint64_t bytes) {
    if (std::filesystem::is_symlink(path) || !std::filesystem::is_regular_file(path)) {
        throw VoyemuError("PIN_PATH", "regular nonsymlink input required");
    }

    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw VoyemuError("PIN_READ", "cannot open input");
    }

    const auto size = std::filesystem::file_size(path);

    if (offset > size || bytes > size - offset) {
        throw VoyemuError("PIN_RANGE", "bounded file range required");
    }

    file.seekg(static_cast<std::streamoff>(offset));
    std::array<char, 64 * 1024> buffer{};
    os::Sha256 hash;

    while (bytes) {
        interruption::check();
        const auto count = std::min<std::uint64_t>(bytes, buffer.size());
        file.read(buffer.data(), count);

        if (std::uint64_t(file.gcount()) != count) {
            throw VoyemuError("PIN_READ", "incomplete file range");
        }

        bytes -= count;
        hash.update(buffer.data(), count);
    }

    if (!file) {
        throw VoyemuError("PIN_READ", "input range read failed");
    }

    return os::hex_digest(hash.finish());
}

ProcessResult execute(const ProcessSpec &spec, int timeout_ms, std::size_t output_limit) {
    return execute_capture(spec, timeout_ms, output_limit, 600000);
}

ProcessResult execute_artifact_preparation(const ProcessSpec &spec, int timeout_ms,
                                           std::size_t output_limit) {
    return execute_capture(spec, timeout_ms, output_limit, 3600000);
}

bool tcp_port_free(unsigned port) {
    return tcp_port_free(port, nullptr);
}

bool tcp_port_free(unsigned port, TcpPortProbe *diagnostic) {
    if (diagnostic) {
        *diagnostic = {};
        diagnostic->port = port;
    }

    auto fail = [&](const char *stage, unsigned error) {
        if (diagnostic) {
            diagnostic->stage = stage;
            diagnostic->error = error;
        }

        return false;
    };

    if (port > 65535 || port == 0) {
        return fail("port", 0);
    }

    const os::SocketLibrary network;

    if (network.error()) {
        return fail("startup", network.error());
    }

    for (int family : {AF_INET, AF_INET6}) {
        if (diagnostic) {
            diagnostic->family = family;
        }

        sockaddr_storage storage{};
        sockaddr *address = reinterpret_cast<sockaddr *>(&storage);
        const auto address_size = static_cast<os::SocketLength>(
                family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));

        if (family == AF_INET) {
            auto &local = *reinterpret_cast<sockaddr_in *>(&storage);
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            local.sin_port = htons(static_cast<std::uint16_t>(port));
        } else {
            auto &local = *reinterpret_cast<sockaddr_in6 *>(&storage);
            local.sin6_family = AF_INET6;
            local.sin6_addr = in6addr_loopback;
            local.sin6_port = htons(static_cast<std::uint16_t>(port));
        }

        if (const auto refusal = os::loopback_listener(family, address, address_size, port,
                                                       diagnostic)) {
            return fail(refusal->stage, refusal->error);
        }

        const auto fd = os::open_stream_socket(family);

        if (fd == os::no_socket) {
            return fail("socket", os::socket_error());
        }

        const os::SocketCloser socket_cleanup{fd};

        if (!os::reuse_address(fd)) {
            return fail("reuse", os::socket_error());
        }

        if (!os::bind_socket(fd, address, address_size)) {
            return fail("bind", os::socket_error());
        }

        if (!os::listen_socket(fd, 1)) {
            return fail("listen", os::socket_error());
        }
    }

    if (diagnostic) {
        diagnostic->stage = "free";
    }

    return true;
}

SanitizedProcessOutput::~SanitizedProcessOutput() {
    stop = true;

    if (reader.joinable()) {
        reader.join();
    }

    if (read != os::no_file) {
        os::close_file(read);
    }

    if (file != os::no_file) {
        os::close_file(file);
    }
}

void SanitizedProcessOutput::run(ChildProcess::OutputSanitizer sanitizer, std::int64_t pid,
                                 std::size_t limit) noexcept {
    std::size_t written = 0;
    std::array<char, 16384> bytes{};
    const auto emit = [&](std::string_view raw, bool eof) {
        if (failed.load()) {
            return;
        }

        try {
            auto safe = sanitizer(raw, pid, eof);

            if (safe.size() > 32768 || safe.size() > limit - written) {
                failed = true;

                return;
            }

            std::size_t offset = 0;

            while (offset < safe.size()) {
                const auto count = os::write_capture(file, safe.data() + offset,
                                                     safe.size() - offset);

                if (count <= 0) {
                    failed = true;

                    return;
                }

                offset += static_cast<std::size_t>(count);
                written += static_cast<std::size_t>(count);
            }
        } catch (...) {
            failed = true;
        }
    };

    while (!stop.load()) {
        const auto count = os::read_capture(read, bytes.data(), bytes.size());

        if (count > 0) {
            emit(std::string_view(bytes.data(), static_cast<std::size_t>(count)), false);
        } else if (count == 0) {
            emit({}, true);
            complete = true;

            return;
        } else if (count == -2) {
            failed = true;

            return;
        }
    }
}

ChildProcess::ChildProcess() = default;

ChildProcess::~ChildProcess() {
    if (impl_) {
        try {
            terminate(500);
        } catch (...) { /* Ignored: destructors must not throw. */
        }
    }
}

ChildProcess::ChildProcess(ChildProcess &&) noexcept = default;

ChildProcess &ChildProcess::operator=(ChildProcess &&other) noexcept {
    if (this != &other) {
        if (impl_) {
            try {
                terminate(500);
            } catch (...) { /* Ignored: the replaced child is stopped best-effort. */
            }
        }

        impl_ = std::move(other.impl_);
        sanitized_ = std::move(other.sanitized_);
    }

    return *this;
}

ChildProcess ChildProcess::launch_sanitized(const ProcessSpec &spec, OutputSanitizer sanitizer,
                                            std::size_t output_limit) {
    if (spec.wine_native_diagnostic) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "sanitized native capture unavailable");
    }

    if (!sanitizer || output_limit < 1 || output_limit > 4 * 1024 * 1024 || spec.output.empty() ||
        spec.detached) {
        throw VoyemuError("PROCESS_SANITIZER", "invalid capture bounds or lifetime");
    }

    auto capture = std::make_unique<SanitizedProcessOutput>();
    capture->file = os::create_capture_output(spec.output);
    os::FileHandle write = os::no_file;
    os::create_capture_pipe(capture->read, write);
    ChildProcess child;
    try {
        child = launch_child_process(spec, write);
    } catch (...) {
        os::close_file(write);
        throw;
    }

    os::close_file(write);
    child.sanitized_ = std::move(capture);
    auto owned = child.sanitized_.get();
    const auto pid = child.pid();
    owned->reader =
            std::thread([owned, sanitizer = std::move(sanitizer), pid, output_limit]() mutable {
                owned->run(std::move(sanitizer), pid, output_limit);
            });

    return child;
}

bool ChildProcess::sanitized_output_complete() const {
    return impl_ && sanitized_ && sanitized_->complete.load();
}

bool ChildProcess::sanitized_output_failed() const {
    return impl_ && sanitized_ && sanitized_->failed.load();
}

std::int64_t ChildProcess::pid() const {
    return impl_ ? child_pid(*impl_) : 0;
}

int ChildProcess::exit_code() const {
    return impl_ ? child_exit(*impl_).value_or(-1) : -1;
}

void check_process_variable(const std::string &key, const std::string &value) {
    check_process_string(key);
    check_process_string(value);

    if (key.empty() || key.find('=') != std::string::npos) {
        throw VoyemuError("PROCESS_ENV", "invalid key");
    }
}

void check_capture_request(const ProcessSpec &spec, int timeout_ms, std::size_t output_limit,
                           int maximum_timeout_ms) {
    if (spec.wine_native_diagnostic) {
        throw VoyemuError("DIAGNOSTIC_UNSUPPORTED", "native capture requires owned ChildProcess");
    }

    if (timeout_ms < 1 || timeout_ms > maximum_timeout_ms || output_limit > 1024 * 1024) {
        throw VoyemuError("PROCESS_BOUNDS", "invalid capture bounds");
    }
}

bool local_ipv6_address(const in6_addr &address) {
    constexpr unsigned char unspecified[16] = {};
    constexpr unsigned char loopback[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    constexpr unsigned char mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 255, 255};

    if (std::memcmp(&address, loopback, sizeof loopback) == 0 ||
        std::memcmp(&address, unspecified, sizeof unspecified) == 0) {
        return true;
    }

    if (std::memcmp(&address, mapped_prefix, sizeof mapped_prefix) != 0) {
        return false;
    }

    std::uint32_t ipv4 = 0;
    std::memcpy(&ipv4, reinterpret_cast<const unsigned char *>(&address) + 12, sizeof ipv4);

    return ipv4 == htonl(INADDR_LOOPBACK) || ipv4 == htonl(INADDR_ANY);
}
} // namespace voyemu
