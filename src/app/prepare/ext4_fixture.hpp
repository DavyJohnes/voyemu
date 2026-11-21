#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
namespace voyemu::ext4_fixture {
inline void put(std::string &bytes, std::size_t at, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) {
        bytes.at(at + i) = static_cast<char>(value >> (8 * i));
    }
}

// A minimal 1 KiB-block ext4 image with extent-mapped files and the directories they imply.
inline std::string build(const std::map<std::string, std::string> &files) {
    constexpr unsigned blocks = 128, inodes = 32, data = 7;
    std::string image(std::size_t(blocks) * 1024, '\0');

    put(image, 1024, inodes, 4);
    put(image, 1024 + 4, blocks, 4);
    put(image, 1024 + 20, 1, 4);
    put(image, 1024 + 32, blocks, 4);
    put(image, 1024 + 40, inodes, 4);
    put(image, 1024 + 56, 0xef53, 2);
    put(image, 1024 + 88, 128, 2);
    put(image, 1024 + 96, 0x42, 4);
    put(image, 2048 + 8, 3, 4);
    std::map<std::string, std::vector<std::pair<std::string, unsigned>>> directories{{"", {}}};
    unsigned next_inode = 11, next_block = data;
    auto node = [&](unsigned id, unsigned mode, const std::string &content) {
        const unsigned count = std::max<unsigned>(1, (content.size() + 1023) / 1024);

        if (next_block + count > blocks || id > inodes) {
            throw std::runtime_error("ext4 fixture bounds");
        }

        const auto at = 3072 + (id - 1) * 128;
        put(image, at, mode, 2);
        put(image, at + 4, content.size(), 4);
        put(image, at + 32, 0x80000, 4);
        put(image, at + 40, 0xf30a, 2);
        put(image, at + 42, 1, 2);
        put(image, at + 44, 4, 2);
        put(image, at + 56, count, 2);
        put(image, at + 60, next_block, 4);
        image.replace(std::size_t(next_block) * 1024, content.size(), content);
        next_block += count;
    };
    std::map<std::string, unsigned> ids{{"", 2}};

    for (const auto &[path, content] : files) {
        std::string parent;

        for (std::size_t at = 1; at <= path.size(); ++at) {
            if (at == path.size() || path[at] == '/') {
                const auto current = path.substr(0, at);

                if (!ids.contains(current)) {
                    ids[current] = next_inode++;
                    directories[parent].emplace_back(
                            path.substr(parent.size() + 1, at - parent.size() - 1), ids[current]);

                    if (at != path.size()) {
                        directories[current];
                    }
                }

                parent = current;
            }
        }

        node(ids[path], 0100644, content);
    }

    for (const auto &[path, children] : directories) {
        std::string block(1024, '\0');
        std::size_t at = 0;

        for (std::size_t i = 0; i < children.size(); ++i) {
            const auto &[name, id] = children[i];
            const auto length = i + 1 == children.size() ? 1024 - at
                                                         : (8 + name.size() + 3) / 4 * 4;
            put(block, at, id, 4);
            put(block, at + 4, length, 2);
            put(block, at + 6, name.size(), 1);
            block.replace(at + 8, name.size(), name);
            at += length;
        }

        node(ids[path], 040755, block);
    }

    return image;
}
} // namespace voyemu::ext4_fixture
