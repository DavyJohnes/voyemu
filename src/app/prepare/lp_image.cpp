#include "lp_image.hpp"
#include "check.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>
namespace voyemu {
namespace {
constexpr Check need{"LP_IMAGE"};

std::uint64_t number(std::string_view s, std::size_t at, unsigned count) {
    need(count <= 8 && at <= s.size() && count <= s.size() - at, "field outside metadata");
    std::uint64_t v = 0;

    for (unsigned i = 0; i < count; i++) {
        v |= std::uint64_t(static_cast<unsigned char>(s[at + i])) << (i * 8);
    }

    return v;
}

void put(std::string &s, std::size_t at, std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) {
        s.at(at + i) = static_cast<char>(value >> (i * 8));
    }
}

std::uint32_t crc(std::string_view s) {
    std::uint32_t v = 0xffffffff;

    for (unsigned char c : s) {
        v ^= c;

        for (unsigned i = 0; i < 8; i++) {
            v = (v >> 1) ^ (0xedb88320 & std::uint32_t(-std::int32_t(v & 1)));
        }
    }

    return v ^ 0xffffffff;
}

std::string hex(std::string_view s) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;

    for (unsigned char c : s) {
        out += digits[c >> 4];
        out += digits[c & 15];
    }

    return out;
}

std::string name(std::string_view s) {
    auto n = s.find('\0');

    need(n != std::string_view::npos && n > 0, "bounded name required");

    for (std::size_t i = 0; i < n; i++) {
        unsigned char c = s[i];
        need((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_',
             "unsupported name");
    }

    for (std::size_t i = n; i < s.size(); i++) {
        need(s[i] == 0, "name padding differs");
    }

    return std::string(s.substr(0, n));
}

struct Reader {
    std::ifstream file;
    std::uint64_t bytes;

    explicit Reader(const std::filesystem::path &p)
        : file(p, std::ios::binary), bytes(std::filesystem::file_size(p)) {
        need(bool(file) && bytes >= 4096 && bytes <= 16ULL << 30, "bounded input required");
    }

    std::string read(std::uint64_t at, std::size_t n) {
        need(n <= 1ULL << 20 && at <= bytes && n <= bytes - at, "read outside image");
        std::string s(n, '\0');
        file.seekg(static_cast<std::streamoff>(at));
        file.read(s.data(), static_cast<std::streamsize>(n));
        need(bool(file), "short metadata read");

        return s;
    }
};

struct GptSuper {
    std::string primary, entries;
    std::uint64_t offset = 0, bytes = 0, entry = 0;
    bool terminal = false;
};

GptSuper super_partition(Reader &r) {
    auto h = r.read(512, 512);

    need(h.substr(0, 8) == "EFI PART" && number(h, 8, 4) == 0x10000 && number(h, 12, 4) >= 92 &&
                 number(h, 12, 4) <= 512 && number(h, 24, 8) == 1,
         "primary GPT required");
    auto checked = h.substr(0, number(h, 12, 4));
    std::fill(checked.begin() + 16, checked.begin() + 20, 0);
    need(crc(checked) == number(h, 16, 4), "GPT header CRC mismatch");
    auto lba = number(h, 72, 8), count = number(h, 80, 4), width = number(h, 84, 4),
         first = number(h, 40, 8), last = number(h, 48, 8);
    need(count && count <= 256 && width >= 128 && width <= 1024 && width % 8 == 0 &&
                 lba <= r.bytes / 512 && count * width <= r.bytes - lba * 512 && first <= last &&
                 last < r.bytes / 512 && number(h, 32, 8) < r.bytes / 512,
         "GPT bounds invalid");
    auto table = r.read(lba * 512, count * width);
    need(crc(table) == number(h, 88, 4), "GPT table CRC mismatch");
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    GptSuper result;

    for (std::uint64_t i = 0; i < count; i++) {
        auto e = std::string_view(table).substr(i * width, width);

        if (e.substr(0, 16) == std::string(16, 0)) {
            continue;
        }

        auto a = number(e, 32, 8), b = number(e, 40, 8);
        need(a >= first && b <= last && a <= b, "GPT partition outside usable range");
        ranges.emplace_back(a, b);
        std::string label;
        bool ended = false;

        for (unsigned j = 0; j < 36; j++) {
            auto c = number(e, 56 + j * 2, 2);

            if (!c) {
                ended = true;
            } else {
                need(!ended && c >= 32 && c < 127, "unsupported GPT name");
                label += static_cast<char>(c);
            }
        }

        if (label == "super") {
            need(!result.bytes, "duplicate super partition");
            result.offset = a * 512;
            result.bytes = (b - a + 1) * 512;
            result.entry = i * width;
        }
    }

    std::sort(ranges.begin(), ranges.end());

    for (std::size_t i = 1; i < ranges.size(); i++) {
        need(ranges[i].first > ranges[i - 1].second, "GPT partitions overlap");
    }

    need(result.bytes >= 16384, "super partition missing");
    result.terminal = ranges.back().first * 512 == result.offset;
    result.primary = std::move(h);
    result.entries = std::move(table);

    return result;
}

struct GptBackup {
    std::uint64_t entries_lba = 0, header_lba = 0;
    std::string protective_mbr, header;
};

GptBackup read_gpt_backup(Reader &r, const GptSuper &super) {
    const auto &primary = super.primary;
    const auto last = number(primary, 32, 8), first_usable = number(primary, 40, 8);
    const auto primary_entries = number(primary, 72, 8);
    const auto entry_sectors = (super.entries.size() + 511) / 512;

    need(r.bytes % 512 == 0 && last == r.bytes / 512 - 1 && last <= UINT32_MAX &&
                 primary_entries >= 2 && primary_entries <= first_usable &&
                 entry_sectors <= first_usable - primary_entries && entry_sectors < last &&
                 number(primary, 48, 8) == last - entry_sectors - 1,
         "GPT growth requires standard backup placement");
    auto header = r.read(last * 512, 512);
    const auto entries = last - entry_sectors;
    need(header.substr(0, 16) == primary.substr(0, 16) && number(header, 24, 8) == last &&
                 number(header, 32, 8) == 1 && header.substr(40, 32) == primary.substr(40, 32) &&
                 number(header, 72, 8) == entries &&
                 header.substr(80, 12) == primary.substr(80, 12),
         "GPT backup header differs");
    auto checked = header.substr(0, number(header, 12, 4));
    std::fill(checked.begin() + 16, checked.begin() + 20, 0);
    need(crc(checked) == number(header, 16, 4) &&
                 r.read(entries * 512, super.entries.size()) == super.entries,
         "GPT backup checksums or entries differ");
    auto mbr = r.read(0, 512);
    need(number(mbr, 450, 1) == 0xee && number(mbr, 454, 4) == 1 && number(mbr, 458, 4) == last &&
                 mbr.substr(462, 48) == std::string(48, 0) && number(mbr, 510, 2) == 0xaa55,
         "protective MBR differs");

    return {entries, last, std::move(mbr), std::move(header)};
}

void require_zero(Reader &r, std::uint64_t offset, std::uint64_t bytes) {
    for (std::uint64_t at = 0; at < bytes;) {
        const auto count = std::min<std::uint64_t>(1ULL << 20, bytes - at);
        const auto unused = r.read(offset + at, count);
        need(std::all_of(unused.begin(), unused.end(),
                         [](char c) {
                             return c == 0;
                         }),
             "fresh scratch tail must be zero");
        at += count;
    }
}

void write_metadata(std::fstream &file, std::uint64_t offset, std::string_view metadata) {
    file.seekp(offset);
    file.write(metadata.data(), metadata.size());
    need(bool(file), "scratch metadata write failed");
}

void expand_super(std::fstream &file, GptSuper disk, GptBackup backup, std::uint64_t bytes) {
    const auto sectors = bytes / 512;
    const auto entries_lba = backup.entries_lba + sectors, header_lba = backup.header_lba + sectors;
    const auto primary_entries = number(disk.primary, 72, 8);

    put(backup.protective_mbr, 458, header_lba, 4);
    put(disk.entries, disk.entry + 40, number(disk.entries, disk.entry + 40, 8) + sectors, 8);
    const auto table_crc = crc(disk.entries);

    for (auto *header : {&disk.primary, &backup.header}) {
        put(*header, 48, entries_lba - 1, 8);
        put(*header, 88, table_crc, 4);
    }

    put(disk.primary, 32, header_lba, 8);
    put(backup.header, 24, header_lba, 8);
    put(backup.header, 72, entries_lba, 8);

    for (auto *header : {&disk.primary, &backup.header}) {
        put(*header, 16, 0, 4);
        put(*header, 16, crc(std::string_view(*header).substr(0, number(*header, 12, 4))), 4);
    }

    const auto old_backup_bytes = (backup.header_lba - backup.entries_lba + 1) * 512;
    write_metadata(file, backup.entries_lba * 512, std::string(old_backup_bytes, 0));
    write_metadata(file, 0, backup.protective_mbr);
    write_metadata(file, 512, disk.primary);
    write_metadata(file, primary_entries * 512, disk.entries);
    write_metadata(file, entries_lba * 512, disk.entries);
    write_metadata(file, header_lba * 512, backup.header);
}
} // namespace

