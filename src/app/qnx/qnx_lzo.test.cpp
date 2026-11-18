#include "qnx_lzo.hpp"
#include "process.hpp"
#include "qnx_imagefs.hpp"
#include "qnx_lzo_pack.hpp"
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
using namespace voyemu;
using namespace std::string_view_literals;
namespace {
template <class Function> bool rejects(Function action) {
    try {
        action();
    } catch (const std::exception &) {
        return true;
    }

    return false;
}

// Frames LZO1X blocks as a QNX boot stream: 16-bit big-endian block sizes, a zero size, zero
// padding and a last word that makes the little-endian word sum zero.
std::string frame(const std::vector<std::string> &blocks) {
    std::string stream;

    for (const auto &block : blocks) {
        stream += static_cast<char>(block.size() >> 8);
        stream += static_cast<char>(block.size());
        stream += block;
    }

    stream.append(2, 0);

    while (stream.size() % 4) {
        stream += char(0);
    }

    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < stream.size(); at += 4) {
        for (unsigned i = 0; i < 4; ++i) {
            sum += std::uint32_t(static_cast<unsigned char>(stream[at + i])) << (i * 8);
        }
    }

    for (unsigned i = 0; i < 4; ++i) {
        stream += static_cast<char>((0 - sum) >> (i * 8));
    }

    return stream;
}

std::string compress(std::string_view plain) {
    return QnxLzoBlock(plain).emit();
}

// 64 pseudo-random bytes, 17000 zero bytes, the same 64 bytes 17064 bytes back and near repeats.
std::string fixture_plaintext() {
    std::string plain;
    std::uint32_t x = 1;

    for (unsigned i = 0; i < 64; ++i) {
        x = x * 1103515245U + 12345U;
        plain += static_cast<char>(x >> 16);
    }

    plain.append(17000, 0);
    plain += plain.substr(0, 64);
    plain += "QNX boot stream QNX boot block QNX boot stream block ab ab abc abc abcd";

    return plain;
}

// fixture_plaintext() packed by liblzo2 2.10 lzo1x_999_compress_level at level 9.
constexpr std::string_view liblzo_999 =
        "\x52\xc6\x7e\x81\x6b\x4b\xfb\xe2\xfb\x54\xf6\xbd\xdf\x7c\x1c\xe1\x87\x01\xbf\x31\xde\x56\x72\x0f"
        "\x47\x67\x66\x87\x59\xaa\x88\x3c\x59\xea\x56\x13\x7b\xd2\x85\xa1\xd8\x3c\x54\x55\x2f\x37\xae\x65"
        "\x5b\xda\x02\x79\x98\xcc\xe3\x1a\x76\x8e\x5f\xd9\x99\x8f\x1f\x3f\x36\x00\x20\x00\x00\x00\x00\x00"
        "\x00\x00\xe6\x00\x00\x20\x00\x00\x00\x00\x00\x00\x00\xe6\x00\x00\x20\x00\x00\x00\x00\x00\x00\x00"
        "\xe6\x00\x00\x20\x00\x00\x00\x00\x00\x00\x00\xe6\x00\x00\x20\x00\x00\x00\x00\x00\x00\x00\xe6\x00"
        "\x00\x20\x00\x00\x00\x00\x00\x00\x00\xe6\x00\x00\x20\x00\x00\x00\x00\x00\x00\x00\xe6\x00\x00\x20"
        "\x00\x00\x00\x00\x00\x00\x00\xe6\x00\x00\x20\x00\x00\x48\x00\x00\x10\x37\xa0\x0a\x0d\x51\x4e\x58"
        "\x20\x62\x6f\x6f\x74\x20\x73\x74\x72\x65\x61\x6d\x20\x27\x3c\x00\x03\x62\x6c\x6f\x63\x6b\x20\x2e"
        "\x78\x00\xb6\x02\x61\x62\xa9\x00\x63\xed\x00\x64\x11\x00\x00"sv;
