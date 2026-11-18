#include "dex_method_redirect.hpp"
#include "check.hpp"
#include "dex_file.hpp"
#include "dex_inventory.hpp"
#include "error.hpp"
#include "process.hpp"
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>
namespace voyemu {
using namespace dex_file;
namespace {
constexpr Check need{"DEX_REDIRECT"};

struct Caller {
    const char *owner;
    const char *method;
    const char *proto;
    std::uint32_t id, access, units;
    std::array<std::uint16_t, 4> header;
    const char *hash;
    std::vector<std::pair<std::uint32_t, std::uint16_t>> sites;
};

const std::array<Caller, 3> &callers() {
    static const std::array<Caller, 3> value = {
            {{"Lcom/qinggan/launcher/base/utils/AppLauncher;",
              "openMusicPage",
              "(Ljava/lang/String;Z)V",
              30082,
              10,
              122,
              {6, 2, 5, 0},
              "31c76411625cd1b1686665a30488f3cb6dc98a464089b301009c7a39a1b19598",
              {{50, 84}, {114, 84}}},
             {"Lcom/qinggan/media/helper/app/MediaJumpUtils;",
              "toMediaHomePage",
              "(Landroid/content/Context;Lcom/qinggan/media/helper/MediaEnum;Z)V",
              32793,
              9,
              90,
              {7, 3, 3, 0},
              "b80f786fedd7122a113c75edd43842a2b8916a897ce9ecc73ae43d532039ad8f",
              {{44, 99}}},
             {"Lcom/pateo/voyah/mediaCard/home/view/HomeBaseView;",
              "toMediaHomePage",
              "()V",
              2105,
              1,
              112,
              {5, 1, 3, 0},
              "4f89c7770ce05f311213ede4b283a917c37a0c50878d48627756b22b15001863",
              {{104, 1}}}}};

    return value;
}

void method_identity(const Reader &r, std::uint32_t index, const char *owner, const char *name,
                     const char *proto) {
    need(index < r.word(88), "method index");
    const auto at = r.word(92) + index * 8;
    need(r.type(r.half(at)) == owner && r.text(r.word(at + 4)) == name &&
                 r.proto(r.half(at + 2)) == proto,
         "method identity");
}
} // namespace

std::string patch_launcher_media_redirect(const std::string &source) {
    need(source.size() <= 32ULL << 20 && sha256_bytes(source) == launcher_media_source_sha256,
         "original Launcher classes2 pin");
    inspect_dex_inventory(std::as_bytes(std::span(source.data(), source.size())));
    Reader r{source, need};
    const auto digest = sha1(std::string_view(source).substr(32));

    for (unsigned i = 0; i < 20; ++i) {
        need(r.byte(12 + i) == digest[i], "input SHA1");
    }

    need(r.word(8) == adler(std::string_view(source).substr(12)), "input Adler32");
    method_identity(r, 32808, "Lcom/qinggan/media/helper/app/MediaJumpUtils;", "toWeCarFlowPage",
                    "(Ljava/lang/String;Z)V");
    method_identity(r, 32801, "Lcom/qinggan/media/helper/app/MediaJumpUtils;", "toMusicPage",
                    "(Ljava/lang/String;Z)V");
    std::array<unsigned, 3> found{};
    bool target_static = false;
    std::vector<std::size_t> edits;

    for (std::uint32_t c = 0; c < r.word(96); ++c) {
        const auto def = r.word(100) + c * 32;
        const auto owner = r.type(r.word(def));
        std::size_t at = r.word(def + 24);

        if (!at) {
            continue;
        }

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
                const auto delta = r.leb(at);
                need(delta <= std::numeric_limits<std::uint32_t>::max() - index,
                     "method delta overflow");
                index += delta;
                const auto access = r.leb(at), code = r.leb(at);

                if (index == 32801) {
                    need(owner == "Lcom/qinggan/media/helper/app/MediaJumpUtils;" && (access & 8) &&
                                 code >= 112,
                         "target static definition");
                    target_static = true;
                }

                for (std::size_t k = 0; k < callers().size(); ++k) {
                    const auto &p = callers()[k];

                    if (index != p.id) {
                        continue;
                    }

                    method_identity(r, index, p.owner, p.method, p.proto);
                    need(owner == p.owner && access == p.access && code >= 112 && code % 4 == 0,
                         "caller definition");
                    ++found[k];
                    r.extent(code, 16);

                    for (unsigned j = 0; j < 4; ++j) {
                        need(r.half(code + j * 2) == p.header[j], "caller code header");
                    }

                    need(r.word(code + 12) == p.units, "caller instruction units");
                    const auto start = code + 16;
                    r.extent(start, static_cast<std::size_t>(p.units) * 2);
                    need(sha256_bytes(source.substr(start, static_cast<std::size_t>(p.units) *
                                                                   2)) == p.hash,
                         "caller instructions pin");

                    for (const auto &[unit, registers] : p.sites) {
                        need(unit + 3 <= p.units, "callsite extent");
                        const auto pos = start + unit * 2;
                        need(r.half(pos) == 0x2071 && r.half(pos + 2) == 32808 &&
                                     r.half(pos + 4) == registers,
                             "invoke-static35c semantic guard");
                        edits.push_back(pos + 2);
                    }
                }
            }
        }
    }

    for (auto n : found) {
        need(n == 1, "caller selection");
    }

    need(target_static && edits.size() == 4, "redirect coverage");
    std::string result = source;

    for (auto at : edits) {
        result[at] = static_cast<char>(32801);
        result[at + 1] = static_cast<char>(32801 >> 8);
    }

    const auto updated = sha1(std::string_view(result).substr(32));

    for (unsigned i = 0; i < 20; ++i) {
        result[12 + i] = static_cast<char>(updated[i]);
    }

    put32(result, 8, adler(std::string_view(result).substr(12)));
    need(sha256_bytes(result) == launcher_media_output_sha256, "redirect output pin");

    return result;
}
} // namespace voyemu
