#include "process.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/digest.hpp"
#include "os/random.hpp"
#include <array>
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
} // namespace voyemu
