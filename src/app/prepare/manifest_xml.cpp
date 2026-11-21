#include "manifest_xml.hpp"
#include "check.hpp"
#include "error.hpp"
#include "little_endian.hpp"
#include <algorithm>
#include <cstdint>
#include <set>
#include <stdexcept>
namespace voyemu {
namespace {
constexpr std::uint32_t none = 0xffffffff;
constexpr std::size_t maximum = 32 * 1024 * 1024;

constexpr Check require{"MANIFEST_XML_INVALID"};

std::uint16_t u16(std::string_view data, std::size_t at) {
    return std::uint16_t(le_read(data, at, 2, require, "uint16 extent"));
}

std::uint32_t u32(std::string_view data, std::size_t at) {
    return std::uint32_t(le_read(data, at, 4, require, "uint32 extent"));
}

void w16(std::string &data, std::size_t at, std::uint16_t value) {
    le_write(data, at, value, 2, require, "write uint16 extent");
}

void w32(std::string &data, std::size_t at, std::uint32_t value) {
    le_write(data, at, value, 4, require, "write uint32 extent");
}

std::u16string ascii(std::string_view value) {
    return std::u16string(value.begin(), value.end());
}

bool letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool digit(char c) {
    return c >= '0' && c <= '9';
}

bool declaration_name(std::string_view name, bool service) {
    if (name.empty() || name.size() >= 0x8000) {
        return false;
    }

    if (!service) {
        if (name.size() > 128 || !letter(name.front())) {
            return false;
        }

        for (const auto c : name) {
            if (!letter(c) && !digit(c) && c != '.' && c != '_' && c != '-') {
                return false;
            }
        }

        return true;
    }

    bool first = true;
    unsigned dots = 0;

    for (const auto c : name) {
        if (c == '.') {
            if (first) {
                return false;
            }

            first = true;
            ++dots;
            continue;
        }

        if (first) {
            if (!letter(c) && c != '_' && c != '$') {
                return false;
            }

            first = false;
        } else if (!letter(c) && !digit(c) && c != '_' && c != '$') {
            return false;
        }
    }

    return !first && dots > 0;
}

struct Chunk {
    std::uint16_t type;
    std::string bytes;
};
} // namespace

std::string append_manifest_declarations(std::string_view input,
                                         const std::vector<std::string> &names,
                                         ManifestDeclaration kind) {
    require(kind == ManifestDeclaration::library || kind == ManifestDeclaration::service,
            "declaration kind");
    const bool service = kind == ManifestDeclaration::service;
    const std::string tag_name = service ? "service" : "uses-library";
    require(!names.empty() && names.size() <= 1024, "declaration count");
    std::set<std::string> unique;

    for (const auto &name : names) {
        require(declaration_name(name, service) && unique.insert(name).second, "declaration names");
    }

    require(input.size() >= 8 && input.size() <= maximum && u16(input, 0) == 3 &&
                    u16(input, 2) == 8 && u32(input, 4) == input.size(),
            "XML header");
    std::vector<Chunk> chunks;

    for (std::size_t offset = 8; offset < input.size();) {
        require(input.size() - offset >= 8, "chunk header");
        const auto size = u32(input, offset + 4), header = std::uint32_t(u16(input, offset + 2));
        require(size >= header && header >= 8 && size <= input.size() - offset, "chunk size");
        require(chunks.size() < 100000, "chunk count");
        chunks.push_back({u16(input, offset), std::string(input.substr(offset, size))});
        offset += size;
    }

    std::size_t pool_index = 0, map_index = 0;
    unsigned pools = 0, maps = 0;

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (chunks[i].type == 1) {
            pool_index = i;
            ++pools;
        }

        if (chunks[i].type == 0x180) {
            map_index = i;
            ++maps;
        }
    }

