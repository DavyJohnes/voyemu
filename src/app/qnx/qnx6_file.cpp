#include "qnx6_file.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid QNX6 file system"};

std::uint64_t integer(std::string_view bytes, std::size_t at, unsigned width) {
    need(width <= 8 && at + width <= bytes.size(), "QNX6_FIELD_BOUNDS");
    std::uint64_t value = 0;

    for (unsigned i = 0; i < width; ++i) {
        value |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    }

    return value;
}

std::uint32_t crc(std::string_view data) {
    std::uint32_t value = 0;

    for (unsigned char c : data) {
        value ^= std::uint32_t(c) << 24;

        for (unsigned i = 0; i < 8; ++i) {
            value = (value << 1) ^ ((value & 0x80000000) ? 0x04c11db7 : 0);
        }
    }

    return value;
}

struct Node {
    std::uint64_t size;
    std::array<std::uint32_t, 16> pointers;
    unsigned levels, mode;
};
struct Reader {
    std::ifstream input;
    std::uint64_t extent, base = 0;
    unsigned blocksize = 0, blocks = 0, inode_count = 0;
    Node table{}, longnames{};

    explicit Reader(const std::filesystem::path &image)
        : input(image, std::ios::binary), extent(std::filesystem::file_size(image)) {
        need(bool(input) && extent >= 0x4000 && extent <= 4ULL * 1024 * 1024 * 1024,
             "QNX6_IMAGE_BOUNDS");
    }

    Reader(const std::filesystem::path &image, std::uint64_t start, std::uint64_t size)
        : Reader(image) {
        need(start <= extent && size <= extent - start, "QNX6_PARTITION_BOUNDS");
        base = start;
        extent = size;
    }

    std::string bytes(std::uint64_t offset, std::uint64_t count) {
        need(offset <= extent && count <= extent - offset && count <= 64 * 1024 * 1024,
             "QNX6_READ_BOUNDS");
        std::string data(static_cast<std::size_t>(count), 0);
        input.seekg(static_cast<std::streamoff>(base + offset));
        input.read(data.data(), static_cast<std::streamsize>(count));
        need(bool(input), "QNX6_READ");

        return data;
    }

    Node node(std::string_view data, bool root) {
        Node result{};
        result.size = integer(data, 0, 8);
        const auto at = root ? 8 : 36;

        for (unsigned i = 0; i < 16; ++i) {
            result.pointers[i] = static_cast<std::uint32_t>(integer(data, at + i * 4, 4));
        }

        result.levels = static_cast<unsigned>(integer(data, root ? 72 : 100, 1));
        result.mode = root ? 0100000 : static_cast<unsigned>(integer(data, 32, 2));
        need(result.levels <= 5, "QNX6_INDIRECTION_DEPTH");

        return result;
    }

    std::uint64_t address(std::uint32_t block) {
        need(block < blocks, "QNX6_BLOCK_POINTER");

        return 0x3000 + std::uint64_t(block) * blocksize;
    }

    std::uint64_t block(const Node &file, std::uint64_t index) {
        const std::uint64_t fanout = blocksize / 4;
        std::uint64_t span = 1;

        for (unsigned i = 0; i < file.levels; ++i) {
            need(span <= UINT64_MAX / fanout, "QNX6_BLOCK_OVERFLOW");
            span *= fanout;
        }

        const auto top = index / span;
        need(top < 16, "QNX6_FILE_CAPACITY");
        std::uint32_t value = file.pointers[top];

        for (unsigned i = 0; i < file.levels; ++i) {
            span /= fanout;
            value = static_cast<std::uint32_t>(
                    integer(bytes(address(value) + (index / span % fanout) * 4, 4), 0, 4));
        }

        return address(value);
    }

