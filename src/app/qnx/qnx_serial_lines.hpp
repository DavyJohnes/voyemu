#pragma once
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <system_error>
namespace voyemu {
// Complete console lines that the QNX guest wrote to its session UART file since the previous
// read, each without a slog2 header or CR. A shorter file is a new QEMU incarnation.
class QnxSerialLines {
public:
    std::string read(const std::filesystem::path &path) {
        std::error_code error;

        if (std::filesystem::is_symlink(path, error) ||
            !std::filesystem::is_regular_file(path, error)) {
            return {};
        }

        const auto size = std::filesystem::file_size(path, error);

        if (error) {
            return {};
        }

        if (size < cursor_) {
            cursor_ = 0;
            line_.clear();
            discard_ = false;
        }

        std::ifstream input(path, std::ios::binary);
        std::string bytes(static_cast<std::size_t>(std::min<std::uint64_t>(size - cursor_, chunk)),
                          '\0');
        input.seekg(static_cast<std::streamoff>(cursor_));
        input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        bytes.resize(static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), 0)));
        cursor_ += bytes.size();
        std::string lines;

        for (const char c : bytes) {
            if (c == '\n') {
                if (!discard_) {
                    lines += text(line_) + '\n';
                }

                line_.clear();
                discard_ = false;
            } else if (!discard_) {
                if (line_.size() == line_bytes) {
                    line_.clear();
                    discard_ = true;
                } else {
                    line_.push_back(c);
                }
            }
        }

        return lines;
    }

private:
    static constexpr std::uint64_t chunk = 64 * 1024;
    static constexpr std::size_t line_bytes = 256;

    static std::string text(std::string line) {
        static const std::regex slog2(
                R"(^[A-Z][a-z]{2} [ 0-9][0-9] [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3} +[!-~]{1,64} +[!-~]{1,32} +[0-9]{1,10} +)");
        std::smatch header;

        if (line.ends_with('\r')) {
            line.pop_back();
        }

        return std::regex_search(line, header, slog2) ? header.suffix().str() : line;
    }

    std::uint64_t cursor_ = 0;
    std::string line_;
    bool discard_ = false;
};
} // namespace voyemu
