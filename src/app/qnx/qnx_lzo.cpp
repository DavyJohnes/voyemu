#include "qnx_lzo.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_lzo_pack.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <lzokay.hpp>
#include <optional>
#include <stdexcept>
#include <vector>
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid LZO stream"};

// Block sizes are 16-bit big-endian, so a packed block holds at most 65535 bytes.
std::string pack(const QnxLzoBlock &block, std::size_t grow = 0) {
    need(block.bytes() + grow <= 65535, "QNX_LZO_COMPRESS");

    return block.emit(grow);
}
} // namespace

static std::string decode_stream(std::string_view stream, unsigned blocks, std::size_t extent) {
    need(!stream.empty() && stream.size() <= 8 * 1024 * 1024 && stream.size() % 4 == 0,
         "QNX_LZO_STREAM_BOUNDS");
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < stream.size(); at += 4) {
        std::uint32_t word = 0;

        for (unsigned i = 0; i < 4; ++i) {
            word |= std::uint32_t(static_cast<unsigned char>(stream[at + i])) << (i * 8);
        }

        sum += word;
    }

    need(sum == 0, "QNX_LZO_STREAM_CHECKSUM");
    need(blocks > 0 && blocks <= 512 &&
                 (!extent || (extent > 65536ULL * (blocks - 1) && extent <= 65536ULL * blocks)),
         "QNX_LZO_DECLARED_LAYOUT");
    std::string result;
    result.reserve(static_cast<std::size_t>(blocks) * 65536);
    std::size_t at = 0, count = 0;
    std::array<std::uint8_t, 65536> plain{}, input{};

    while (true) {
        need(at + 2 <= stream.size(), "QNX_LZO_BLOCK_HEADER");
        const auto bytes = (static_cast<unsigned char>(stream[at]) << 8) |
                           static_cast<unsigned char>(stream[at + 1]);
        at += 2;

        if (bytes == 0) {
            break;
        }

        need(count < blocks && at + bytes <= stream.size(), "QNX_LZO_BLOCK_BOUNDS");
        // lzokay reads a run of zero length bytes without an input bound; the non-zero byte after
        // the block ends such a run inside the buffer.
        std::copy_n(stream.data() + at, bytes, input.data());
        input[bytes] = 0xff;
        std::size_t size = 0;
        need(lzokay::decompress(input.data(), bytes, plain.data(), plain.size(), size) ==
                             lzokay::EResult::Success &&
                     size > 0 && size <= plain.size(),
             "QNX_LZO_DECODE");
        need(count == blocks - 1 || size == plain.size(), "QNX_LZO_BLOCK_GEOMETRY");
        result.append(reinterpret_cast<const char *>(plain.data()), size);
        at += bytes;
        ++count;
    }

    const auto padding = (4 - at % 4) % 4;
    need(count == blocks && at + padding + 4 == stream.size() &&
                 (!extent || result.size() == extent),
         "QNX_LZO_FOOTER");

    for (std::size_t i = 0; i < padding; ++i) {
        need(stream[at + i] == 0, "QNX_LZO_PADDING");
    }

    return result;
}

std::string decode_qnx_declared_boot_stream(std::string_view stream, unsigned blocks,
                                            std::size_t bytes) {
    return decode_stream(stream, blocks, bytes);
}

std::string decode_qnx_boot_stream(std::string_view stream) {
    return decode_stream(stream, 105, 0);
}

// The OEM host LZO stream of a qinggan11 adapter, from 0x69108 to the end its header declares.
static std::string_view qinggan11_host_stream(const QnxAdapterSpec &spec, std::string_view hyp) {
    return hyp.substr(0x69108, std::size_t(spec.host_stream_end) + 0x3000 - 0x69108);
}

