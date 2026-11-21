#include "apk_digest.hpp"
#include "apk_alignment.hpp"
#include "error.hpp"
#include "little_endian.hpp"
#include "process.hpp"
#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <string_view>
namespace voyemu {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_DIGEST", "bounded unsigned APK required");
}

void valid(bool condition, const char *) {
    if (!condition) {
        invalid();
    }
}

unsigned u16(std::string_view s, std::size_t p) {
    return unsigned(le_read(s, p, 2, valid, nullptr));
}

std::uint64_t u32(std::string_view s, std::size_t p) {
    return le_read(s, p, 4, valid, nullptr);
}

std::string le32(unsigned value) {
    std::string s;

    for (unsigned i = 0; i < 4; ++i) {
        s += static_cast<char>(value >> (8 * i));
    }

    return s;
}

std::string raw_digest(std::string_view s) {
    const auto hex = sha256_bytes(s);
    std::string raw;

    raw.reserve(32);

    for (std::size_t i = 0; i < 64; i += 2) {
        raw += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
    }

    return raw;
}
} // namespace

std::string unsigned_apk_v2_digest(const std::filesystem::path &file, const std::string &pin) {
    verify_pinned_apk_alignment(file, pin);
    const auto size = std::filesystem::file_size(file);
    std::ifstream stream(file, std::ios::binary);
    auto read = [&](std::uint64_t offset, std::size_t count) {
        if (offset > size || count > size - offset) {
            invalid();
        }

        std::string bytes(count, '\0');
        stream.seekg(offset);
        stream.read(bytes.data(), count);

        if (!stream) {
            invalid();
        }

        return bytes;
    };
    const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, 65557));
    const auto tail = read(size - tail_size, tail_size);
    std::size_t end = tail.size() - 22;

    while (true) {
        if (u32(tail, end) == 0x06054b50 && u16(tail, end + 20) == tail.size() - end - 22) {
            break;
        }

        if (!end) {
            invalid();
        }

        --end;
    }

    const auto central = u32(tail, end + 16), eocd = std::uint64_t(size) - tail_size + end;

    if (central > eocd || central + u32(tail, end + 12) != eocd) {
        invalid();
    }

    if (central >= 16 && read(central - 16, 16) == "APK Sig Block 42") {
        invalid();
    }

    std::string digests;
    unsigned chunks = 0;

    // Each ZIP section starts its own 1 MiB chunk sequence; chunks never cross sections.
    for (const auto &range :
         {std::pair<std::uint64_t, std::uint64_t>{0, central}, {central, eocd}, {eocd, size}}) {
        for (auto offset = range.first; offset < range.second;) {
            const auto count =
                    static_cast<unsigned>(std::min<std::uint64_t>(1 << 20, range.second - offset));
            const auto bytes = read(offset, count);
            digests += raw_digest(std::string(1, char(0xa5)) + le32(count) + bytes);
            ++chunks;
            offset += count;
        }
    }

    if (sha256_file(file) != pin) {
        invalid();
    }

    return sha256_bytes(std::string(1, char(0x5a)) + le32(chunks) + digests);
}
} // namespace voyemu
