#include "dex_instruction_patch.hpp"
#include "check.hpp"
#include "dex_file.hpp"
#include "dex_inventory.hpp"
#include "error.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <string_view>
namespace voyemu {
using namespace dex_file;
namespace {
constexpr Check need{"DEX_LITERAL"};

std::size_t resolve_code(const std::string &source, const DexLiteralPatch &p) {
    need(sha256_text(p.source_sha256) && sha256_text(p.instructions_sha256) &&
                 sha256_text(p.output_sha256),
         "recipe pins");
    need(source.size() <= 128ULL << 20, "input budget");
    need(sha256_bytes(source) == p.source_sha256, "source SHA mismatch");
    inspect_dex_inventory(std::as_bytes(std::span(source.data(), source.size())));
    Reader r{source, need};
    const auto digest = sha1(std::string_view(source).substr(32));

    for (unsigned i = 0; i < 20; ++i) {
        need(r.byte(12 + i) == digest[i], "input SHA1");
    }

    need(r.word(8) == adler(std::string_view(source).substr(12)), "input Adler32");
    need(!p.owner.empty() && !p.method.empty() && !p.prototype.empty() && p.reg < 256 &&
                 p.before != p.after,
         "recipe identity");
    std::size_t selected = 0;
    unsigned found = 0;

    for (std::uint32_t c = 0; c < r.word(96); ++c) {
        const auto def = r.word(100) + c * 32;

        if (r.type(r.word(def)) != p.owner) {
            continue;
        }

        std::size_t at = r.word(def + 24);
        need(at >= 112, "class data");
        std::array<std::uint32_t, 4> count{};

        for (auto &n : count) {
            n = r.leb(at);
            need(n <= 65535, "member count");
        }

        for (std::uint64_t i = 0; i < static_cast<std::uint64_t>(count[0]) + count[1]; ++i) {
            r.leb(at);
            r.leb(at);
        }

        for (unsigned group = 2; group < 4; ++group) {
            std::uint32_t index = 0;

            for (std::uint32_t i = 0; i < count[group]; ++i) {
                auto delta = r.leb(at);
                need(delta <= std::numeric_limits<std::uint32_t>::max() - index,
                     "method delta overflow");
                index += delta;
                const auto flags = r.leb(at), code = r.leb(at);
                need(index < r.word(88), "method index");
                const auto method = r.word(92) + index * 8;
                need(r.type(r.half(method)) == p.owner, "method owner");

                if (r.text(r.word(method + 4)) != p.method ||
                    r.proto(r.half(method + 2)) != p.prototype) {
                    continue;
                }

                need(flags == p.access && code >= 112 && code % 4 == 0, "method flags/code");
                selected = code;
                ++found;
            }
        }
    }

    need(found == 1, "method selection");
    r.extent(selected, 16);
    need(r.half(selected) == p.registers && r.half(selected + 2) == p.inputs &&
                 r.half(selected + 4) == p.outputs && r.half(selected + 6) == p.tries,
         "code header");
    need(p.reg < p.registers, "constant register extent");
    const auto units = r.word(selected + 12);
    need(units == p.instruction_units && units <= 1000000 && p.code_unit < units &&
                 units - p.code_unit >= 2,
         "instruction extent");
    const auto start = selected + 16;
    r.extent(start, static_cast<std::size_t>(units) * 2);
    need(sha256_bytes(source.substr(start, static_cast<std::size_t>(units) * 2)) ==
                 p.instructions_sha256,
         "instruction SHA mismatch");

    return start;
}

void seal(std::string &result) {
    const auto digest = sha1(std::string_view(result).substr(32));

    for (unsigned i = 0; i < 20; ++i) {
        result[12 + i] = static_cast<char>(digest[i]);
    }

    put32(result, 8, adler(std::string_view(result).substr(12)));
}
} // namespace

std::string patch_dex_literal(const std::string &source, const DexLiteralPatch &p) {
    const auto start = resolve_code(source, p);
    Reader r{source, need};
    const auto units = p.instruction_units;
    const auto opcode = static_cast<std::uint16_t>(0x13 | (p.reg << 8));
    unsigned matches = 0;

    for (std::uint32_t i = 0; i + 1 < units; ++i) {
        if (r.half(start + static_cast<std::size_t>(i) * 2) == opcode &&
            r.half(start + static_cast<std::size_t>(i) * 2 + 2) == p.before) {
            ++matches;
        }
    }

    const auto change = start + static_cast<std::size_t>(p.code_unit) * 2;
    need(matches == 1 && r.half(change) == opcode && r.half(change + 2) == p.before,
         "const/16 guard");
    std::string result = source;
    result[change + 2] = static_cast<char>(p.after);
    result[change + 3] = static_cast<char>(p.after >> 8);
    seal(result);
    need(sha256_bytes(result) == p.output_sha256, "output SHA mismatch");

    return result;
}

DexLiteralPatch launcher_ethernet_guard_recipe() {
    return {"458351aa5f7512e2920b2c4ad57adf0153c25f250fe3848c691876fd7fa89705",
            "Lcom/qinggan/util/NetUtil;",
            "isNetworkConnected",
            "(Landroid/content/Context;)Z",
            "e00352a2fc0508ad181b23a1d74d1348ca7fd8a41e6739128d919a4a06b1e74a",
            "844aa4798d771d7bd62c0850ecfe0d4d1e9b2ebd83afa348bd8c38087e4e2ff5",
            9,
            105,
            240,
            8,
            1,
            2,
            2,
            4,
            9,
            1};
}

std::string patch_navi_msaa(const std::string &source) {
    const DexLiteralPatch duplicate = {
            "f24e35ff5987ca00155badfe6ee83d1ea5b3aad2098549997aabc76763428b0c",
            "Lcom/baidu/mapautosdk/component/map/gl/e$c;",
            "a",
            "([I)[I",
            "3e073faebd1f41ccddef79cd0894a813ad3ffc1f74f9946b89f7f2817c24a739",
            "2ec1b46d64adfb4dcfc0d1abd5ca8ddbd06a44a41f7720214a018de7bb211644",
            2,
            11,
            25,
            6,
            2,
            5,
            0,
            5,
            0x3040,
            0x3038};
    auto result = patch_dex_literal(source, duplicate);
    const DexLiteralPatch constructor = {
            duplicate.output_sha256,
            duplicate.owner,
            "<init>",
            "()V",
            "3dbb6a2a88ee1bde2426d83680e16348fa71cb9ceefad470eb2b2f9771918c7b",
            "a43bed8ccbab8e9e980004ca38d30133381845a7f81fe97c0c7fcdce38be5efe",
            65538,
            117,
            142,
            8,
            1,
            2,
            0,
            0,
            9,
            1};
    const auto start = resolve_code(result, constructor);
    Reader r{result, need};

    // The pinned constructor allocates a new zero-initialized int[23]. Omit only
    // its sample-buffer/sample-count stores; no instruction addresses move.
    need(r.half(start + 24 * 2) == 0x0313 && r.half(start + 25 * 2) == 23 &&
                 r.half(start + 26 * 2) == 0x3323 && r.type(r.half(start + 27 * 2)) == "[I",
         "EGL array allocation");
    need(r.half(start + 111 * 2) == 0x0213 && r.half(start + 112 * 2) == 0x3032,
         "sample-buffer attribute");
    need(r.half(start + 121 * 2) == 0x0113 && r.half(start + 122 * 2) == 0x3031,
         "sample-count attribute");
    need(r.half(start + 115 * 2) == 0x0113 && r.half(start + 116 * 2) == 19 &&
                 r.half(start + 117 * 2) == 0x004b && r.half(start + 118 * 2) == 0x0103,
         "sample-buffer store");
    need(r.half(start + 125 * 2) == 0x0013 && r.half(start + 126 * 2) == 21 &&
                 r.half(start + 127 * 2) == 0x054b && r.half(start + 128 * 2) == 0x0003,
         "sample-count store");

    for (const auto unit : {117U, 127U}) {
        for (unsigned byte = 0; byte < 4; ++byte) {
            result[start + static_cast<std::size_t>(unit) * 2 + byte] = 0;
        }
    }

    seal(result);
    need(sha256_bytes(result) == constructor.output_sha256, "MSAA output SHA mismatch");

    return result;
}
} // namespace voyemu