std::string decode_qnx_qinggan11_host(QnxGuestAdapter adapter, std::string_view hyp,
                                      std::string_view hyp_pin) {
    const auto &spec = qnx_adapter(adapter);
    const auto expected = spec.hyp_sha256;

    need(spec.platform == QnxGuestPlatform::qinggan11 && hyp_pin == expected &&
                 hyp.size() == spec.hyp_bytes && sha256_bytes(hyp) == expected,
         "QNX_QINGGAN11_HYP_SHA");
    auto word = [&](std::size_t at) {
        need(at + 4 <= hyp.size(), "QNX_QINGGAN11_HEADER_BOUNDS");
        std::uint32_t value = 0;

        for (unsigned i = 0; i < 4; ++i) {
            value |= std::uint32_t(static_cast<unsigned char>(hyp[at + i])) << (i * 8);
        }

        return value;
    };
    need(word(0x3000) == 0x00ff7eeb && word(0x3020) == 0x66108 &&
                 word(0x3024) == spec.host_stream_end && word(0x302c) == 6704052,
         "QNX_QINGGAN11_HEADER_LAYOUT");
    std::uint32_t sum = 0;

    for (std::size_t at = 0x3000; at < 0x69108; at += 4) {
        sum += word(at);
    }

    need(sum == 0, "QNX_QINGGAN11_STARTUP_CHECKSUM");

    return decode_stream(qinggan11_host_stream(spec, hyp), 103, 6704052);
}

static std::string encode_declared(std::string_view reference, std::string_view original,
                                   std::string_view plaintext, unsigned blocks,
                                   std::size_t extent) {
    if (plaintext == original) {
        return std::string(reference);
    }

    std::string output;
    std::size_t at = 0;

    for (unsigned block = 0; block < blocks; ++block) {
        const auto extent = (static_cast<unsigned char>(reference[at]) << 8) |
                            static_cast<unsigned char>(reference[at + 1]);
        at += 2;
        std::string encoded(reference.substr(at, extent));
        at += extent;
        const auto part = plaintext.substr(
                static_cast<std::size_t>(block) * 65536,
                std::min<std::size_t>(65536,
                                      plaintext.size() - static_cast<std::size_t>(block) * 65536));

        if (part != std::string_view(original).substr(static_cast<std::size_t>(block) * 65536,
                                                      part.size())) {
            encoded = pack(QnxLzoBlock(part));
        }

        output += static_cast<char>(encoded.size() >> 8);
        output += static_cast<char>(encoded.size());
        output += encoded;
    }

    output.append(2, 0);

    while (output.size() % 4) {
        output += char(0);
    }

    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < output.size(); at += 4) {
        std::uint32_t word = 0;

        for (unsigned byte = 0; byte < 4; ++byte) {
            word |= std::uint32_t(static_cast<unsigned char>(output[at + byte])) << (byte * 8);
        }

        sum += word;
    }

    for (unsigned byte = 0; byte < 4; ++byte) {
        output += static_cast<char>((0 - sum) >> (byte * 8));
    }

    need(decode_stream(output, blocks, extent) == plaintext,
         "QNX_QINGGAN11_LZO_COMPLETE_ROUNDTRIP");

    return output;
}

std::string encode_qnx_declared_boot_stream(std::string_view reference, std::string_view plaintext,
                                            unsigned blocks, std::size_t extent) {
    const auto original = decode_stream(reference, blocks, extent);

    need(plaintext.size() == original.size(), "QNX_LZO_PLAINTEXT_EXTENT");

    return encode_declared(reference, original, plaintext, blocks, extent);
}

std::string encode_qnx_qinggan11_host(QnxGuestAdapter adapter, std::string_view hyp,
                                      std::string_view hyp_pin, std::string_view plaintext) {
    const auto original = decode_qnx_qinggan11_host(adapter, hyp, hyp_pin);

    need(plaintext.size() == original.size(), "QNX_QINGGAN11_LZO_PLAINTEXT_EXTENT");

    return encode_declared(qinggan11_host_stream(qnx_adapter(adapter), hyp), original, plaintext,
                           103, 6704052);
}

