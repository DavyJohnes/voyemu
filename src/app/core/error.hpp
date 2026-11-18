#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

namespace voyemu {
// The CODE of a "CODE: message" text, such as what() of an exception, or empty without one.
inline std::string_view message_code(std::string_view text) noexcept {
    const auto colon = text.find(':');

    if (colon == 0 || colon >= 64) {
        return {};
    }

    auto code = text;

    code.remove_suffix(text.size() - colon);

    return code.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ_0123456789") == std::string_view::npos
                   ? code
                   : std::string_view();
}

// The failure of a Voyemu operation: a CODE of capital letters, digits and underscores and a
// message, with what() "CODE: message". api::error() maps the code to the JSON error, a code ending
// in _UNKNOWN to an unknown outcome, and result_exit_code() maps that error to the exit code.
class VoyemuError : public std::runtime_error {
public:
    VoyemuError(const char *code, const char *message)
        : std::runtime_error(std::string(code) + ": " + message) {}

    VoyemuError(const char *code, const std::string &message)
        : std::runtime_error(std::string(code) + ": " + message) {}

    std::string_view code() const noexcept {
        return message_code(what());
    }
};
} // namespace voyemu
