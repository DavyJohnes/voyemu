#include "dex_inventory.hpp"
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace voyemu;
namespace {
void check(bool value) {
    if (!value) {
        throw std::runtime_error("DEX metadata regression");
    }
}

void put(std::vector<std::byte> &bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(at + i) = std::byte((value >> (i * 8)) & 255);
    }
}

std::vector<std::byte> fixture(std::vector<std::pair<std::string, unsigned>> names) {
    const auto count = static_cast<std::uint32_t>(names.size()), types = 112 + count * 4,
               classes = types + count * 4;
    std::vector<std::byte> bytes(classes + count * 32);
    const std::string magic = "dex\n035";

    for (unsigned i = 0; i < magic.size(); ++i) {
        bytes[i] = std::byte(magic[i]);
    }

    put(bytes, 36, 112);
    put(bytes, 40, 0x12345678);
    put(bytes, 56, count);
    put(bytes, 60, 112);
    put(bytes, 64, count);
    put(bytes, 68, types);
    put(bytes, 96, count);
    put(bytes, 100, classes);

    for (unsigned i = 0; i < count; ++i) {
        put(bytes, 112 + i * 4, static_cast<std::uint32_t>(bytes.size()));
        put(bytes, types + i * 4, i);
        put(bytes, classes + i * 32, i);
        check(names[i].second < 128);
        bytes.push_back(std::byte(names[i].second));

        for (unsigned char c : names[i].first) {
            bytes.push_back(std::byte(c));
        }

        bytes.push_back(std::byte{0});
    }

    put(bytes, 32, static_cast<std::uint32_t>(bytes.size()));

    return bytes;
}

void rejects(const std::vector<std::byte> &bytes) {
    bool rejected = false;

    try {
        inspect_dex_inventory(bytes);
    } catch (const std::exception &) {
        rejected = true;
    }

    check(rejected);
}
} // namespace

int main() {
    try {
        const auto good = fixture({{"Lru/own/A;", 10}, {"Lru/own/B;", 10}});
        const auto inventory = inspect_dex_inventory(good);
        check(inventory.version == "035" && inventory.methods == 0 &&
              inventory.classes == std::vector<std::string>{"Lru/own/A;", "Lru/own/B;"});

        for (const char *version : {"037", "038", "039"}) {
            auto bytes = good;

            for (unsigned i = 0; i < 3; ++i) {
                bytes[4 + i] = std::byte(version[i]);
            }

            check(inspect_dex_inventory(bytes).classes == inventory.classes);
        }

        check(inspect_dex_inventory(fixture({{"Lru/\xe8\xbd\xa6;", 6}})).classes.at(0) ==
              "Lru/\xe8\xbd\xa6;");
        check(inspect_dex_inventory(fixture({{"Lru/\xed\xa0\xbd\xed\xb8\x80;", 7}}))
                      .classes.at(0) == "Lru/\xf0\x9f\x98\x80;");

        for (const auto [at, value] : {std::pair<unsigned, unsigned>{32, 1},
                                       {36, 108},
                                       {40, 0x78563412},
                                       {60, 0xfffffffc},
                                       {64, 65536},
                                       {96, 65536},
                                       {128, 2},
                                       {120, 2}}) {
            auto bytes = good;
            put(bytes, at, value);
            rejects(bytes);
        }

        auto bad = good;
        bad[0] = std::byte{0};
        rejects(bad);
        bad = good;
        bad[6] = std::byte{'6'};
        rejects(bad);
        bad = good;
        bad.pop_back();
        put(bad, 32, static_cast<std::uint32_t>(bad.size()));
        rejects(bad);
        rejects(fixture({{"Lru/own/A;", 9}}));
        rejects(fixture({{"Lru/own/A;", 10}, {"Lru/own/A;", 10}}));
        rejects(fixture({{"Lru/\xc0\xaf;", 6}}));
        rejects(fixture({{"Lru/\xed\xa0\xbd;", 6}}));
        rejects(fixture({{"[Lru/A;", 7}}));

        const auto a = fixture({{"Lru/own/A;", 10}}), b = fixture({{"Lru/own/B;", 10}}),
                   c = fixture({{"Lru/own/C;", 10}});
        const std::vector<DexInput> originals = {{"classes.dex", a}};
        const std::vector<DexTemplate> additions = {{b, {"Lru/own/B;"}}};
        auto appended = plan_dex_append(originals, additions);
        check(appended.size() == 1 && appended[0].entry == "classes2.dex" &&
              appended[0].inventory.classes == std::vector<std::string>{"Lru/own/B;"});
        check(plan_dex_append({}, additions).at(0).entry == "classes.dex");
        const std::vector<DexInput> unordered = {{"classes2.dex", b}, {"classes.dex", a}};
        const std::vector<DexTemplate> third = {{c, {"Lru/own/C;"}}};
        check(plan_dex_append(unordered, third).at(0).entry == "classes3.dex");
        auto refuse = [](const std::vector<DexInput> &inputs,
                         const std::vector<DexTemplate> &templates) {
            bool rejected = false;
            try {
                plan_dex_append(inputs, templates);
            } catch (const std::exception &) {
                rejected = true;
            }

            check(rejected);
        };
        refuse(originals, {{a, {"Lru/own/A;"}}});
        refuse(originals, {{b, {"Lru/own/C;"}}});
        refuse(originals, {{b, {"Lru/own/B;", "Lru/own/B;"}}});
        refuse(originals, {{b, {}}});
        refuse({{"classes2.dex", a}}, additions);
        refuse({{"classes01.dex", a}}, additions);
        refuse({{"classes1.dex", a}}, additions);
        refuse({{"classes.dex", a}, {"classes.dex", b}}, third);
        refuse({{"classes.dex", a}, {"classes2.dex", a}}, third);

        rejects({});
        std::cout << "bounded DEX metadata guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
