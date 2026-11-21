#include "qnx_link_interface.hpp"
#include "check.hpp"
#include "error.hpp"
#include "little_endian.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid QNX link interface"};

std::uint64_t field(std::string_view bytes, std::uint64_t at, unsigned width) {
    return le_read(bytes, at, width, need, "QNX_LINK_ELF_BOUNDS");
}

void aarch64_elf(std::string_view bytes) {
    need(bytes.size() >= 64 && bytes.size() <= (64ULL << 20) &&
                 bytes.substr(0, 6) == std::string_view("\x7f"
                                                        "ELF\x02\x01",
                                                        6) &&
                 field(bytes, 18, 2) == 183,
         "QNX_LINK_ELF_TYPE");
}

std::string_view text(std::string_view bytes, std::uint64_t table, std::uint64_t size,
                      std::uint64_t index) {
    need(table <= bytes.size() && size <= bytes.size() - table && index < size,
         "QNX_LINK_ELF_STRING_INDEX");
    const auto remaining = bytes.substr(table + index, size - index);
    const auto end = remaining.find('\0');
    need(end != std::string_view::npos && end <= 512, "QNX_LINK_ELF_STRING_END");

    return remaining.substr(0, end);
}
} // namespace

std::vector<QnxElfSection> qnx_elf_sections(std::string_view bytes) {
    aarch64_elf(bytes);
    const auto headers = field(bytes, 40, 8), width = field(bytes, 58, 2),
               count = field(bytes, 60, 2), names = field(bytes, 62, 2);
    need(width == 64 && count > 0 && count <= 256 && names < count && headers <= bytes.size() &&
                 count * width <= bytes.size() - headers,
         "QNX_LINK_ELF_SECTIONS");
    std::vector<QnxElfSection> sections(count);

    for (std::uint64_t i = 0; i < count; ++i) {
        const auto at = headers + i * width;
        auto &section = sections[i];
        section.type = field(bytes, at + 4, 4);
        section.flags = field(bytes, at + 8, 8);
        section.address = field(bytes, at + 16, 8);
        section.offset = field(bytes, at + 24, 8);
        section.size = field(bytes, at + 32, 8);
        section.link = field(bytes, at + 40, 4);
        need(section.type == 8 || (section.offset <= bytes.size() &&
                                   section.size <= bytes.size() - section.offset),
             "QNX_LINK_ELF_SECTION_BOUNDS");
    }

    const auto &table = sections[names];

    for (std::uint64_t i = 0; i < count; ++i) {
        sections[i].name = text(bytes, table.offset, table.size,
                                field(bytes, headers + i * width, 4));
    }

    return sections;
}

std::set<std::string> qnx_link_exports(std::string_view bytes, std::string_view soname) {
    aarch64_elf(bytes);
    need(field(bytes, 16, 2) == 3, "QNX_LINK_ELF_TYPE");
    const auto headers = field(bytes, 32, 8), width = field(bytes, 54, 2),
               count = field(bytes, 56, 2);
    need(width == 56 && count > 0 && count <= 64, "QNX_LINK_ELF_HEADERS");
    std::vector<std::array<std::uint64_t, 3>> loads;
    std::uint64_t dynamic = 0, dynamic_size = 0;

    for (std::uint64_t i = 0; i < count; ++i) {
        const auto at = headers + i * width, type = field(bytes, at, 4),
                   offset = field(bytes, at + 8, 8), address = field(bytes, at + 16, 8),
                   size = field(bytes, at + 32, 8);
        need(offset <= bytes.size() && size <= bytes.size() - offset, "QNX_LINK_ELF_SEGMENT");

        if (type == 1) {
            loads.push_back({address, size, offset});
        }

        if (type == 2) {
            need(dynamic_size == 0, "QNX_LINK_ELF_DYNAMIC_DUPLICATE");
            dynamic = offset;
            dynamic_size = size;
        }
    }

    auto offset = [&](std::uint64_t address) {
        for (const auto &segment : loads) {
            if (address >= segment[0] && address - segment[0] < segment[1]) {
                return segment[2] + address - segment[0];
            }
        }

        throw VoyemuError("QNX_LINK_ELF_ADDRESS", "ELF address outside the link interface");
    };
    need(dynamic_size > 0 && dynamic_size % 16 == 0, "QNX_LINK_ELF_DYNAMIC");
    std::map<std::uint64_t, std::uint64_t> tags;

    for (auto at = dynamic; at < dynamic + dynamic_size; at += 16) {
        const auto tag = field(bytes, at, 8);

        if (tag == 0) {
            break;
        }

        const auto value = field(bytes, at + 8, 8);
        const auto [old, inserted] = tags.emplace(tag, value);
        need(inserted || old->second == value || tag == 1, "QNX_LINK_ELF_DYNAMIC_CONFLICT");
    }

    need(tags.contains(5) && tags.contains(6) && tags.contains(10) && tags.contains(14) &&
                 (tags.contains(0x6ffffef5) || tags.contains(4)) && tags.contains(11) &&
                 tags.at(11) == 24,
         "QNX_LINK_ELF_TABLES");
    const auto strings = offset(tags.at(5)), symbols = offset(tags.at(6)),
               string_bytes = tags.at(10);
    need(strings <= bytes.size() && string_bytes <= bytes.size() - strings, "QNX_LINK_ELF_STRINGS");
    auto name = [&](std::uint64_t index) {
        return text(bytes, strings, string_bytes, index);
    };
    need(name(tags.at(14)) == soname, "QNX_LINK_ELF_SONAME");
    std::uint64_t last = 0;

    // A SysV hash table counts every dynamic symbol; a GNU one only through its chains.
    if (!tags.contains(0x6ffffef5)) {
        last = field(bytes, offset(tags.at(4)) + 4, 4);
        need(last > 1 && last <= 65536, "QNX_LINK_ELF_HASH_GEOMETRY");
        --last;
    }

    const auto hash = tags.contains(0x6ffffef5) ? offset(tags.at(0x6ffffef5)) : 0;
    const auto buckets = hash ? field(bytes, hash, 4) : 1,
               first = hash ? field(bytes, hash + 4, 4) : 0,
               bloom = hash ? field(bytes, hash + 8, 4) : 1;
    need(buckets > 0 && buckets <= 65536 && bloom > 0 && bloom <= 65536,
         "QNX_LINK_ELF_HASH_GEOMETRY");
    const auto bucket_offset = hash + 16 + bloom * 8, chains = bucket_offset + buckets * 4;

    for (std::uint64_t i = 0; hash && i < buckets; ++i) {
        auto id = field(bytes, bucket_offset + i * 4, 4);

        if (id == 0) {
            continue;
        }

        need(id >= first, "QNX_LINK_ELF_HASH_INDEX");

        for (unsigned guard = 0;; ++guard, ++id) {
            need(guard < 65536 && id < 65536, "QNX_LINK_ELF_HASH_CHAIN");
            const auto value = field(bytes, chains + (id - first) * 4, 4);
            last = std::max(last, id);

            if (value & 1) {
                break;
            }
        }
    }

    need(last > 0, "QNX_LINK_ELF_EXPORTS");
    std::set<std::string> defined;

    for (std::uint64_t id = 1; id <= last; ++id) {
        const auto at = symbols + id * 24, index = field(bytes, at, 4),
                   info = field(bytes, at + 4, 1), section = field(bytes, at + 6, 2),
                   value = field(bytes, at + 8, 8);

        if (index == 0 || section == 0 || value == 0) {
            continue;
        }

        const auto bind = info >> 4;

        if (bind != 1 && bind != 2) {
            continue;
        }

        const auto symbol = name(index);
        need(!symbol.empty() &&
                     symbol.find_first_not_of(
                             "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_.$") ==
                             std::string_view::npos,
             "QNX_LINK_ELF_SYMBOL_NAME");
        defined.insert(std::string(symbol));
    }

    need(!defined.empty(), "QNX_LINK_ELF_EXPORTS");

    return defined;
}

