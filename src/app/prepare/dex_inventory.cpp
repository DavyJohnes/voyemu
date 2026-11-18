#include "dex_inventory.hpp"
#include "error.hpp"
#include <algorithm>
#include <charconv>
#include <set>
#include <stdexcept>
namespace voyemu {
namespace {
void need(bool value) {
    if (!value) {
        throw VoyemuError("DEX_METADATA", "invalid or unsupported input");
    }
}

struct Reader {
    std::span<const std::byte> bytes;

    unsigned byte(std::size_t at) const {
        need(at < bytes.size());

        return std::to_integer<unsigned>(bytes[at]);
    }

    std::uint32_t word(std::size_t at) const {
        need(at <= bytes.size() && bytes.size() - at >= 4);

        return byte(at) | (byte(at + 1) << 8) | (byte(at + 2) << 16) | (byte(at + 3) << 24);
    }

    void table(std::uint32_t count, std::uint32_t offset, unsigned stride,
               std::uint32_t limit) const {
        need(count <= limit);

        if (!count) {
            need(offset == 0);

            return;
        }

        need(offset >= 112 && offset % 4 == 0 && offset <= bytes.size() &&
             count <= (bytes.size() - offset) / stride);
    }

    std::uint32_t uleb(std::size_t &at) const {
        std::uint32_t result = 0;

        for (unsigned i = 0; i < 5; ++i) {
            const unsigned b = byte(at++);

            if (i == 4) {
                need(b <= 15);
            }

            result |= (b & 127) << (7 * i);

            if (!(b & 128)) {
                return result;
            }
        }

        need(false);

        return 0;
    }

    static void append(std::string &result, unsigned c) {
        if (c < 128) {
            result += static_cast<char>(c);
        } else if (c < 2048) {
            result += static_cast<char>(192 | (c >> 6));
            result += static_cast<char>(128 | (c & 63));
        } else if (c < 65536) {
            result += static_cast<char>(224 | (c >> 12));
            result += static_cast<char>(128 | ((c >> 6) & 63));
            result += static_cast<char>(128 | (c & 63));
        } else {
            result += static_cast<char>(240 | (c >> 18));
            result += static_cast<char>(128 | ((c >> 12) & 63));
            result += static_cast<char>(128 | ((c >> 6) & 63));
            result += static_cast<char>(128 | (c & 63));
        }
    }