    std::string range(const Node &file, std::uint64_t offset, std::uint64_t count) {
        need(offset <= file.size && count <= file.size - offset && count <= 64 * 1024 * 1024,
             "QNX6_FILE_BOUNDS");
        std::string out;
        out.reserve(static_cast<std::size_t>(count));

        while (count) {
            const auto within = offset % blocksize,
                       take = std::min<std::uint64_t>(count, blocksize - within);
            out += bytes(block(file, offset / blocksize) + within, take);
            offset += take;
            count -= take;
        }

        return out;
    }

    std::string name(std::string_view entry) {
        const auto length = static_cast<unsigned>(integer(entry, 4, 1));
        std::string result;

        if (length == 255) {
            const auto offset = integer(entry, 8, 4) * blocksize;
            const auto prefix = range(longnames, offset, 2);
            const auto count = integer(prefix, 0, 2);
            need(count > 27 && count <= 510, "QNX6_LONGNAME_LENGTH");
            result = range(longnames, offset + 2, count);
            std::uint32_t sum = 0;

            for (unsigned char c : result) {
                sum = ((sum >> 1) | (sum << 31)) + c;
            }

            need(sum == integer(entry, 12, 4), "QNX6_LONGNAME_CHECKSUM");
        } else {
            need(length > 0 && length <= 27, "QNX6_DIRECTORY_NAME");
            result = std::string(entry.substr(5, length));
        }

        need(result.find_first_of("/\\\\") == std::string::npos &&
                     result.find('\0') == std::string::npos,
             "QNX6_DIRECTORY_NAME");

        return result;
    }

    Node inode(std::uint32_t id) {
        need(id > 0 && id <= inode_count, "QNX6_INODE_NUMBER");

        return node(range(table, std::uint64_t(id - 1) * 128, 128), false);
    }

    std::string super(std::uint64_t offset) {
        auto data = bytes(offset, 512);
        need(integer(data, 0, 4) == 0x68191122 &&
                     integer(data, 4, 4) == crc(std::string_view(data).substr(8)),
             "QNX6_SUPERBLOCK_CHECKSUM");

        return data;
    }

    void open() {
        auto first = super(0x2000);
        blocksize = static_cast<unsigned>(integer(first, 48, 4));
        blocks = static_cast<unsigned>(integer(first, 60, 4));
        inode_count = static_cast<unsigned>(integer(first, 52, 4));
        need(blocksize >= 512 && blocksize <= 65536 && (blocksize & (blocksize - 1)) == 0 &&
                     inode_count > 0 && blocks > 0,
             "QNX6_SUPERBLOCK_GEOMETRY");
        // Both accepted on-disk arrangements are supported for the second superblock: the
        // flat offset behind the full data area (OEM images) and the mkqnx6fs tail placement
        // in the image's last block (the guest-accepted seed volume layout).
        std::string second;

        for (const auto where : {0x3000 + std::uint64_t(blocks) * blocksize, extent - blocksize}) {
            try {
                second = super(where);
            } catch (...) {
                continue;
            }

            if (integer(second, 48, 4) == blocksize && integer(second, 60, 4) == blocks &&
                integer(second, 52, 4) == inode_count) {
                break;
            }
        }

        need(!second.empty(), "QNX6_SUPERBLOCK_PAIR");
        const auto &active = integer(first, 8, 8) >= integer(second, 8, 8) ? first : second;
        table = node(std::string_view(active).substr(72, 80), true);
        longnames = node(std::string_view(active).substr(232, 80), true);
        need(table.size >= std::uint64_t(inode_count) * 128, "QNX6_INODE_TABLE");
    }

