#include "qnx_lzo_pack.hpp"
#include <cstdint>
#include <iostream>
#include <lzokay.hpp>
#include <stdexcept>
#include <vector>
using namespace voyemu;
namespace {
void check(bool yes, const std::string &what) {
    if (!yes) {
        throw std::runtime_error(what);
    }
}

std::string decode(std::string_view packed, std::size_t bytes) {
    std::vector<std::uint8_t> output(bytes + 1);
    std::size_t size = 0;

    check(lzokay::decompress(reinterpret_cast<const std::uint8_t *>(packed.data()), packed.size(),
                             output.data(), output.size(), size) == lzokay::EResult::Success,
          "lzokay rejects the packed block");

    return {reinterpret_cast<const char *>(output.data()), size};
}

// Packs, then checks every growth step: exact size, same plaintext, and the bound past it.
void roundtrip(const std::string &plain, const std::string &name) {
    const QnxLzoBlock block(plain);
    const auto shortest = block.emit();

    check(shortest.size() == block.bytes() && decode(shortest, plain.size()) == plain,
          name + ": roundtrip");
    const auto steps = std::min<std::size_t>(block.growable(), 64);

    for (std::size_t grow = 1; grow <= steps; ++grow) {
        const auto grown = block.emit(grow);
        check(grown.size() == block.bytes() + grow && decode(grown, plain.size()) == plain,
              name + ": growth " + std::to_string(grow));
    }

    const auto widest = block.emit(block.growable());
    check(widest.size() == block.bytes() + block.growable() &&
                  decode(widest, plain.size()) == plain,
          name + ": full growth");
    bool rejected = false;

    try {
        (void)block.emit(block.growable() + 1);
    } catch (const std::exception &) {
        rejected = true;
    }

    check(rejected, name + ": growth past the two-byte matches accepted");
}

std::string noise(std::size_t bytes, std::uint32_t seed) {
    std::string out;

    for (std::size_t i = 0; i < bytes; ++i) {
        seed = seed * 1103515245U + 12345U;
        out += static_cast<char>(seed >> 16);
    }

    return out;
}
} // namespace

int main() {
    try {
        for (const std::size_t size : {1, 2, 3, 4, 5, 17, 18, 19, 238, 239, 300, 4000}) {
            roundtrip(noise(size, static_cast<std::uint32_t>(size)),
                      "literals " + std::to_string(size));
        }

        roundtrip(std::string(65536, 0), "zeros");
        std::string pattern;

        while (pattern.size() < 65536) {
            pattern += "abc";
        }

        pattern.resize(65536);
        roundtrip(pattern, "three-byte pattern");
        std::string text;

        for (unsigned line = 0; text.size() < 65536; ++line) {
            text += "QNX boot line " + std::to_string(line * line % 977) + " of the host image\n";
        }

        text.resize(65536);
        roundtrip(text, "text");

        // A unique 64-byte pattern repeated after each distance: every opcode class and the bounds
        // between them, 1 KiB, 2 KiB, 2..3 KiB after long literals, 16 KiB and 48 KiB.
        for (const std::size_t distance :
             {1, 2, 3, 1023, 1024, 1025, 2047, 2048, 2049, 3072, 3073, 16383, 16384, 16385, 32767,
              32768, 49150, 49151, 49152}) {
            const auto head = noise(64, static_cast<std::uint32_t>(distance)),
                       gap = noise(distance > 64 ? distance - 64 : 0,
                                   static_cast<std::uint32_t>(distance * 7 + 1));
            auto plain = head + gap;
            plain.resize(distance);
            plain += head.substr(0, std::min<std::size_t>(64, 65536 - plain.size()));
            roundtrip(plain, "distance " + std::to_string(distance));
        }

        // Two-byte repeats within 1 KiB after one to three literals, three-byte ones 2..3 KiB back.
        std::string pairs = noise(3000, 9);

        for (unsigned i = 0; i < 400; ++i) {
            pairs += pairs.substr(pairs.size() - 1 - i % 1000, 2) + noise(1 + i % 3, i);
            pairs += pairs.substr(pairs.size() - 2049 - i % 1000, 3) + noise(4 + i % 9, i + 7);
        }

        roundtrip(pairs, "short copies");
        // Long literal runs between matches of every length class.
        std::string runs;

        for (unsigned i = 0; runs.size() < 60000; ++i) {
            runs += noise(1 + i * 37 % 600, i);
            runs += std::string(3 + i * 13 % 400, static_cast<char>('a' + i % 26));
        }

        roundtrip(runs, "literal runs");
        bool rejected = false;

        try {
            QnxLzoBlock(std::string(65537, 0));
        } catch (const std::exception &) {
            rejected = true;
        }

        check(rejected, "oversized block accepted");
        std::cout << "QNX LZO packer roundtrip, growth and bounds passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