LpPartitionView read_lp_partition(const std::filesystem::path &image, const std::string &pin,
                                  std::string_view selected) {
    need(image.is_absolute() &&
                 std::filesystem::is_regular_file(std::filesystem::symlink_status(image)) &&
                 std::filesystem::hard_link_count(image) == 1,
         "ordinary image required");
    need(sha256_text(pin), "SHA256 required");
    need(!selected.empty() && selected.size() < 36 &&
                 selected.find_first_not_of(
                         "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_") ==
                         std::string_view::npos,
         "logical partition name unsupported");

    need(!os::linked_ancestor(image), "linked image ancestor");

    auto epoch = cache_file_identity(image);
    need(sha256_file(image) == pin, "image SHA mismatch");
    Reader reader(image);
    const auto partition = super_partition(reader);
    const auto base = partition.offset, length = partition.bytes;
    auto read = [&](std::uint64_t at, std::size_t n) {
        need(at <= length && n <= length - at, "read outside super");

        return reader.read(base + at, n);
    };
    auto g = read(4096, 52), backup = read(8192, 52);
    need(g == backup, "geometry copies disagree");
    need(number(g, 0, 4) == 0x616c4467 && number(g, 4, 4) == 52, "geometry version unsupported");
    auto checked = g;
    std::fill(checked.begin() + 8, checked.begin() + 40, 0);
    need(sha256_bytes(checked) == hex(std::string_view(g).substr(8, 32)), "geometry SHA mismatch");
    auto maximum = number(g, 40, 4), slots = number(g, 44, 4), block = number(g, 48, 4);
    need(maximum >= 512 && maximum <= 1ULL << 20 && maximum % 512 == 0 && slots >= 1 &&
                 slots <= 4 && block == 4096,
         "geometry bounds unsupported");
    auto metadata_end = 12288 + 2 * maximum * slots;
    need(metadata_end <= length, "metadata outside super");
    std::string canonical;

    for (std::uint64_t i = 0; i < slots * 2; i++) {
        auto m = read(12288 + i * maximum, maximum);
        need(number(m, 0, 4) == 0x414c5030 && number(m, 4, 2) == 10 && number(m, 6, 2) == 0 &&
                     number(m, 8, 4) == 128,
             "LP version unsupported");
        auto tables = number(m, 44, 4);
        need(tables <= maximum - 128, "tables outside metadata");
        auto h = m.substr(0, 128);
        std::fill(h.begin() + 12, h.begin() + 44, 0);
        need(sha256_bytes(h) == hex(std::string_view(m).substr(12, 32)) &&
                     sha256_bytes(std::string_view(m).substr(128, tables)) ==
                             hex(std::string_view(m).substr(48, 32)),
             "metadata SHA mismatch");
        m.resize(128 + tables);

        if (canonical.empty()) {
            canonical = std::move(m);
        } else {
            need(m == canonical, "metadata copies disagree");
        }
    }

    struct Table {
        std::uint64_t offset, count, width;
    };
    std::array<Table, 4> tables;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> coverage;
    constexpr unsigned widths[] = {52, 24, 48, 64};

    for (unsigned i = 0; i < 4; i++) {
        auto at = 80 + i * 12;
        auto &t = tables[i];
        t = {number(canonical, at, 4), number(canonical, at + 4, 4), number(canonical, at + 8, 4)};
        need(t.count && t.count <= 4096 && t.width == widths[i] &&
                     t.offset <= canonical.size() - 128 &&
                     t.count * t.width <= canonical.size() - 128 - t.offset,
             "table descriptor invalid");
        coverage.emplace_back(t.offset, t.offset + t.count * t.width);
    }

    std::sort(coverage.begin(), coverage.end());
    std::uint64_t end = 0;

    for (auto range : coverage) {
        need(range.first == end, "table gaps or overlap");
        end = range.second;
    }

    need(end == canonical.size() - 128, "table coverage differs");
    auto entry = [&](unsigned table, std::uint64_t index) {
        auto t = tables[table];
        need(index < t.count, "table index outside bounds");

        return std::string_view(canonical).substr(128 + t.offset + index * t.width, t.width);
    };
    need(tables[3].count == 1, "multiple block devices unsupported");
    auto device = entry(3, 0);
    need(name(device.substr(24, 36)) == "super" && number(device, 16, 8) == length &&
                 number(device, 60, 4) == 0,
         "block device identity differs");
    auto first_sector = number(device, 0, 8);
    need(first_sector <= length / 512 && first_sector * 512 >= metadata_end,
         "data overlaps metadata");
    std::set<std::string> groups, partitions;

    for (std::uint64_t i = 0; i < tables[2].count; i++) {
        auto e = entry(2, i);
        need(groups.insert(name(e.substr(0, 36))).second && number(e, 36, 4) == 0, "group invalid");
    }

    LpPartitionView result;
    std::set<std::uint64_t> used_extents;
    std::map<std::uint64_t, std::uint64_t> group_bytes;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> physical;

    for (std::uint64_t i = 0; i < tables[0].count; i++) {
        auto p = entry(0, i);
        auto label = name(p.substr(0, 36));
        need(partitions.insert(label).second && number(p, 36, 4) <= 1,
             "partition name or attributes unsupported");
        auto group = number(p, 48, 4);
        need(group < tables[2].count && number(p, 44, 4) == 1,
             "partition extent layout unsupported");
        auto extent_index = number(p, 40, 4);
        need(used_extents.insert(extent_index).second, "shared extent index unsupported");
        auto e = entry(1, extent_index);
        auto sectors = number(e, 0, 8), target = number(e, 12, 8);
        need(number(e, 8, 4) == 0 && number(e, 20, 4) == 0,
             "non-LINEAR or foreign device unsupported");
        need(sectors && target >= first_sector && target <= length / 512 &&
                     sectors <= length / 512 - target,
             "extent outside super");
        need(target * 512 % block == 0 && sectors * 512 % block == 0,
             "extent alignment unsupported");
        group_bytes[group] += sectors * 512;
        auto quota = number(entry(2, group), 40, 8);
        need(!quota || sectors * 512 <= quota, "partition exceeds group limit");
        physical.emplace_back(target, target + sectors);

        if (label == selected) {
            result = {base + target * 512,    sectors * 512, base, length, label,
                      sha256_bytes(canonical)};
        }
    }

    need(used_extents.size() == tables[1].count, "unowned extents unsupported");

    for (const auto &[index, bytes] : group_bytes) {
        auto quota = number(entry(2, index), 40, 8);
        need(!quota || bytes <= quota, "group size exceeded");
    }

    std::sort(physical.begin(), physical.end());

    for (std::size_t i = 1; i < physical.size(); i++) {
        need(physical[i].first >= physical[i - 1].second, "partition extents overlap");
    }

    need(result.bytes, "logical partition missing");
    need(cache_file_identity(image) == epoch && sha256_file(image) == pin,
         "image changed during metadata read");

    return result;
}