    Node resolve(std::string_view relative, bool owner_config = false) {
        need(!relative.empty() && relative.front() != '/' &&
                     relative.find('\\') == std::string_view::npos,
             "QNX6_RELATIVE_PATH");
        std::vector<std::string> parts;

        for (std::size_t at = 0; at < relative.size();) {
            auto end = relative.find('/', at);

            if (end == std::string_view::npos) {
                end = relative.size();
            }

            auto part = relative.substr(at, end - at);
            need(!part.empty() && part != "." && part != ".." && part.size() <= 510 &&
                         part.find('\0') == std::string_view::npos,
                 "QNX6_PATH_COMPONENT");
            parts.emplace_back(part);
            at = end + 1;
        }

        need(parts.size() <= 16, "QNX6_PATH_DEPTH");
        auto current = inode(1);

        for (const auto &part : parts) {
            need((current.mode & 0170000) == 0040000 && current.size <= 4 * 1024 * 1024 &&
                         current.size % 32 == 0,
                 "QNX6_DIRECTORY");
            const auto entries = range(current, 0, current.size);
            std::uint32_t found = 0;

            for (std::size_t at = 0; at < entries.size(); at += 32) {
                const auto id = static_cast<std::uint32_t>(integer(entries, at, 4));

                if (id && name(std::string_view(entries).substr(at, 32)) == part) {
                    need(found == 0, "QNX6_AMBIGUOUS_ENTRY");
                    found = id;
                }
            }

            need(found != 0, "QNX6_FILE_MISSING");
            current = inode(found);
        }

        need((current.mode & 0170000) == 0100000, "QNX6_REGULAR_FILE_REQUIRED");
        need(!owner_config ||
                     ((current.mode & 07777) == 0600 && current.size > 0 && current.size <= 65536),
             "QNX6_CONFIG_MODE_SIZE");

        return current;
    }

    std::string file(std::string_view relative, bool owner_config = false) {
        const auto current = resolve(relative, owner_config);

        return range(current, 0, current.size);
    }
};
} // namespace

void validate_qnx6_image(const std::filesystem::path &image) {
    need(image.is_absolute() && !std::filesystem::is_symlink(image) &&
                 std::filesystem::is_regular_file(image),
         "QNX6_IMAGE_PATH");
    const auto epoch = cache_file_identity(image);
    Reader fs(image);
    fs.open();
    need((fs.inode(1).mode & 0170000) == 0040000 && cache_file_identity(image) == epoch,
         "QNX6_ROOT_DIRECTORY");
}

std::string read_qnx6_file(const std::filesystem::path &image, std::string_view pin,
                           std::string_view relative) {
    need(image.is_absolute() && !std::filesystem::is_symlink(image) &&
                 std::filesystem::is_regular_file(image) && sha256_file(image) == pin,
         "QNX6_INPUT_SHA");
    const auto epoch = cache_file_identity(image);
    Reader fs(image);
    fs.open();
    auto data = fs.file(relative);
    need(cache_file_identity(image) == epoch, "QNX6_INPUT_CHANGED");

    return data;
}

void replace_qnx6_file(const std::filesystem::path &image, std::string_view pin,
                       std::string_view relative, std::string_view original_pin,
                       std::string_view payload, std::uint64_t base) {
    need(image.is_absolute() && std::filesystem::canonical(image) == image &&
                 !std::filesystem::is_symlink(image) && std::filesystem::is_regular_file(image) &&
                 std::filesystem::hard_link_count(image) == 1 && sha256_file(image) == pin,
         "QNX6_REPLACE_IMAGE");
    const auto extent = std::filesystem::file_size(image);
    need(base < extent && !payload.empty() && payload.size() <= 64 * 1024 * 1024,
         "QNX6_REPLACE_BOUNDS");
    Reader fs(image, base, extent - base);
    fs.open();
    const auto node = fs.resolve(relative);
    const auto original = fs.range(node, 0, node.size);
    need(node.size == payload.size() && sha256_bytes(original) == original_pin,
         "QNX6_REPLACE_FILE");
    std::vector<std::uint64_t> offsets;

    for (std::uint64_t at = 0; at < node.size; at += fs.blocksize) {
        const auto offset = fs.block(node, at / fs.blocksize);
        need(std::find(offsets.begin(), offsets.end(), offset) == offsets.end(),
             "QNX6_REPLACE_ALIAS");
        offsets.push_back(offset);
    }

    std::fstream out(image, std::ios::binary | std::ios::in | std::ios::out);
    need(bool(out), "QNX6_REPLACE_OPEN");
    std::uint64_t at = 0;

    for (const auto offset : offsets) {
        const auto count = std::min<std::uint64_t>(fs.blocksize, node.size - at);
        out.seekp(static_cast<std::streamoff>(base + offset));
        out.write(payload.data() + at, static_cast<std::streamsize>(count));
        need(bool(out), "QNX6_REPLACE_WRITE");
        at += count;
    }

    out.flush();
    need(bool(out), "QNX6_REPLACE_FLUSH");
    out.close();
    Reader checked(image, base, extent - base);
    checked.open();
    need(checked.file(relative) == payload && std::filesystem::file_size(image) == extent,
         "QNX6_REPLACE_READBACK");
}