// fixture_plaintext() packed by liblzo2 2.10 lzo1x_1_compress.
constexpr std::string_view liblzo_1 =
        "\x00\x31\xc6\x7e\x81\x6b\x4b\xfb\xe2\xfb\x54\xf6\xbd\xdf\x7c\x1c\xe1\x87\x01\xbf\x31\xde\x56\x72"
        "\x0f\x47\x67\x66\x87\x59\xaa\x88\x3c\x59\xea\x56\x13\x7b\xd2\x85\xa1\xd8\x3c\x54\x55\x2f\x37\xae"
        "\x65\x5b\xda\x02\x79\x98\xcc\xe3\x1a\x76\x8e\x5f\xd9\x99\x8f\x1f\x3f\x36\x00\x00\x00\x20\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
        "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x86\x08\x00\x10\x37\xa0\x0a\x0d"
        "\x51\x4e\x58\x20\x62\x6f\x6f\x74\x20\x73\x74\x72\x65\x61\x6d\x20\x27\x3c\x00\x02\x62\x6c\x6f\x63"
        "\x6b\x28\x38\x00\xd8\x03\xb4\x02\x0f\x61\x62\x20\x61\x62\x20\x61\x62\x63\x20\x61\x62\x63\x20\x61"
        "\x62\x63\x64\x11\x00\x00"sv;

// Start of block 104 that the A9 startup sees for a stream of 105 blocks.
std::uint64_t landing(std::string_view stream) {
    std::size_t at = 0;

    for (unsigned i = 0; i < 104; ++i) {
        at += 2 + ((static_cast<unsigned char>(stream[at]) << 8) |
                   static_cast<unsigned char>(stream[at + 1]));
    }

    return 0x50d24800ULL + at + 2;
}

