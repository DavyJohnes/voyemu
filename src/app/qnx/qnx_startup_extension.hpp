#pragma once
#include "../core/process.hpp"
#include "check.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace voyemu {
// Authenticate OEM bytes, then accept only a bounded insertion before one complete
// startup line. Returned bytes are shell input; this helper never executes them.
inline std::string qnx_startup_extension(std::string_view original, std::string_view installed,
                                         std::string_view original_sha256,
                                         std::string_view anchor = "clusterboot &") {
    constexpr std::size_t limit = 64 * 1024;
    auto require = [](bool valid, const char *message) {
        if (!valid) {
            throw std::runtime_error(message);
        }
    };

    require(!original.empty() && original.size() <= limit && installed.size() <= 2 * limit,
            "QNX_STARTUP_EXTENSION: script size exceeds bounds");
    require(original.find('\0') == std::string_view::npos &&
                    installed.find('\0') == std::string_view::npos,
            "QNX_STARTUP_EXTENSION: NUL in shell input");
    require(sha256_text(original_sha256) && sha256_bytes(original) == original_sha256,
            "QNX_STARTUP_EXTENSION: original SHA differs");
    require(!anchor.empty() && anchor.find_first_of("\n\r\0", 0, 3) == std::string_view::npos,
            "QNX_STARTUP_EXTENSION: invalid line anchor");
    auto position = [&](std::string_view bytes) {
        const auto at = bytes.find(anchor);
        require(at != std::string_view::npos &&
                        bytes.find(anchor, at + anchor.size()) == std::string_view::npos &&
                        (at == 0 || bytes[at - 1] == '\n') &&
                        (at + anchor.size() == bytes.size() || bytes[at + anchor.size()] == '\n'),
                "QNX_STARTUP_EXTENSION: anchor must be a unique complete line");

        return at;
    };
    const auto before = position(original);
    const auto after = position(installed);
    require(installed.size() >= original.size(), "QNX_STARTUP_EXTENSION: original bytes removed");
    const auto added = installed.size() - original.size();
    require(added <= limit && after == before + added &&
                    installed.substr(0, before) == original.substr(0, before) &&
                    installed.substr(after) == original.substr(before),
            "QNX_STARTUP_EXTENSION: change is not an anchor insertion");
    const auto hook = installed.substr(before, added);
    require(hook.empty() || hook.back() == '\n',
            "QNX_STARTUP_EXTENSION: insertion must end at a line boundary");

    return std::string(hook);
}
} // namespace voyemu
