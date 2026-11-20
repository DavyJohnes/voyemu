#include "qnx_gpt.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_imagefs.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid GPT"};

void put(std::string &bytes, std::size_t at, std::uint64_t value, unsigned width) {
    need(at + width <= bytes.size(), "QNX_GPT_BOUNDS");

    for (unsigned i = 0; i < width; ++i) {
        bytes[at + i] = static_cast<char>(value >> (i * 8));
    }
}

std::uint32_t crc(std::string_view data) {
    std::uint32_t value = ~0U;

    for (unsigned char c : data) {
        value ^= c;

        for (unsigned i = 0; i < 8; ++i) {
            value = (value >> 1) ^ ((value & 1) ? 0xedb88320 : 0);
        }
    }

    return ~value;
}

std::string hex(std::string_view value) {
    std::string bytes;

    for (std::size_t at = 0; at < value.size(); at += 2) {
        auto digit = [](char c) -> unsigned {
            return c <= '9' ? c - '0' : c - 'a' + 10;
        };
        bytes += static_cast<char>(digit(value[at]) * 16 + digit(value[at + 1]));
    }

    return bytes;
}

std::string entry(std::uint64_t first, std::uint64_t last, std::string_view name,
                  std::string_view unique) {
    std::string result(128, 0);

    result.replace(0, 16, hex("084a3e13ef4de611beb89e71128cae77"));
    result.replace(16, 16, hex(unique));
    put(result, 32, first, 8);
    put(result, 40, last, 8);
    need(name.size() <= 36, "QNX_GPT_NAME");

    for (std::size_t at = 0; at < name.size(); ++at) {
        put(result, 56 + at * 2, static_cast<unsigned char>(name[at]), 2);
    }

    return result;
}

std::string header(std::uint64_t mine, std::uint64_t alternate, std::uint64_t last,
                   std::uint64_t entries, unsigned count, std::uint32_t array_crc,
                   std::string_view guid) {
    std::string result(92, 0);

    result.replace(0, 8, "EFI PART");
    put(result, 8, 0x10000, 4);
    put(result, 12, 92, 4);
    put(result, 24, mine, 8);
    put(result, 32, alternate, 8);
    put(result, 40, 34, 8);
    put(result, 48, last, 8);
    result.replace(56, 16, hex(guid));
    put(result, 72, entries, 8);
    put(result, 80, count, 4);
    put(result, 84, 128, 4);
    put(result, 88, array_crc, 4);
    put(result, 16, crc(result), 4);

    return result;
}
} // namespace
namespace {
QnxIfs2Gpt ifs2_gpt(std::uint64_t bytes) {
    need(bytes > 0 && bytes <= 0x0ffbe000, "QNX_IFS2_GPT_EXTENT");
    std::string entries(4096, 0);
    entries.replace(
            0, 128,
            entry(64, 64 + (bytes + 4095) / 4096 - 1, "ifs2", "f1e2a3b4c5d607981a2b3c4d5e6f7081"));
    auto make_header = [&](std::uint64_t mine, std::uint64_t other, std::uint64_t table) {
        auto h = header(mine, other, 65533, table, 32, crc(entries),
                        "3f5b8a1c6a1e4c0d9f8b2e7a5d1c4b09");
        put(h, 40, 64, 8);
        put(h, 16, 0, 4);
        put(h, 16, crc(h), 4);
        h.resize(4096, 0);

        return h;
    };
    const auto primary_header = make_header(1, 65535, 2),
               backup_header = make_header(65535, 1, 65534);
    QnxIfs2Gpt result;
    result.cache = primary_header + entries;
    result.primary.assign(4096, 0);
    result.primary[450] = static_cast<char>(0xee);
    put(result.primary, 454, 1, 4);
    put(result.primary, 458, 65535, 4);
    result.primary[510] = 0x55;
    result.primary[511] = static_cast<char>(0xaa);
    result.primary += result.cache;
    result.backup = entries + backup_header;

    return result;
}
} // namespace

QnxIfs2Gpt qnx_passion_gpt(std::uint64_t bytes) {
    need(bytes > 0 && bytes <= 0x0c7de000, "QNX_PASSION_GPT_EXTENT");

    return ifs2_gpt(bytes);
}