// 105 equal blocks of word salad then zeros, packed and grown so that block 104 starts on the low
// end of the A9 window like the OEM stream; changes must keep it inside the window.
void a9_window() {
    constexpr std::uint64_t low = 0x50f6fa12, high = 0x50f6fa1b,
                            payload = low - 0x50d24800ULL - 2 - 104 * 2;
    auto salad = [](std::size_t text, std::uint32_t seed) {
        constexpr std::array<std::string_view, 12> words{
                "qnx ", "boot ", "image ",   "stream ", "host ", "block ",
                "the ", "of ",   "startup ", "kernel ", "load ", "window "};
        std::string plain;

        while (plain.size() < text) {
            seed = seed * 1103515245U + 12345U;
            plain += words[(seed >> 16) % words.size()];

            plain += static_cast<char>('a' + (seed >> 8) % 26);
            plain += static_cast<char>('a' + (seed >> 24) % 26);
        }

        plain.resize(text);
        plain.resize(65536, 0);

        return plain;
    };
    // Shortest text whose packing plus half its growth reaches the per-block share.
    std::size_t lo = 256, hi = 65536;

    while (lo < hi) {
        const auto mid = (lo + hi) / 2;
        const QnxLzoBlock block(salad(mid, 1));

        if (block.bytes() + block.growable() / 2 < payload / 104) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    const auto plain = salad(lo, 1);
    const QnxLzoBlock block(plain);

    if (block.bytes() * 104 > payload || (block.bytes() + block.growable()) * 104 < payload) {
        throw std::runtime_error("A9 window fixture share");
    }

    std::vector<std::string> blocks;
    auto grow = payload - block.bytes() * 104;

    for (unsigned i = 0; i < 105; ++i) {
        const auto extra = i < 104 ? std::min<std::uint64_t>(grow, (grow + 103 - i) / (104 - i))
                                   : 0;
        blocks.push_back(block.emit(extra));
        grow -= extra;
    }

    std::string host;

    for (unsigned i = 0; i < 105; ++i) {
        host += plain;
    }

    const auto reference = frame(blocks);

    if (landing(reference) != low || decode_qnx_boot_stream(reference) != host) {
        throw std::runtime_error("A9 window fixture landing");
    }

    auto flipped = host, noisy = host, quiet = host;
    flipped[7 * 65536 + 100] ^= 1;
    std::uint32_t x = 3;

    for (std::size_t i = 0; i < lo / 2; ++i) {
        x = x * 1103515245U + 12345U;
        noisy[9 * 65536 + i] = static_cast<char>(x >> 16);
    }

    std::fill_n(quiet.begin() + 11 * 65536, lo, 'z');

    for (const auto &changed : {flipped, noisy, quiet}) {
        const auto packed = encode_qnx_boot_stream(reference, changed);

        if (landing(packed) < low || landing(packed) > high ||
            decode_qnx_boot_stream(packed) != changed) {
            throw std::runtime_error("A9 window missed");
        }
    }
}

void synthetic() {
    const auto fixture = fixture_plaintext();

    for (const auto packed : {liblzo_999, liblzo_1}) {
        if (decode_qnx_declared_boot_stream(frame({std::string(packed)}), 1, fixture.size()) !=
            fixture) {
            throw std::runtime_error("liblzo2 block decode");
        }
    }

    std::string first;

    for (unsigned line = 0; first.size() < 65536; ++line) {
        first += "QNX boot line " + std::to_string(line * line % 977) + "\n";
    }

    first.resize(65536);
    const auto plain = first + fixture, head = compress(first), tail = compress(fixture),
               stream = frame({head, tail});

    if (decode_qnx_declared_boot_stream(stream, 2, plain.size()) != plain ||
        encode_qnx_declared_boot_stream(stream, plain, 2, plain.size()) != stream) {
        throw std::runtime_error("synthetic identity");
    }

    auto changed = plain;
    changed[65536 + 17100] ^= 1;
    const auto packed = encode_qnx_declared_boot_stream(stream, changed, 2, plain.size());

    if (decode_qnx_declared_boot_stream(packed, 2, plain.size()) != changed ||
        packed.substr(0, 2 + head.size()) != stream.substr(0, 2 + head.size())) {
        throw std::runtime_error("synthetic changed block roundtrip");
    }

    if (!rejects([&] {
            encode_qnx_declared_boot_stream(stream, std::string_view(plain).substr(1), 2,
                                            plain.size());
        })) {
        throw std::runtime_error("synthetic plaintext extent accepted");
    }

    // Every truncation of the last block keeps a valid frame and checksum; the codec must reject
    // it.
    for (std::size_t cut = 0; cut < tail.size(); ++cut) {
        if (!rejects([&] {
                decode_qnx_declared_boot_stream(frame({head, tail.substr(0, cut)}), 2,
                                                plain.size());
            })) {
            throw std::runtime_error("truncated block accepted");
        }
    }

    // A long literal whose run of zero length bytes reaches the end of the block.
    for (std::size_t zeros = 1; zeros <= 8; ++zeros) {
        if (!rejects([&] {
                decode_qnx_declared_boot_stream(frame({std::string(zeros, 0)}), 1, 1);
            })) {
            throw std::runtime_error("zero length run past the block accepted");
        }
    }

    if (!rejects([] {
            decode_qnx_boot_stream("");
        })) {
        throw std::runtime_error("empty stream accepted");
    }

    if (QnxLzoBlock(fixture).bytes() > liblzo_999.size()) {
        throw std::runtime_error("packing longer than liblzo2 lzo1x_999");
    }

    a9_window();
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11-611") {
            std::ifstream in(argv[2], std::ios::binary);

            if (!in) {
                throw std::runtime_error("6.11 owner input open");
            }

            const std::string hyp((std::istreambuf_iterator<char>(in)), {}),
                    pin = "dbb3bc2c6f9b0308e38627cf6a3e3b6757ba98c4f5413334526c26d812ab08f0";
            const auto host = decode_qnx_qinggan11_611_host(hyp, pin);

            if (sha256_bytes(host) !=
                        "734b0b0f0c0383853231f5d1ba216cd4e9d6e9fe8373ec4aeb21b1db7d5ed7e2" ||
                encode_qnx_qinggan11_611_host(hyp, pin, host) !=
                        hyp.substr(0x69108, 0x2d76d0 - 0x69108)) {
                throw std::runtime_error("6.11 host identity");
            }

            auto changed = host;
            changed.at(changed.size() - 8) ^= 1;
            const auto packed = encode_qnx_qinggan11_611_host(hyp, pin, changed);

            if (decode_qnx_declared_boot_stream(packed, 103, 6704052) != changed) {
                throw std::runtime_error("6.11 host changed roundtrip");
            }

            for (unsigned mode = 0; mode < 3; ++mode) {
                bool rejected = false;
                try {
                    if (mode == 0) {
                        decode_qnx_qinggan11_host(hyp, pin);
                    } else if (mode == 1) {
                        decode_qnx_qinggan11_611_host(hyp, std::string(64, '0'));
                    } else {
                        auto bad = hyp;
                        bad[0x3024] ^= 1;
                        decode_qnx_qinggan11_611_host(bad, sha256_bytes(bad));
                    }
                } catch (...) {
                    rejected = true;
                }

                if (!rejected) {
                    throw std::runtime_error("6.11 cross-version or mutated host accepted");
                }
            }

            std::cout << "6.11 host identity and mixed-version guards passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11") {
            std::ifstream in(argv[2], std::ios::binary);

            if (!in) {
                throw std::runtime_error("A11 owner input open");
            }

            const std::string hyp((std::istreambuf_iterator<char>(in)), {});
            const std::string input_pin =
                    "a476a1e3d3aaa51d855d2d91c412a962efb6ef1e3e63540ec739779d35ce3ce8";
            const auto host = decode_qnx_qinggan11_host(hyp, input_pin);

            if (encode_qnx_qinggan11_host(hyp, input_pin, host) !=
                hyp.substr(0x69108, 0x2d75a8 - 0x69108)) {
                throw std::runtime_error("A11 identity repack");
            }

            auto changed_host = host;
            changed_host.at(changed_host.size() - 8) ^= 1;
            const auto repacked = encode_qnx_qinggan11_host(hyp, input_pin, changed_host);

            if (repacked.empty() || repacked == hyp.substr(0x69108, 0x2d75a8 - 0x69108)) {
                throw std::runtime_error("A11 changed repack");
            }

            bool extent_rejected = false;
            try {
                encode_qnx_qinggan11_host(hyp, input_pin, std::string_view(host).substr(1));
            } catch (...) {
                extent_rejected = true;
            }

            if (!extent_rejected) {
                throw std::runtime_error("A11 repack bad extent accepted");
            }

            if (host.size() != 6704052 ||
                read_qnx_imagefs_file(host, "proc/boot/procnto-smp-instr").empty()) {
                throw std::runtime_error("A11 host ImageFS layout");
            }

            for (const auto at : {0x3020U, 0x3024U, 0x302cU, 0x69108U}) {
                auto changed = hyp;
                changed[at] ^= 1;
                bool rejected = false;
                try {
                    decode_qnx_qinggan11_host(changed, sha256_bytes(changed));
                } catch (...) {
                    rejected = true;
                }

                if (!rejected) {
                    throw std::runtime_error("modified A11 count/size/stream selected");
                }
            }

            bool rejected = false;
            try {
                decode_qnx_qinggan11_host(hyp, std::string(64, '0'));
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("wrong A11 source pin accepted");
            }

            std::cout
                    << "Exact A11 103-block host decoded and source/header mutation guards passed; SHA256 "
                    << sha256_bytes(host) << '\n';

            return 0;
        }

        if (argc == 2) {
            std::ifstream in(argv[1], std::ios::binary);

            if (!in) {
                throw std::runtime_error("OEM input open");
            }

            const std::string hyp((std::istreambuf_iterator<char>(in)), {});

            if (sha256_bytes(hyp) !=
                "0af64f396f7de8dafe3488f21ec7e4cd989b3d02d3de0412aec020fdb1424b89") {
                throw std::runtime_error("OEM input pin");
            }

            std::uint32_t end = 0;

            for (unsigned i = 0; i < 4; ++i) {
                end |= std::uint32_t(static_cast<unsigned char>(hyp.at(0x3024 + i))) << (i * 8);
            }

            if (end < 0x57108 || 0x3000 + end > hyp.size()) {
                throw std::runtime_error("OEM stream header");
            }

            const auto stream = hyp.substr(0x5a108, end - 0x57108);
            const auto host = decode_qnx_boot_stream(stream);

            if (encode_qnx_boot_stream(stream, host) != stream) {
                throw std::runtime_error("identity stream changed");
            }

            auto changed = host;
            changed.at(0x3396e2) = changed.at(0x3396e2) == char(48) ? char(49) : char(48);
            const auto packed = encode_qnx_boot_stream(stream, changed);

            if (decode_qnx_boot_stream(packed) != changed) {
                throw std::runtime_error("changed host roundtrip failed");
            }

            bool failed = false;
            auto bad = stream;
            bad[10] ^= 1;
            try {
                decode_qnx_boot_stream(bad);
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("corrupt stream accepted");
            }

            std::cout << "OEM host IFS decoded: " << host.size() << " bytes SHA256 "
                      << sha256_bytes(host) << '\n';

            return 0;
        }

        synthetic();
        std::cout
                << "QNX LZO liblzo2 fixtures, synthetic roundtrip, A9 window and input guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