    require(pools == 1 && chunks[pool_index].bytes.size() >= 28, "string pool");
    const auto &pool = chunks[pool_index].bytes;
    const auto count = u32(pool, 8), start = u32(pool, 20);
    require(count <= 100000 && u16(pool, 2) == 28 && u32(pool, 12) == 0 && u32(pool, 16) == 0 &&
                    u32(pool, 24) == 0 && std::uint64_t(start) == 28 + std::uint64_t(count) * 4 &&
                    start <= pool.size(),
            "unstyled UTF16 pool required");
    // Original string indices remain stable; appended declarations only intern new values.
    std::vector<std::u16string> strings;
    strings.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = std::uint64_t(start) + u32(pool, 28 + i * 4);
        require(offset <= pool.size() && pool.size() - offset >= 2, "string offset");
        const auto length = u16(pool, offset);
        const auto end = offset + 2 + std::uint64_t(length) * 2;
        require(length < 0x8000 && end + 2 <= pool.size() && u16(pool, end) == 0, "UTF16 string");
        std::u16string text;

        for (std::size_t j = 0; j < length; ++j) {
            text.push_back(u16(pool, offset + 2 + j * 2));
        }

        strings.push_back(std::move(text));
    }

    auto index = [&](std::string_view value) -> std::uint32_t {
        const auto found = std::find(strings.begin(), strings.end(), ascii(value));

        return found == strings.end() ? none : static_cast<std::uint32_t>(found - strings.begin());
    };
    const auto space = index("http://schemas.android.com/apk/res/android"), name = index("name");
    require(space != none && name != none && maps == 1, "android:name namespace/map");
    const auto &resource = chunks[map_index].bytes;
    require(resource.size() >= 12 + std::uint64_t(name) * 4 &&
                    u32(resource, 8 + std::size_t(name) * 4) == 0x01010003,
            "android:name resource ID");
    bool application = false;
    std::size_t insertion = chunks.size();
    std::uint32_t line = 0;

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const auto &chunk = chunks[i];

        if (chunk.type != 0x102 && chunk.type != 0x103) {
            continue;
        }

        require(chunk.bytes.size() >= (chunk.type == 0x102 ? 36u : 24u), "element extent");
        const auto id = u32(chunk.bytes, 20);
        require(id < strings.size(), "element name");
        const auto &element = strings[id];

        if (element == u"application") {
            if (chunk.type == 0x102) {
                require(!application, "multiple applications");
                application = true;
            } else {
                require(application && insertion == chunks.size(), "application end");
                insertion = i;
                line = u32(chunk.bytes, 8);
            }
        }

        if (element == ascii(tag_name) && chunk.type == 0x102) {
            const auto at = 16 + std::size_t(u16(chunk.bytes, 24));
            const auto size = u16(chunk.bytes, 26), attributes = u16(chunk.bytes, 28);
            require(size == 20 && at + std::size_t(attributes) * 20 <= chunk.bytes.size(),
                    "attributes");

            for (std::size_t j = 0; j < attributes; ++j) {
                const auto offset = at + j * 20;

                if (u32(chunk.bytes, offset) == space && u32(chunk.bytes, offset + 4) == name) {
                    require(static_cast<unsigned char>(chunk.bytes[offset + 15]) == 3,
                            "declaration value");
                    const auto value = u32(chunk.bytes, offset + 16);
                    require(value < strings.size(), "declaration string");

                    for (const auto &requested : names) {
                        require(strings[value] != ascii(requested), "declaration already present");
                    }
                }
            }
        }
    }

    // Insert before the application closing node and preserve all other original chunks.
    require(application && insertion < chunks.size(), "application missing");
    std::vector<std::uint32_t> offsets;
    std::string additions;
    std::size_t data_size = pool.size() - start;
    auto intern = [&](std::string_view value) -> std::uint32_t {
        const auto existing = index(value);

        if (existing != none) {
            return existing;
        }

        require(data_size + 4 + value.size() * 2 <= maximum, "string pool growth");
        std::string encoded(4 + value.size() * 2, '\0');
        w16(encoded, 0, static_cast<std::uint16_t>(value.size()));

        for (std::size_t i = 0; i < value.size(); ++i) {
            w16(encoded, 2 + i * 2, static_cast<unsigned char>(value[i]));
        }

        offsets.push_back(static_cast<std::uint32_t>(data_size));
        data_size += encoded.size();
        additions += encoded;
        strings.push_back(ascii(value));

        return static_cast<std::uint32_t>(strings.size() - 1);
    };
    const auto tag = intern(tag_name), exported = index("exported");

    if (service) {
        require(exported != none && resource.size() >= 12 + std::uint64_t(exported) * 4 &&
                        u32(resource, 8 + std::size_t(exported) * 4) == 0x01010010,
                "android:exported resource ID");
    }

    struct Attribute {
        std::uint32_t name, value;
        bool boolean = false;
    };
    auto node = [&](std::uint32_t id,
                    const std::vector<Attribute> &attributes = std::vector<Attribute>{}) {
        std::string bytes(36 + attributes.size() * 20, '\0');
        w16(bytes, 0, 0x102);
        w16(bytes, 2, 16);
        w32(bytes, 4, static_cast<std::uint32_t>(bytes.size()));
        w32(bytes, 8, line);
        w32(bytes, 12, none);
        w32(bytes, 16, none);
        w32(bytes, 20, id);
        w16(bytes, 24, 20);
        w16(bytes, 26, 20);
        w16(bytes, 28, static_cast<std::uint16_t>(attributes.size()));

        for (std::size_t i = 0; i < attributes.size(); ++i) {
            const auto offset = 36 + i * 20;
            const auto &attr = attributes[i];
            w32(bytes, offset, space);
            w32(bytes, offset + 4, attr.name);
            w32(bytes, offset + 8, attr.boolean ? none : attr.value);
            w16(bytes, offset + 12, 8);
            bytes[offset + 15] = attr.boolean ? 0x12 : 3;
            w32(bytes, offset + 16, attr.value);
        }

        return bytes;
    };
    auto close = [](const std::string &bytes) {
        auto end = bytes.substr(0, 24);
        w16(end, 0, 0x103);
        w32(end, 4, 24);

        return end;
    };
    std::string nodes;

    for (const auto &requested : names) {
        const auto value = intern(requested);
        std::vector<Attribute> attrs{{name, value}};

        if (service) {
            attrs.push_back({exported, none, true});
        }

        const auto element = node(tag, attrs);

        if (!service) {
            nodes += element + close(element);
        } else {
            const auto filter = node(intern("intent-filter"));
            const auto action = node(intern("action"),
                                     {{name, intern("android.media.browse.MediaBrowserService")}});
            nodes += element + filter + action + close(action) + close(filter) + close(element);
        }
    }

    const auto new_start = std::size_t(start) + offsets.size() * 4;
    require(new_start + data_size + 3 <= maximum, "new string pool extent");
    std::string new_pool((new_start + data_size + 3) & ~std::size_t(3), '\0');
    std::copy_n(pool.begin(), start, new_pool.begin());
    w32(new_pool, 4, static_cast<std::uint32_t>(new_pool.size()));
    w32(new_pool, 8, static_cast<std::uint32_t>(strings.size()));
    w32(new_pool, 20, static_cast<std::uint32_t>(new_start));

    for (std::size_t i = 0; i < offsets.size(); ++i) {
        w32(new_pool, start + i * 4, offsets[i]);
    }

    std::copy(pool.begin() + start, pool.end(), new_pool.begin() + new_start);
    std::copy(additions.begin(), additions.end(),
              new_pool.begin() + new_start + pool.size() - start);
    std::string output(input.substr(0, 8));

    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (i == insertion) {
            output += nodes;
        }

        output += i == pool_index ? new_pool : chunks[i].bytes;
        require(output.size() <= maximum, "output extent");
    }

    w32(output, 4, static_cast<std::uint32_t>(output.size()));

    return output;
}
} // namespace voyemu