std::string validate_qnx6_system_initialization(const std::filesystem::path &source,
                                                std::string_view source_pin,
                                                const std::filesystem::path &stopped,
                                                std::string_view stopped_pin) {
    auto ordinary = [](const std::filesystem::path &p, std::string_view pin) {
        need(p.is_absolute() && std::filesystem::canonical(p) == p &&
                     !std::filesystem::is_symlink(p) && std::filesystem::is_regular_file(p) &&
                     std::filesystem::hard_link_count(p) == 1,
             "QNX6_SYSTEM_PATH");
        need(pin.size() == 64 && sha256_file(p) == pin, "QNX6_SYSTEM_SHA");
    };

    ordinary(source, source_pin);
    ordinary(stopped, stopped_pin);
    need(source != stopped && !std::filesystem::equivalent(source, stopped), "QNX6_SYSTEM_ALIAS");
    const auto before = cache_file_identity(source), after = cache_file_identity(stopped);
    const auto size = std::filesystem::file_size(source);
    need(size == std::filesystem::file_size(stopped), "QNX6_SYSTEM_EXTENT");
    Reader seed(source);
    const auto header = seed.bytes(512, 92);
    need(header.substr(0, 8) == "EFI PART" && integer(header, 12, 4) == 92 &&
                 integer(header, 72, 8) == 2 && integer(header, 80, 4) == 33 &&
                 integer(header, 84, 4) == 128,
         "QNX6_SYSTEM_GPT");
    const auto entry = seed.bytes(1024, 128);
    const auto first = integer(entry, 32, 8), last = integer(entry, 40, 8);
    need(first == 64 && last >= first && last < size / 512 && last - first <= (4ULL << 30) / 512,
         "QNX6_SYSTEM_PARTITION");
    const auto base = first * 512, extent = (last - first + 1) * 512;
    need(base <= size && extent <= size - base, "QNX6_SYSTEM_PARTITION");
    Reader final(stopped);
    auto same = [&](std::uint64_t begin, std::uint64_t end) {
        while (begin < end) {
            const auto count = std::min<std::uint64_t>(1 << 20, end - begin);
            need(seed.bytes(begin, count) == final.bytes(begin, count),
                 "QNX6_SYSTEM_OUTSIDE_PARTITION");
            begin += count;
        }
    };
    same(0, base);
    same(base + extent, size);
    Reader original(source, base, extent);
    original.open();
    need((original.inode(1).mode & 0170000) == 0040000, "QNX6_ROOT_DIRECTORY");
    Reader fs(stopped, base, extent);
    fs.open();
    need((fs.inode(1).mode & 0170000) == 0040000, "QNX6_ROOT_DIRECTORY");
    auto config = fs.file("etc/clsboot/boot_cfg.xml", true);
    need(cache_file_identity(source) == before && cache_file_identity(stopped) == after &&
                 sha256_file(source) == source_pin && sha256_file(stopped) == stopped_pin,
         "QNX6_SYSTEM_CHANGED");

    return config;
}
} // namespace voyemu