QnxLinkImports qnx_link_imports(std::string_view bytes) {
    const auto sections = qnx_elf_sections(bytes);
    QnxLinkImports imports;
    unsigned dynamic = 0, symbols = 0;

    for (const auto &section : sections) {
        if (section.type != 6 && section.type != 11) {
            continue;
        }

        const auto link = section.link;
        need(link < sections.size() && sections[link].type == 3, "QNX_LINK_ELF_STRINGS");
        const auto &strings = sections[link];

        if (section.type == 6) {
            ++dynamic;
            need(section.size % 16 == 0, "QNX_LINK_ELF_DYNAMIC");

            for (std::uint64_t at = 0; at < section.size; at += 16) {
                const auto tag = field(bytes, section.offset + at, 8);

                if (tag == 0) {
                    break;
                }

                if (tag == 1) {
                    imports.needed.emplace_back(text(bytes, strings.offset, strings.size,
                                                     field(bytes, section.offset + at + 8, 8)));
                }
            }

            continue;
        }

        ++symbols;
        need(section.size % 24 == 0 && section.size / 24 <= 65536, "QNX_LINK_ELF_SYMBOLS");

        for (std::uint64_t at = 24; at < section.size; at += 24) {
            const auto entry = section.offset + at, info = field(bytes, entry + 4, 1);

            if (field(bytes, entry + 6, 2) != 0) {
                continue;
            }

            const auto symbol =
                    std::string(text(bytes, strings.offset, strings.size, field(bytes, entry, 4)));
            need(!symbol.empty() && (info >> 4 == 1 || info >> 4 == 2), "QNX_LINK_ELF_SYMBOL_NAME");
            (info >> 4 == 2 ? imports.weak : imports.strong).insert(symbol);
        }
    }

    need(dynamic == 1 && symbols == 1, "QNX_LINK_ELF_TABLES");

    return imports;
}

void verify_qnx_link_imports(
        std::string_view module,
        const std::vector<std::pair<std::string, std::string_view>> &libraries) {
    const auto imports = qnx_link_imports(module);
    std::vector<std::string> sonames;
    std::set<std::string> exported;

    for (const auto &[soname, library] : libraries) {
        sonames.push_back(soname);
        exported.merge(qnx_link_exports(library, soname));
    }

    if (imports.needed != sonames) {
        throw VoyemuError("QNX_LINK_NEEDED", "guest module libraries differ");
    }

    for (const auto &symbol : imports.strong) {
        if (!exported.contains(symbol)) {
            throw VoyemuError("QNX_LINK_IMPORT", "OEM libraries do not export " + symbol);
        }
    }
}

void verify_link_exports(std::string_view module,
                         const std::vector<std::pair<std::string, std::string_view>> &libraries) {
    std::set<std::string> exported;

    for (const auto &[soname, library] : libraries) {
        exported.merge(qnx_link_exports(library, soname));
    }

    for (const auto &symbol : qnx_link_imports(module).strong) {
        if (!exported.contains(symbol)) {
            throw VoyemuError("QNX_LINK_IMPORT", "guest libraries do not export " + symbol);
        }
    }
}
} // namespace voyemu