std::uint64_t lp_scratch_growth_bytes(std::uint64_t deficit, std::uint64_t alignment,
                                      std::uint64_t disk_bytes, std::uint64_t limit) {
    need(alignment >= 4096 && alignment <= 1ULL << 20 && (alignment & (alignment - 1)) == 0 &&
                 limit <= lp_scratch_max_growth && deficit <= limit,
         "bounded scratch super growth required");
    const auto growth = (deficit + alignment - 1) / alignment * alignment;
    need(growth <= limit && disk_bytes <= (4ULL << 30) - growth,
         "bounded scratch super growth required");

    return growth;
}

Json seed_lp_overlay_scratch(const std::filesystem::path &image, const std::string &pin,
                             std::uint64_t bytes, LpScratchPlacement placement,
                             std::uint64_t growth_limit) {
    need(bytes && bytes <= 2ULL << 30 && bytes % 4096 == 0,
         "bounded aligned scratch size required");
    const auto system = read_lp_partition(image, pin, "system");
    const auto epoch = cache_file_identity(image);
    Reader r(image);
    const auto base = system.super_offset, length = system.super_bytes;
    const auto geometry = r.read(base + 4096, 52);
    const auto maximum = number(geometry, 40, 4), slots = number(geometry, 44, 4);
    const auto old = r.read(base + 12288, maximum);
    const auto table_size = number(old, 44, 4);
    std::array<std::string, 4> tables;
    std::array<std::uint64_t, 4> counts;
    constexpr unsigned widths[] = {52, 24, 48, 64};

    for (unsigned i = 0; i < 4; i++) {
        counts[i] = number(old, 84 + i * 12, 4);
        tables[i] = old.substr(128 + number(old, 80 + i * 12, 4), counts[i] * widths[i]);
    }

    need(table_size + 76 <= maximum - 128 && counts[0] < 4096 && counts[1] < 4096,
         "scratch metadata table space required");
    std::uint64_t end = number(tables[3], 0, 8) * 512, group = UINT64_MAX;
    Json original = Json::array();

    for (std::uint64_t i = 0; i < counts[2]; i++) {
        auto row = std::string_view(tables[2]).substr(i * 48, 48);

        if (name(row.substr(0, 36)) == "default" && number(row, 40, 8) == 0) {
            group = i;
        }
    }

    need(group != UINT64_MAX, "unlimited default scratch group required");

    for (std::uint64_t i = 0; i < counts[0]; i++) {
        const auto row = std::string_view(tables[0]).substr(i * 52, 52);
        const auto label = name(row.substr(0, 36));
        need(label != "scratch", "scratch must be fresh");
        const auto e = std::string_view(tables[1]).substr(number(row, 40, 4) * 24, 24);
        const auto start = number(e, 12, 8) * 512, size = number(e, 0, 8) * 512;
        end = std::max(end, start + size);
        original.push_back({{"name", label},
                            {"offset", base + start},
                            {"bytes", size},
                            {"sha256", sha256_file_range(image, base + start, size)}});
    }

    const auto device_alignment = number(tables[3], 8, 4),
               alignment = device_alignment ? device_alignment : 4096;
    need(alignment >= 4096 && alignment <= 1ULL << 20 && (alignment & (alignment - 1)) == 0 &&
                 number(tables[3], 12, 4) == 0,
         "scratch device alignment unsupported");
    end = (end + alignment - 1) / alignment * alignment;
    std::uint64_t growth = 0, preceding_offset = 0, preceding_bytes = 0;
    std::string preceding_sha;
    GptSuper disk;
    GptBackup backup;

    if (end + bytes > length) {
        need(placement == LpScratchPlacement::grow_terminal_super,
             "contiguous scratch would exceed super");
        growth = lp_scratch_growth_bytes(end + bytes - length, alignment, r.bytes, growth_limit);
        disk = super_partition(r);
        need(disk.terminal, "scratch growth requires terminal super");
        backup = read_gpt_backup(r, disk);
        require_zero(r, base + length, backup.entries_lba * 512 - (base + length));
        preceding_offset = number(disk.primary, 40, 8) * 512;
        preceding_bytes = base - preceding_offset;
        preceding_sha = sha256_file_range(image, preceding_offset, preceding_bytes);
        put(tables[3], 16, length + growth, 8);
    }

    if (end < length) {
        require_zero(r, base + end, std::min(bytes, length - end));
    }

    auto digest = [](std::string_view data) {
        const auto value = sha256_bytes(data);
        std::string raw;

        for (std::size_t i = 0; i < value.size(); i += 2) {
            raw += static_cast<char>(std::stoul(value.substr(i, 2), nullptr, 16));
        }

        return raw;
    };
    std::string partition(52, 0);
    partition.replace(0, 7, "scratch");
    put(partition, 40, counts[1], 4);
    put(partition, 44, 1, 4);
    put(partition, 48, group, 4);
    tables[0] += partition;
    std::string extent(24, 0);
    put(extent, 0, bytes / 512, 8);
    put(extent, 12, end / 512, 8);
    tables[1] += extent;
    ++counts[0];
    ++counts[1];
    auto metadata = old.substr(0, 128);
    std::uint64_t offset = 0;

    for (unsigned i = 0; i < 4; i++) {
        put(metadata, 80 + i * 12, offset, 4);
        put(metadata, 84 + i * 12, counts[i], 4);
        metadata += tables[i];
        offset += tables[i].size();
    }

    put(metadata, 44, offset, 4);
    metadata.replace(48, 32, digest(std::string_view(metadata).substr(128)));
    std::fill(metadata.begin() + 12, metadata.begin() + 44, 0);
    metadata.replace(12, 32, digest(std::string_view(metadata).substr(0, 128)));
    const auto metadata_sha = sha256_bytes(metadata);
    metadata.resize(maximum, 0);
    need(cache_file_identity(image) == epoch && sha256_file(image) == pin,
         "scratch source changed before write");
    r.file.close();

    if (growth) {
        std::filesystem::resize_file(image, r.bytes + growth);
    }

    std::fstream output(image, std::ios::binary | std::ios::in | std::ios::out);
    need(bool(output), "owned scratch image open failed");

    if (growth) {
        expand_super(output, std::move(disk), std::move(backup), growth);
    }

    for (std::uint64_t i = 0; i < slots * 2; i++) {
        write_metadata(output, base + 12288 + i * maximum, metadata);
    }

    output.flush();
    need(bool(output), "scratch metadata flush failed");
    output.close();
    const auto after = sha256_file(image);
    const auto scratch = read_lp_partition(image, after, "scratch");
    need(scratch.super_bytes == length + growth && scratch.offset == base + end &&
                 scratch.bytes == bytes,
         "scratch view differs after validation");

    if (growth) {
        Reader expanded(image);
        read_gpt_backup(expanded, super_partition(expanded));
        const auto new_scratch = std::max(base + length, scratch.offset);
        require_zero(expanded, new_scratch, scratch.offset + scratch.bytes - new_scratch);
        need(sha256_file_range(image, preceding_offset, preceding_bytes) == preceding_sha,
             "preceding GPT partition payload changed");
    }

    for (const auto &item : original) {
        need(sha256_file_range(image, item.at("offset").get<std::uint64_t>(),
                               item.at("bytes").get<std::uint64_t>()) ==
                     item.at("sha256").get<std::string>(),
             "existing partition payload changed");
    }

    return {{"schema", 1},
            {"kind", "fresh-lp-overlay-scratch"},
            {"source_image_sha256", pin},
            {"image_sha256", after},
            {"scratch_bytes", bytes},
            {"scratch_offset", scratch.offset},
            {"metadata_sha256", metadata_sha},
            {"metadata_copies", slots * 2},
            {"original_partitions", original},
            {"payloads_unchanged", true},
            {"scratch_formatted", false},
            {"image_grown", growth != 0},
            {"super_growth_bytes", growth}};
}
} // namespace voyemu