void validate_qnx_passion_gpt(const QnxIfs2Gpt &images, std::uint64_t bytes) {
    const auto expected = qnx_passion_gpt(bytes);

    need(images.cache == expected.cache && images.primary == expected.primary &&
                 images.backup == expected.backup,
         "QNX_PASSION_GPT_METADATA");
}

namespace {
std::uint32_t qinggan11_imagefs_extent(std::string_view image, std::string_view pin) {
    need(image.size() == 0x10000000 && pin.size() == 64 && sha256_bytes(image) == pin,
         "QNX_QINGGAN11_GPT_INPUT_SHA");
    std::uint32_t extent = 0;

    for (unsigned i = 0; i < 4; ++i) {
        extent |= std::uint32_t(static_cast<unsigned char>(image[8 + i])) << (8 * i);
    }

    need(extent == 147839040, "QNX_QINGGAN11_GPT_IMAGEFS_EXTENT");

    return extent;
}
} // namespace
namespace {
std::uint32_t qinggan11_older_imagefs_extent(std::string_view image, std::string_view pin) {
    need(image.size() == 0x10000000 && pin.size() == 64 && sha256_bytes(image) == pin,
         "QNX_QINGGAN11_OLDER_GPT_INPUT_SHA");
    std::uint32_t extent = 0;

    for (unsigned i = 0; i < 4; ++i) {
        extent |= std::uint32_t(static_cast<unsigned char>(image[8 + i])) << (8 * i);
    }

    need(extent == 147830848, "QNX_QINGGAN11_OLDER_GPT_IMAGEFS_EXTENT");

    return extent;
}
} // namespace

QnxIfs2Gpt qnx_qinggan11_older_gpt(std::string_view image, std::string_view pin) {
    return ifs2_gpt(qinggan11_older_imagefs_extent(image, pin));
}

std::string qnx_qinggan11_older_loader_image(std::string_view image, std::string_view pin) {
    const auto extent = qinggan11_older_imagefs_extent(image, pin);
    const std::size_t loaded = (std::size_t(extent) + 4095) / 4096 * 4096;

    need(loaded == 147832832 && loaded <= image.size() && loaded < 0x0ffbe000,
         "QNX_QINGGAN11_OLDER_LOADER_EXTENT");
    need(std::all_of(image.begin() + extent, image.end(),
                     [](unsigned char value) {
                         return value == 0xff;
                     }),
         "QNX_QINGGAN11_OLDER_LOADER_PADDING");

    return std::string(image.substr(0, loaded));
}

void validate_qnx_qinggan11_older_gpt(const QnxIfs2Gpt &images, std::string_view image,
                                      std::string_view pin) {
    const auto expected = qnx_qinggan11_older_gpt(image, pin);

    need(images.cache == expected.cache && images.primary == expected.primary &&
                 images.backup == expected.backup,
         "QNX_QINGGAN11_OLDER_GPT_METADATA");
}

QnxIfs2Gpt qnx_qinggan11_gpt(std::string_view image, std::string_view pin) {
    return ifs2_gpt(qinggan11_imagefs_extent(image, pin));
}

std::string qnx_qinggan11_loader_image(std::string_view image, std::string_view pin) {
    const auto extent = qinggan11_imagefs_extent(image, pin);
    const std::size_t loaded = (std::size_t(extent) + 4095) / 4096 * 4096;

    need(loaded == 147841024 && loaded % 4096 == 0 && loaded <= image.size() && loaded < 0x0ffbe000,
         "QNX_QINGGAN11_LOADER_EXTENT");
    need(std::all_of(image.begin() + extent, image.end(),
                     [](unsigned char value) {
                         return value == 0xff;
                     }),
         "QNX_QINGGAN11_LOADER_PADDING");

    return std::string(image.substr(0, loaded));
}

void validate_qnx_qinggan11_gpt(const QnxIfs2Gpt &images, std::string_view image,
                                std::string_view pin) {
    const auto expected = qnx_qinggan11_gpt(image, pin);

    need(images.cache == expected.cache && images.primary == expected.primary &&
                 images.backup == expected.backup,
         "QNX_QINGGAN11_GPT_METADATA");
}

