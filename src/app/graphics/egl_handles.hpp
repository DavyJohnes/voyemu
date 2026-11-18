#pragma once
#include "error.hpp"
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace voyemu::graphics {
enum class EglKind {
    display,
    config,
    context,
    surface
};
// Guest EGL handles are opaque identities, never host process addresses.
class EglHandles {
public:
    std::uint64_t expose(EglKind kind, std::uintptr_t native,
                         std::optional<std::uint64_t> restored = {}) {
        if (!native) {
            if (restored && *restored) {
                throw VoyemuError("EGL_RESTORE", "null identity changed");
            }

            return 0;
        }

        if (restored && !*restored) {
            throw VoyemuError("EGL_RESTORE", "non-null identity changed");
        }

        const auto key = std::pair(kind, native);

        if (auto it = reverse_.find(key); it != reverse_.end()) {
            if (restored && *restored != it->second) {
                throw VoyemuError("EGL_RESTORE", "alias changed");
            }

            return it->second;
        }

        std::uint64_t token = restored.value_or(0);

        if (!restored) {
            do {
                if (++next_ >= 0x100000000ULL) {
                    throw VoyemuError("EGL_HANDLE", "identity exhausted");
                }

                token = 0x564f000000000000ULL | next_;
            } while (handles_.contains(token));
        }

        if (handles_.contains(token)) {
            throw VoyemuError("EGL_RESTORE", "duplicate identity");
        }

        if ((token & 0xffffffff00000000ULL) == 0x564f000000000000ULL &&
            std::uint32_t(token) > next_) {
            next_ = std::uint32_t(token);
        }

        handles_.emplace(token, key);
        reverse_.emplace(key, token);

        return token;
    }

    std::uintptr_t resolve(EglKind kind, std::uint64_t token) const {
        if (!token) {
            return 0;
        }

        auto it = handles_.find(token);

        if (it == handles_.end() || it->second.first != kind) {
            throw VoyemuError("EGL_HANDLE", "unknown identity or type");
        }

        return it->second.second;
    }

    void release(EglKind kind, std::uint64_t token) {
        auto native = resolve(kind, token);

        if (!token) {
            return;
        }

        reverse_.erase({kind, native});
        handles_.erase(token);
    }

private:
    std::uint64_t next_ = 0;
    std::map<std::uint64_t, std::pair<EglKind, std::uintptr_t>> handles_;
    std::map<std::pair<EglKind, std::uintptr_t>, std::uint64_t> reverse_;
};
} // namespace voyemu::graphics