    std::string descriptor(std::uint32_t offset) const {
        need(offset >= 112 && offset < bytes.size());
        std::size_t at = offset;
        const auto length = uleb(at);
        need(length >= 3 && length <= 1024);
        std::string result;
        unsigned units = 0, high = 0;

        while (byte(at) != 0) {
            need(units < length);
            unsigned c = byte(at++);

            if (c >= 128) {
                const unsigned first = c;
                need((c & 224) == 192 || (c & 240) == 224);
                const bool three = (c & 240) == 224;
                const unsigned second = byte(at++);
                need((second & 192) == 128);
                c = ((first & (three ? 15 : 31)) << 6) | (second & 63);

                if (three) {
                    const unsigned third = byte(at++);
                    need((third & 192) == 128);
                    c = (c << 6) | (third & 63);
                    need(c >= 2048);
                } else {
                    need(c >= 128); // Embedded NUL and overlong encodings cannot name a class.
                }
            }

            ++units;

            if (high) {
                need(c >= 0xdc00 && c <= 0xdfff);
                append(result, 0x10000 + ((high - 0xd800) << 10) + (c - 0xdc00));
                high = 0;
                continue;
            }

            if (c >= 0xd800 && c <= 0xdbff) {
                high = c;
                continue;
            }

            need(c < 0xdc00 || c > 0xdfff);
            append(result, c);
        }

        need(units == length && !high && result.front() == 'L' && result.back() == ';');
        const auto name = result.substr(1, result.size() - 2);
        need(!name.empty() && name.front() != '/' && name.back() != '/' &&
             name.find("//") == std::string::npos);

        for (unsigned char c : name) {
            need(c > 32 && c != '.' && c != ';' && c != '[');
        }

        return result;
    }
};
} // namespace

DexInventory inspect_dex_inventory(std::span<const std::byte> bytes) {
    Reader r{bytes};

    need(bytes.size() >= 112 && bytes.size() <= 128 * 1024 * 1024);
    need(r.byte(0) == 'd' && r.byte(1) == 'e' && r.byte(2) == 'x' && r.byte(3) == 10 &&
         r.byte(7) == 0);
    const std::string version = {static_cast<char>(r.byte(4)), static_cast<char>(r.byte(5)),
                                 static_cast<char>(r.byte(6))};
    need(version == "035" || version == "037" || version == "038" || version == "039");
    need(r.word(32) == bytes.size() && r.word(36) == 112 && r.word(40) == 0x12345678);
    const auto strings = r.word(56), string_offset = r.word(60), types = r.word(64),
               type_offset = r.word(68), methods = r.word(88), classes = r.word(96),
               class_offset = r.word(100);
    r.table(strings, string_offset, 4, 1000000);
    r.table(types, type_offset, 4, 65535);
    r.table(r.word(72), r.word(76), 12, 65535);
    r.table(r.word(80), r.word(84), 8, 65535);
    r.table(methods, r.word(92), 8, 65535);
    r.table(classes, class_offset, 32, 65535);
    need(classes <= types);
    DexInventory result{version, methods, {}};
    std::set<std::string> unique;
    std::size_t total = 0;

    for (std::uint32_t i = 0; i < classes; ++i) {
        const auto type = r.word(class_offset + i * 32);
        need(type < types);
        const auto string = r.word(type_offset + type * 4);
        need(string < strings);
        auto name = r.descriptor(r.word(string_offset + string * 4));
        total += name.size();
        need(total <= 8 * 1024 * 1024 && unique.insert(name).second);
        result.classes.push_back(std::move(name));
    }

    return result;
}

std::vector<DexAppendSlot> plan_dex_append(std::span<const DexInput> original,
                                           std::span<const DexTemplate> templates) {
    auto require = [](bool value, const char *message) {
        if (!value) {
            throw VoyemuError("DEX_APPEND", message);
        }
    };

    require(original.size() <= 65535 && templates.size() <= 65535 - original.size(),
            "too many DEX slots");
    std::set<unsigned> slots;
    std::set<std::string> classes;
    std::size_t total_names = 0;
    auto remember = [&](const DexInventory &inventory) {
        for (const auto &name : inventory.classes) {
            total_names += name.size();
            require(total_names <= 64 * 1024 * 1024 && classes.size() < 1000000,
                    "class registry exceeds limit");
            require(classes.insert(name).second, "class collision; explicit DEX rewrite required");
        }
    };

    for (const auto &dex : original) {
        unsigned slot = 1;

        if (dex.entry != "classes.dex") {
            require(dex.entry.starts_with("classes") && dex.entry.ends_with(".dex") &&
                            dex.entry.size() > 11,
                    "noncanonical DEX entry");
            const auto suffix = std::string_view(dex.entry).substr(7, dex.entry.size() - 11);
            require(suffix.front() != '0', "noncanonical DEX slot");
            const auto parsed = std::from_chars(suffix.data(), suffix.data() + suffix.size(), slot);
            require(parsed.ec == std::errc{} && parsed.ptr == suffix.data() + suffix.size() &&
                            slot >= 2,
                    "invalid DEX slot");
        }

        require(slots.insert(slot).second, "duplicate DEX slot");
        remember(inspect_dex_inventory(dex.bytes));
    }

    for (unsigned slot = 1; slot <= original.size(); ++slot) {
        require(slots.contains(slot), "DEX slot gap");
    }

    std::vector<DexAppendSlot> result;

    for (const auto &component : templates) {
        auto inventory = inspect_dex_inventory(component.bytes);
        require(component.expected_classes.size() <= 65535, "template allowlist extent");
        std::size_t expected_bytes = 0;

        for (const auto &name : component.expected_classes) {
            require(name.size() <= 4096, "template class name extent");
            expected_bytes += name.size();
            require(expected_bytes <= 8 * 1024 * 1024, "template allowlist budget");
        }

        const std::set<std::string> expected(component.expected_classes.begin(),
                                             component.expected_classes.end()),
                actual(inventory.classes.begin(), inventory.classes.end());
        require(!expected.empty() && expected.size() == component.expected_classes.size() &&
                        actual == expected,
                "template class allowlist mismatch");
        remember(inventory);
        const auto slot = original.size() + result.size() + 1;
        result.push_back({slot == 1 ? "classes.dex" : "classes" + std::to_string(slot) + ".dex",
                          std::move(inventory)});
    }

    return result;
}
} // namespace voyemu