std::string qnx_synthetic_gpt_for_ifs2(std::uint64_t bytes) {
    need(bytes >= 512 && bytes <= 0x10000000 && bytes % 512 == 0, "QNX_GPT_IFS_EXTENT");
    std::string entries(32 * 128, 0);
    entries.replace(
            0, 128,
            entry(0x40, 0x40 + bytes / 512 - 1, "ifs2", "f1e2a3b4c5d607981a2b3c4d5e6f7081"));
    std::string output(8192, 0);
    output.replace(
            0, 92,
            header(1, 0x200, 0x1ff, 2, 32, crc(entries), "3f5b8a1c6a1e4c0d9f8b2e7a5d1c4b09"));
    output.replace(0x1000, entries.size(), entries);

    return output;
}

std::string qnx_synthetic_gpt() {
    return qnx_synthetic_gpt_for_ifs2(0x937c000);
}

std::filesystem::path prepare_qnx_system_disk(const std::filesystem::path &original,
                                              std::string_view pin,
                                              const std::filesystem::path &destination,
                                              std::uint64_t reserve_bytes) {
    need(reserve_bytes <= (20ULL << 30), "QNX_GPT_RESERVE_BOUND");
    need(original.is_absolute() && !std::filesystem::is_symlink(original) &&
                 std::filesystem::is_regular_file(original) &&
                 std::filesystem::file_size(original) > 0 &&
                 std::filesystem::file_size(original) <= 4ULL * 1024 * 1024 * 1024 &&
                 std::filesystem::file_size(original) % 512 == 0 && sha256_file(original) == pin,
         "QNX_GPT_INPUT_SHA");
    const auto epoch = cache_file_identity(original);
    need(destination.is_absolute() && !std::filesystem::exists(destination) &&
                 !std::filesystem::is_symlink(destination),
         "QNX_GPT_FRESH_OUTPUT");
    need(std::filesystem::space(destination.parent_path()).available >=
                 std::filesystem::file_size(original) + reserve_bytes,
         "QNX_GPT_DISK_RESERVE");
    need(std::filesystem::create_directory(destination), "QNX_GPT_CREATE");
    ensure_private_directory(destination);
    const auto output = destination / "sysla-gpt-ramdisk.img";
    std::ofstream out(output, std::ios::binary);
    std::ifstream in(original, std::ios::binary);
    need(bool(out) && bool(in), "QNX_GPT_OPEN");
    const auto sectors = std::filesystem::file_size(original) / 512, total = sectors + 98,
               alternate = total - 1;
    std::string entries(33 * 128, 0);
    entries.replace(0, 128,
                    entry(64, 64 + sectors - 1, "la_system_a", "b4a3e2f1d6c598071a2b3c4d5e6f7081"));
    const auto array_crc = crc(entries);
    const std::string guid = "1c8a5b3f1e6a0d4c9f8b2e7a5d1c4b09";
    std::string lead(64 * 512, 0), tail(34 * 512, 0);
    lead[450] = static_cast<char>(0xee);
    lead[510] = 0x55;
    lead[511] = static_cast<char>(0xaa);
    lead.replace(512, 92, header(1, alternate, alternate, 2, 33, array_crc, guid));
    lead.replace(1024, entries.size(), entries);
    tail.replace(0, entries.size(), entries);
    tail.replace(tail.size() - 512, 92,
                 header(alternate, 1, alternate, 64 + sectors, 33, array_crc, guid));
    out.write(lead.data(), lead.size());
    std::array<char, 1024 * 1024> buffer{};

    while (in) {
        in.read(buffer.data(), buffer.size());
        out.write(buffer.data(), in.gcount());
    }

    need(in.eof() && bool(out), "QNX_GPT_COPY");
    out.write(tail.data(), tail.size());
    out.close();
    need(bool(out) && cache_file_identity(original) == epoch &&
                 std::filesystem::file_size(output) == total * 512,
         "QNX_GPT_COMPLETE");

    return output;
}

