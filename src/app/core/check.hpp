#pragma once
#include "error.hpp"
#include <string>
#include <string_view>

namespace voyemu {
// Whether value is size lowercase hex digits, such as a session id or token.
inline bool hex_text(std::string_view value, std::size_t size) {
    return value.size() == size &&
           value.find_first_not_of("0123456789abcdef") == std::string_view::npos;
}

// Whether value is a SHA-256 digest in lowercase hex, the form of every pin.
inline bool sha256_text(std::string_view value) {
    return hex_text(value, 64);
}

// The check of one component: need(condition, message) throws VoyemuError(code, message) when the
// condition fails.
struct Check {
    const char *code;

    template <typename Message> void operator()(bool condition, const Message &message) const {
        if (!condition) {
            throw VoyemuError(code, message);
        }
    }
};

// The check of a reader of one input: need(condition, code) throws VoyemuError(code, message) when
// the condition fails, so each failed check keeps its own code.
struct CodeCheck {
    const char *message;

    void operator()(bool condition, const char *code) const {
        if (!condition) {
            throw VoyemuError(code, message);
        }
    }
};
} // namespace voyemu