std::string encode_qnx_boot_stream(std::string_view reference, std::string_view plaintext) {
    const auto original = decode_qnx_boot_stream(reference);

    need(plaintext.size() == original.size(), "QNX_LZO_PLAINTEXT_EXTENT");

    if (plaintext == original) {
        return std::string(reference);
    }

    // Block 104 must start inside the window the OEM startup accepts; the OEM stream lands on its
    // low end. Each repacked block below 104 takes a size between its shortest packing and that
    // packing grown by its growable matches, so the total lands inside the window.
    constexpr std::uint64_t low = 0x50f6fa12, high = 0x50f6fa1b;
    std::vector<std::string> encoded;
    std::vector<std::optional<QnxLzoBlock>> packed(105);
    std::size_t at = 0;
    auto part = [&](unsigned i) {
        return plaintext.substr(
                static_cast<std::size_t>(i) * 65536,
                std::min<std::size_t>(65536,
                                      plaintext.size() - static_cast<std::size_t>(i) * 65536));
    };

    for (unsigned i = 0; i < 105; ++i) {
        const auto size = (static_cast<unsigned char>(reference[at]) << 8) |
                          static_cast<unsigned char>(reference[at + 1]);
        at += 2;
        encoded.emplace_back(reference.substr(at, size));
        at += size;

        if (part(i) != std::string_view(original).substr(static_cast<std::size_t>(i) * 65536,
                                                         part(i).size())) {
            packed[i].emplace(part(i));
        }
    }

    auto resident = [&] {
        std::uint64_t offset = 0x50d24800ULL + 2;

        for (unsigned i = 0; i < 104; ++i) {
            offset += 2 + (packed[i] ? packed[i]->bytes() : encoded[i].size());
        }

        return offset;
    };
    auto room = [&] {
        std::uint64_t total = 0;

        for (unsigned i = 0; i < 104; ++i) {
            if (packed[i]) {
                total += std::min<std::size_t>(packed[i]->growable(), 65535 - packed[i]->bytes());
            }
        }

        return total;
    };

    // Out of reach: repack unchanged blocks, those that shrink most while block 104 starts past the
    // window, those that can grow most while it starts short of the window.
    if (resident() > high || resident() + room() < low) {
        std::vector<unsigned> order;

        for (unsigned i = 0; i < 104; ++i) {
            if (!packed[i]) {
                order.push_back(i);
            }
        }

        std::vector<QnxLzoBlock> candidates;
        candidates.reserve(order.size());

        for (const auto i : order) {
            candidates.emplace_back(part(i));
        }

        std::vector<std::size_t> rank(order.size());

        for (std::size_t k = 0; k < rank.size(); ++k) {
            rank[k] = k;
        }

        const bool shrink = resident() > high;
        std::sort(rank.begin(), rank.end(), [&](std::size_t a, std::size_t b) {
            const auto gain = [&](std::size_t k) {
                const auto oem = static_cast<std::int64_t>(encoded[order[k]].size()),
                           bytes = static_cast<std::int64_t>(candidates[k].bytes());

                return shrink ? oem - bytes
                              : bytes + static_cast<std::int64_t>(candidates[k].growable()) - oem;
            };

            return gain(a) > gain(b);
        });

        for (const auto k : rank) {
            if (shrink ? resident() <= high : resident() + room() >= low) {
                break;
            }

            packed[order[k]].emplace(candidates[k]);
        }
    }

    need(resident() <= high && resident() + room() >= low, "QNX_LZO_RESIDENT_COMPENSATION");
    auto grow = resident() < low ? low - resident() : 0;

    for (unsigned i = 0; i < 105; ++i) {
        if (!packed[i]) {
            continue;
        }

        const auto &block = packed[i].value();
        const auto extra = i < 104 ? std::min<std::uint64_t>(
                                             grow, std::min<std::size_t>(block.growable(),
                                                                         65535 - block.bytes()))
                                   : 0;
        encoded[i] = pack(block, extra);
        grow -= extra;
    }

    auto emit = [&]() {
        std::string stream;
        std::uint64_t landing = 0;

        for (unsigned i = 0; i < 105; ++i) {
            if (i == 104) {
                landing = 0x50d24800ULL + stream.size() + 2;
            }

            stream += static_cast<char>(encoded[i].size() >> 8);
            stream += static_cast<char>(encoded[i].size());
            stream += encoded[i];
        }

        stream.append(2, 0);

        while (stream.size() % 4) {
            stream += char(0);
        }

        std::uint32_t sum = 0;

        for (std::size_t p = 0; p < stream.size(); p += 4) {
            std::uint32_t word = 0;

            for (unsigned j = 0; j < 4; ++j) {
                word |= std::uint32_t(static_cast<unsigned char>(stream[p + j])) << (j * 8);
            }

            sum += word;
        }

        for (unsigned j = 0; j < 4; ++j) {
            stream += static_cast<char>((0 - sum) >> (j * 8));
        }

        return std::pair{std::move(stream), landing};
    };
    const auto result = emit();

    need(result.second >= low && result.second <= high, "QNX_LZO_RESIDENT_LANDING");
    need(decode_qnx_boot_stream(result.first) == plaintext, "QNX_LZO_COMPLETE_ROUNDTRIP");

    return result.first;
}
} // namespace voyemu