std::vector<QnxLoaderSegment> split_qnx_loader_image(const std::filesystem::path &original,
                                                     std::string_view pin,
                                                     const std::filesystem::path &destination,
                                                     std::uint64_t part_bytes,
                                                     std::uint64_t reserve_bytes) {
    need(reserve_bytes <= (20ULL << 30), "QNX_LOADER_SEGMENT_RESERVE_BOUND");
    need(part_bytes > 0 && part_bytes <= (1ULL << 30), "QNX_LOADER_SEGMENT_BOUND");
    need(original.is_absolute() && std::filesystem::canonical(original) == original &&
                 !std::filesystem::is_symlink(original) &&
                 std::filesystem::is_regular_file(original) &&
                 std::filesystem::file_size(original) > 0 &&
                 std::filesystem::file_size(original) <= 4ULL * 1024 * 1024 * 1024 &&
                 sha256_file(original) == pin,
         "QNX_LOADER_SEGMENT_INPUT_SHA");
    const auto epoch = cache_file_identity(original);
    const std::uint64_t extent = std::filesystem::file_size(original);
    need((extent + part_bytes - 1) / part_bytes <= 16, "QNX_LOADER_SEGMENT_COUNT");
    need(destination.is_absolute() && destination.lexically_normal() == destination &&
                 std::filesystem::canonical(destination.parent_path()) ==
                         destination.parent_path() &&
                 !std::filesystem::exists(destination) && !std::filesystem::is_symlink(destination),
         "QNX_LOADER_SEGMENT_FRESH_OUTPUT");
    need(std::filesystem::space(destination.parent_path()).available >= extent + reserve_bytes,
         "QNX_LOADER_SEGMENT_RESERVE");
    need(std::filesystem::create_directory(destination), "QNX_LOADER_SEGMENT_CREATE");
    ensure_private_directory(destination);
    std::ifstream input(original, std::ios::binary);
    need(bool(input), "QNX_LOADER_SEGMENT_OPEN");
    std::vector<QnxLoaderSegment> result;
    std::array<char, 1024 * 1024> buffer{}, verify{};
    std::uint64_t offset = 0;

    while (offset < extent) {
        const auto count = std::min<std::uint64_t>(part_bytes, extent - offset);
        const auto file = destination / ("part-" + std::to_string(result.size()) + ".img");
        need(!std::filesystem::exists(file), "QNX_LOADER_SEGMENT_EXISTS");
        std::ofstream output(file, std::ios::binary);
        need(bool(output), "QNX_LOADER_SEGMENT_OPEN");
        std::uint64_t copied = 0;

        while (copied < count) {
            const auto n = std::min<std::uint64_t>(buffer.size(), count - copied);
            input.read(buffer.data(), static_cast<std::streamsize>(n));
            need(input.gcount() == static_cast<std::streamsize>(n), "QNX_LOADER_SEGMENT_READ");
            output.write(buffer.data(), static_cast<std::streamsize>(n));
            need(bool(output), "QNX_LOADER_SEGMENT_WRITE");
            copied += n;
        }

        output.close();
        need(bool(output) && std::filesystem::file_size(file) == count,
             "QNX_LOADER_SEGMENT_EXTENT");
        result.push_back({file, offset, count, sha256_file(file)});
        offset += count;
    }

    need(offset == extent && result.size() == (extent + part_bytes - 1) / part_bytes,
         "QNX_LOADER_SEGMENT_COVERAGE");
    input.clear();
    input.seekg(0);

    for (const auto &part : result) {
        std::ifstream checked(part.file, std::ios::binary);
        need(bool(checked), "QNX_LOADER_SEGMENT_OPEN");
        std::uint64_t compared = 0;

        while (compared < part.bytes) {
            const auto n = std::min<std::uint64_t>(buffer.size(), part.bytes - compared);
            input.read(buffer.data(), static_cast<std::streamsize>(n));
            checked.read(verify.data(), static_cast<std::streamsize>(n));
            need(input.gcount() == static_cast<std::streamsize>(n) &&
                         checked.gcount() == static_cast<std::streamsize>(n) &&
                         std::equal(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(n),
                                    verify.begin()),
                 "QNX_LOADER_SEGMENT_READBACK");
            compared += n;
        }

        need(sha256_file(part.file) == part.sha256, "QNX_LOADER_SEGMENT_CHANGED");
    }

    need(cache_file_identity(original) == epoch && sha256_file(original) == pin,
         "QNX_LOADER_SEGMENT_SOURCE_CHANGED");

    return result;
}
} // namespace voyemu
