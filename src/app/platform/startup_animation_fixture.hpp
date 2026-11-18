#pragma once
#include "error.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Synthetic stored ZIP archives in the bootanimation.zip layout, built by tests.
namespace voyemu::startup_fixture {
inline void word(std::string &bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        bytes += static_cast<char>(value >> (i * 8));
    }
}

inline std::uint32_t crc(std::string_view bytes) {
    std::uint32_t value = ~0U;

    for (unsigned char byte : bytes) {
        value ^= byte;

        for (unsigned i = 0; i < 8; ++i) {
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
        }
    }

    return ~value;
}

// Method 0 keeps entries stored; any other method is recorded for rejection tests only.
inline void zip(const std::filesystem::path &file,
                const std::vector<std::pair<std::string, std::string>> &entries,
                unsigned method = 0) {
    std::string local, central;

    for (const auto &[name, bytes] : entries) {
        const auto at = local.size();
        const unsigned entry_method = name.ends_with(".png") ? method : 0;
        word(local, 0x04034b50, 4);
        word(local, 20, 2);
        word(local, 0, 2);
        word(local, entry_method, 2);
        word(local, 0, 4);
        word(local, crc(bytes), 4);
        word(local, bytes.size(), 4);
        word(local, bytes.size(), 4);
        word(local, name.size(), 2);
        word(local, 0, 2);
        local += name;
        local += bytes;
        word(central, 0x02014b50, 4);
        word(central, 20, 2);
        word(central, 20, 2);
        word(central, 0, 2);
        word(central, entry_method, 2);
        word(central, 0, 4);
        word(central, crc(bytes), 4);
        word(central, bytes.size(), 4);
        word(central, bytes.size(), 4);
        word(central, name.size(), 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 2);
        word(central, 0, 4);
        word(central, at, 4);
        central += name;
    }

    const auto at = local.size();
    local += central;
    word(local, 0x06054b50, 4);
    word(local, 0, 4);
    word(local, entries.size(), 2);
    word(local, entries.size(), 2);
    word(local, central.size(), 4);
    word(local, at, 4);
    word(local, 0, 2);
    std::ofstream output(file, std::ios::binary);
    output.write(local.data(), static_cast<std::streamsize>(local.size()));

    if (!output) {
        throw VoyemuError("FIXTURE", "fixture ZIP write");
    }
}
} // namespace voyemu::startup_fixture
