#include "apk_dex.hpp"
#include "model.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
constexpr char good_hex[] =
        "504b030414000000080083314c5d12e573450e0000000c00000013000000416e64726f69644d616e69666573742e78"
        "6d6ccb4b2cc92c4b65a8ca2cf8cf0500504b030414000008080083314c5d03ea60490d0000000b0000001700000061"
        "73736574732fd0b4d0b0d0bdd0bdd18bd0b52e747874cb2fcf5348cbac28292d4a0500504b01021403140000000800"
        "83314c5d12e573450e0000000c000000130000000000000000000000800100000000416e64726f69644d616e696665"
        "73742e786d6c504b0102140314000008080083314c5d03ea60490d0000000b00000017000000000000000000000080"
        "013f0000006173736574732fd0b4d0b0d0bdd0bdd18bd0b52e747874504b0506000000000200020086000000810000"
        "000000";
namespace {
void check(bool value) {
    if (!value) {
        throw std::runtime_error("APK DEX regression");
    }
}

std::string decode(const std::string &hex) {
    std::string result;

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        result += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
    }

    return result;
}

std::string dex(char letter) {
    std::string bytes(152, 0);
    auto word = [&](unsigned at, unsigned value) {
        for (unsigned i = 0; i < 4; ++i) {
            bytes.at(at + i) = static_cast<char>((value >> (i * 8)) & 255);
        }
    };

    bytes.replace(0, 7, "dex\n035", 7);
    word(36, 112);
    word(40, 0x12345678);
    word(56, 1);
    word(60, 112);
    word(64, 1);
    word(68, 116);
    word(96, 1);
    word(100, 120);
    word(112, 152);
    const std::string name = std::string("Lru/own/") + letter + ";";
    bytes += static_cast<char>(name.size());
    bytes += name;
    bytes += char(0);
    word(32, static_cast<unsigned>(bytes.size()));

    return bytes;
}
} // namespace

int main() {
    try {
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-apk-dex-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        auto store = [&](const char *name, const std::string &bytes) {
            auto p = root / name;
            std::ofstream f(p, std::ios::binary);
            f.write(bytes.data(), bytes.size());
            f.close();
            check(bool(f));

            return p;
        };
        const auto source = store("source.apk", decode(good_hex));
        const auto source_hash = sha256_file(source);
        const auto a = dex('A'), b = dex('B');
        const auto a_hash = sha256_bytes(a), b_hash = sha256_bytes(b);
        const std::vector<ApkDexTemplate> first = {{a, a_hash, {"Lru/own/A;"}}},
                                          second = {{b, b_hash, {"Lru/own/B;"}}};
        const auto candidate = root / "first.apk";
        const auto added = append_pinned_apk_dex(source, source_hash, {}, first, candidate, 0);
        check(added.sha256 == sha256_file(candidate) && added.slots.size() == 1 &&
              added.slots[0].entry == "classes.dex");
        check(read_pinned_zip_entry(candidate, added.sha256, "classes.dex", a_hash) == a);
        const std::string manifest("native\0zip\xff\n", 12);
        check(read_pinned_zip_entry(candidate, added.sha256, "AndroidManifest.xml",
                                    sha256_bytes(manifest)) == manifest);
        const std::vector<ApkDexPin> originals = {{"classes.dex", a_hash}};
        const auto next = root / "second.apk";
        const auto more = append_pinned_apk_dex(candidate, added.sha256, originals, second, next,
                                                0);
        check(more.slots[0].entry == "classes2.dex" &&
              read_pinned_zip_entry(next, more.sha256, "classes.dex", a_hash) == a &&
              read_pinned_zip_entry(next, more.sha256, "classes2.dex", b_hash) == b);
        unsigned index = 0;
        auto refuses = [&](const std::filesystem::path &input, const std::string &hash,
                           const std::vector<ApkDexPin> &pins,
                           const std::vector<ApkDexTemplate> &templates) {
            const auto destination = root / ("rejected-" + std::to_string(++index) + ".apk");
            bool rejected = false;
            try {
                append_pinned_apk_dex(input, hash, pins, templates, destination, 0);
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected && !std::filesystem::exists(destination));
        };
        refuses(source, std::string(64, '0'), {}, first);
        refuses(source, source_hash, {}, {{a, std::string(64, '0'), {"Lru/own/A;"}}});
        refuses(source, source_hash, {}, {{a, a_hash, {"Lru/own/B;"}}});
        refuses(source, source_hash, originals, first);
        refuses(candidate, added.sha256, {}, second);
        refuses(candidate, added.sha256, {{"classes.dex", std::string(64, '0')}}, second);
        refuses(candidate, added.sha256, originals, first);
        refuses(candidate, added.sha256, {{"classes.dex", a_hash}, {"classes.dex", a_hash}},
                second);
        const auto protected_file = store("protected.apk", "keep");
        bool rejected = false;
        try {
            append_pinned_apk_dex(source, source_hash, {}, first, protected_file, 0);
        } catch (const std::exception &) {
            rejected = true;
        }

        check(rejected && sha256_file(protected_file) == sha256_bytes("keep") &&
              sha256_file(source) == source_hash);
        std::cout << "pinned native APK DEX append passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
