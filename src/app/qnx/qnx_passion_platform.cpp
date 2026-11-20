#include "qnx_passion_platform.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_passion.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace voyemu {
namespace {
constexpr CodeCheck need{"Passion platform input differs"};

std::uint32_t be(std::string_view s, std::size_t n) {
    need(n <= s.size() && s.size() - n >= 4, "PASSION_B_FDT_BOUNDS");
    std::uint32_t v = 0;

    for (unsigned i = 0; i < 4; ++i) {
        v = (v << 8) | static_cast<unsigned char>(s[n + i]);
    }

    return v;
}

void put(std::string &s, std::size_t at, std::uint64_t v, unsigned n, bool big = false) {
    need(at <= s.size() && s.size() - at >= n, "PASSION_B_WRITE_BOUNDS");

    for (unsigned i = 0; i < n; ++i) {
        s[at + i] = static_cast<char>(v >> (8 * (big ? n - 1 - i : i)));
    }
}

void append(std::string &s, std::uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; ++i) {
        s += static_cast<char>(v >> (8 * (n - 1 - i)));
    }
}

std::size_t reserved_end(std::string_view fdt) {
    constexpr std::array<std::uint32_t, 10> header{0xd00dfeed, 0x50f2, 0x38, 0x4694, 0x28,
                                                   0x11,       0x10,   0,    0x676,  0x465c};

    need(fdt.size() == header[1], "PASSION_B_FDT_SIZE");

    for (unsigned i = 0; i < header.size(); ++i) {
        need(be(fdt, std::size_t{4} * i) == header[i], "PASSION_B_FDT_HEADER");
    }

    need(fdt.substr(0x4d0a).find_first_not_of('\0') == std::string_view::npos,
         "PASSION_B_FDT_PADDING");
    std::vector<std::string_view> stack;
    std::size_t at = 0x38, selected = 0;
    bool ended = false;

    while (at < 0x4694) {
        const auto token = be(fdt, at);
        at += 4;

        if (token == 1) {
            const auto zero = fdt.find('\0', at);
            need(zero != std::string_view::npos && zero < 0x4694, "PASSION_B_FDT_NAME");
            const auto name = fdt.substr(at, zero - at);
            need(!name.starts_with("voyemu-"), "PASSION_B_FDT_REAPPLY");
            stack.push_back(name);
            at = (zero + 4) & ~std::size_t(3);
        } else if (token == 2) {
            need(!stack.empty(), "PASSION_B_FDT_STACK");

            if (stack.size() == 2 && stack[0].empty() && stack[1] == "reserved-memory") {
                need(!selected, "PASSION_B_FDT_DUPLICATE");
                selected = at - 4;
            }

            stack.pop_back();
        } else if (token == 3) {
            const auto n = be(fdt, at), name = be(fdt, at + 4);
            at += 8;
            need(name < 0x676 && fdt.find('\0', 0x4694 + name) < 0x4d0a && n <= 0x4694 - at,
                 "PASSION_B_FDT_PROPERTY");
            at = (at + n + 3) & ~std::size_t(3);
        } else if (token == 4) {
        } else if (token == 9) {
            need(stack.empty() && at == 0x4694, "PASSION_B_FDT_END");
            ended = true;
            break;
        } else {
            need(false, "PASSION_B_FDT_TOKEN");
        }
    }

    need(ended && selected, "PASSION_B_FDT_RESERVED_ROOT");

    return selected;
}

struct Region {
    const char *node, *name;
    std::uint64_t base, size;
};
// One-character node names keep the inserted block within the 1000-byte FDT slack; the asinfo
// labels (full names) are what the guest publishes into the syspage.
// varimg holds the loader-delivered RW /var QNX6 image (32 MiB payload, 34 MiB reserved) inside
// the free FDT stretch 0xac7de000..0xb0000000 (OEM ifs2 ends at 0xac7de000, gvm_sysram2 starts
// at 0xb0000000); the asinfo label keeps the window out of the guest sysram pool.
constexpr std::array regions{Region{"b", "bootguard", 0xaffc0000, 0x40000},
                             Region{"h", "hostpacked", 0xe4d00000, 0x200000},
                             Region{"e", "earlyl2", 0xe4fa0000, 4096},
                             Region{"c", "gptcache", 0xe4fb0000, 8192},
                             Region{"p", "gptprimary", 0xe4fc0000, 12288},
                             Region{"i", "ifsb", 0xe5000000, 0x0c7de000},
                             Region{"v", "varimg", 0xac800000, 0x2200000},
                             Region{"g", "gptbackup", 0xf4fbe000, 8192},
                             Region{"s", "system", 0x27c000000ULL, 2684404736ULL}};

std::string hex(std::uint64_t n) {
    std::string s;

    do {
        s.insert(s.begin(), "0123456789abcdef"[n & 15]);
        n >>= 4;
    } while (n);

    return s;
}
} // namespace

QnxPassionPlacement qnx_passion_11713_placement() {
    QnxPassionPlacement p;

    p.ifs_b = 0xe5000000;
    p.system = 0x27c000000ULL;
    p.gpt_prefix = 0xe4fc0000;
    p.gpt_backup = 0xf4fbe000;
    p.gpt_cache = 0xe4fb0000;
    p.ipl_map_end = 0x31c100000ULL;
    need(p.system + p.system_sectors * 512 == 0x31c00c400ULL &&
                 p.system + p.system_sectors * 512 <= p.ipl_map_end &&
                 p.gpt_cache + 8192 <= p.gpt_prefix && p.gpt_prefix + 12288 <= p.ifs_b &&
                 p.ifs_b + 200933376ULL <= 0xf1000000ULL && 0xf1020000ULL <= p.gpt_backup,
         "PASSION_B_PLACEMENT");

    return p;
}

void validate_qnx_passion_original_fdt_layout(std::string_view fdt) {
    (void)reserved_end(fdt);
}

std::string qnx_passion_11713_fdt(std::string_view fdt) {
    const auto insert = reserved_end(fdt);
    const auto strings = fdt.substr(0x4694, 0x676);
    std::size_t reg = std::string_view::npos, asinfo = std::string_view::npos;

    for (std::size_t at = 0; at < strings.size();) {
        const auto end = strings.find('\0', at);
        need(end != std::string_view::npos, "PASSION_B_FDT_STRINGS");

        if (strings.substr(at, end - at) == "reg") {
            need(reg == std::string_view::npos, "PASSION_B_FDT_REG_DUPLICATE");
            reg = at;
        }

        if (strings.substr(at, end - at) == "asinfo") {
            need(asinfo == std::string_view::npos, "PASSION_B_FDT_ASINFO_DUPLICATE");
            asinfo = at;
        }

        at = end + 1;
    }

    need(reg != std::string_view::npos && asinfo != std::string_view::npos,
         "PASSION_B_FDT_REG_ASINFO");
    std::string extra;

    for (unsigned i = 0; i < regions.size(); ++i) {
        const auto &r = regions[i];
        need(r.base >= 0x40000000 && r.size && r.base <= 0x31c100000ULL &&
                     r.size <= 0x31c100000ULL - r.base,
             "PASSION_B_BACKING");

        for (unsigned j = i + 1; j < regions.size(); ++j) {
            need(r.base + r.size <= regions[j].base || regions[j].base + regions[j].size <= r.base,
                 "PASSION_B_OVERLAP");
        }

        append(extra, 1, 4);
        extra += r.node;
        extra += '@';
        extra += hex(r.base);
        extra += '\0';
        extra.resize((extra.size() + 3) & ~std::size_t(3), 0);
        append(extra, 3, 4);
        append(extra, 16, 4);
        append(extra, reg, 4);
        append(extra, r.base, 8);
        append(extra, r.size, 8);
        append(extra, 3, 4);
        const std::string_view label = r.name;
        append(extra, label.size() + 1, 4);
        append(extra, asinfo, 4);
        extra += label;
        extra += char(0);
        extra.resize((extra.size() + 3) & ~std::size_t(3), 0);
        append(extra, 2, 4);
    }

    need(extra.size() == 632, "PASSION_B_FDT_BASE_NODES");

    // One initialized memory-backed GPT volume has two OEM cache-name views (LUN0/5).
    // These aliases describe actual bytes; they do not create two physical UFS devices.
    for (const auto lun : {0, 5}) {
        for (const bool header : {true, false}) {
            const auto name = std::string("active_lun") + std::to_string(lun) +
                              (header ? "_header" : "_array");
            const std::uint64_t base = header ? 0xe4fb0000ULL : 0xe4fb1000ULL,
                                size = header ? 92 : 4096;
            need(base >= 0x40000000ULL && base + size <= 0x31c100000ULL,
                 "PASSION_B_CACHE_RAM_CONTAINMENT");
            append(extra, 1, 4);
            extra += "c" + std::to_string(lun) + (header ? "h" : "a") + "@" + hex(base);
            extra += char(0);
            extra.resize((extra.size() + 3) & ~std::size_t(3), 0);
            append(extra, 3, 4);
            append(extra, 16, 4);
            append(extra, reg, 4);
            append(extra, base, 8);
            append(extra, size, 8);
            append(extra, 3, 4);
            append(extra, name.size() + 1, 4);
            append(extra, asinfo, 4);
            extra += name;
            extra += char(0);
            extra.resize((extra.size() + 3) & ~std::size_t(3), 0);
            append(extra, 2, 4);
        }
    }

    need(extra.size() == 968 && extra.size() <= 1000, "PASSION_B_FDT_CAPACITY");
    std::string out(fdt.substr(0, insert));
    out += extra;
    out += fdt.substr(insert, 0x4d0a - insert);
    out.resize(fdt.size(), 0);
    put(out, 12, 0x4694 + extra.size(), 4, true);
    put(out, 36, 0x465c + extra.size(), 4, true);
    need(out.substr(40, insert - 40) == fdt.substr(40, insert - 40) &&
                 out.substr(insert + extra.size(), 0x4d0a - insert) ==
                         fdt.substr(insert, 0x4d0a - insert),
         "PASSION_B_FDT_ORIGINAL_PRESERVATION");

    return out;
}

std::string qnx_passion_11713_ifs2_early_map(std::string_view prior) {
    need(prior.size() == 4096, "PASSION_B_EARLY_TABLE_PREIMAGE");
    constexpr std::uint64_t base = 0xe5000000, loaded = 200929280, leaf = 0x200000,
                            config = 0xf1000000, ram_end = 0x31c100000ULL;
    constexpr unsigned first = 296, count = 96;
    static_assert(base + loaded <= config && base + count * leaf == config && config <= ram_end &&
                  first + count <= 512);
    need(prior.substr(static_cast<std::size_t>(first) * 8, 8) ==
                 std::string_view("\x1d\x04\x00\xe5\0\0\0\0", 8),
         "PASSION_B_EARLY_FIRST_LEAF");

    for (unsigned i = 0; i < 512; ++i) {
        if (i != first) {
            need(prior.substr(static_cast<std::size_t>(i) * 8, 8) == std::string(8, 0),
                 "PASSION_B_EARLY_OTHER_ZERO");
        }
    }

    auto out = std::string(prior);

    for (unsigned i = 0; i < count; ++i) {
        put(out, static_cast<std::size_t>(first + i) * 8, (base + i * leaf) | 0x41d, 8);
    }

    need(out.size() == prior.size() &&
                 out.substr(0, static_cast<std::size_t>(first) * 8) ==
                         prior.substr(0, static_cast<std::size_t>(first) * 8) &&
                 out.substr(static_cast<std::size_t>(first + count) * 8) ==
                         prior.substr(static_cast<std::size_t>(first + count) * 8),
         "PASSION_B_EARLY_TABLE_PARITY");

    return out;
}

QnxPassion11713Bootstrap prepare_qnx_passion_11713_bootstrap(std::string_view original) {
    // Existing function authenticates complete original image/model and all earlier source guards.
    QnxPassion11713Bootstrap result{prepare_qnx_passion_startup(original), std::string(4096, 0)};
    const auto fdt = qnx_passion_11713_fdt(original.substr(205196, 20722));

    result.startup.replace(205196 - 0x3000, fdt.size(), fdt);
    // Only metadata call arguments change. X20/X21 and the later private allocator call remain
    // intact.
    constexpr std::array<std::array<std::uint32_t, 3>, 4> sites{{{0xe060, 0xf00000e4, 0xd29fffe1},
                                                                 {0xe068, 0xd10006a1, 0xf2a381e1},
                                                                 {0xe078, 0x8b140021, 0xf2c00061},
                                                                 {0xe07c, 0xaa1403e0, 0xd2a80000}}};

    for (const auto &x : sites) {
        std::uint32_t before = 0;

        for (unsigned i = 0; i < 4; ++i) {
            before |= std::uint32_t(static_cast<unsigned char>(original[x[0] + i])) << (8 * i);
        }

        need(before == x[1], "PASSION_B_RAM_ARGUMENT_PREIMAGE");
        put(result.startup, x[0] - 0x3000, x[2], 4);
    }

    need(original.substr(0x63e18, 8) == std::string(8, 0), "PASSION_B_ROOT3_PREIMAGE");
    put(result.startup, 0x63e18 - 0x3000, 0xe4fa041f, 8);
    put(result.early_l2, 296 * 8, 0xe500041d, 8);
    result.early_l2 = qnx_passion_11713_ifs2_early_map(result.early_l2);
    put(result.startup, result.startup.size() - 4, 0, 4);
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < result.startup.size(); at += 4) {
        std::uint32_t v = 0;

        for (unsigned i = 0; i < 4; ++i) {
            v |= std::uint32_t(static_cast<unsigned char>(result.startup[at + i])) << (8 * i);
        }

        sum += v;
    }

    put(result.startup, result.startup.size() - 4, 0 - sum, 4);

    return result;
}

std::string prepare_qnx_passion_11713_runtime(std::string_view source) {
    auto out = prepare_qnx_passion_runtime(source);
    auto change = [&](std::string_view before, std::string_view after) {
        unsigned count = 0;

        for (std::size_t at = 0; (at = out.find(before, at)) != std::string::npos;
             at += after.size()) {
            out.replace(at, before.size(), after);
            ++count;
        }

        need(count > 0, "PASSION_B_RUNTIME_FIELD_COUNT");
    };

    change("/proc/boot/bin/", "/bin/");
    change("/proc/boot/usr/", "/usr/");
    change("/proc/boot/lib64", "/lib64");
    // The secpol appends predate the RUN181 green reference and kill every spawn
    // (ldd: Could not preload library secpol-preload.so is FATAL), so keep the
    // preloads byte-identical to the green runs: identity replacements.
    change("/proc/boot/etc/", "/etc/");
    change("LD_PRELOAD=/bin/qditest\n", "LD_PRELOAD=/bin/qditest\n");
    change("LD_PRELOAD=$qm_snap ", "LD_PRELOAD=$qm_snap ");
    change("LD_PRELOAD=/etc/dispdump.sh \\\n",
           "LD_PRELOAD=/etc/dispdump.sh \\\n");
    // hd0 spans the file system extent 0x27c008000+5242880 sectors with a whole-device automount:
    // the cam partition device does not apply the GPT LBA64 base and fails with E302 on sblk.
    change("automount=hd0:/:qnx6:ro", "automount=hd0:/:qnx6:ro");
    need(out.starts_with("#!/bin/ksh\n") && out.find("/proc/boot/bin/") == std::string::npos &&
                 out.find("/proc/boot/lib64") == std::string::npos,
         "PASSION_B_RUNTIME_SCOPE");

    return out;
}

void validate_qnx_passion_11713_ifs2_names(std::string_view original) {
    need(original.size() == 217630, "PASSION_B_IFS2_MANAGER_SIZE");
    need(original.substr(0x2bf88, 5) == std::string_view("ifs2\0", 5) &&
                 original.substr(0x2c15e, 7) == std::string_view("ifs2_a\0", 7) &&
                 original.substr(0x2c194, 7) == std::string_view("ifs2_b\0", 7),
         "PASSION_B_IFS2_NAMES");
}

void validate_qnx_passion_11713_sync_dma(std::string_view original) {
    need(original.size() == 217630, "PASSION_B_DMA_MANAGER_SIZE");
    auto wide = [](std::string_view b, std::size_t a) {
        std::uint64_t v = 0;

        for (unsigned i = 0; i < 8; ++i) {
            v |= std::uint64_t(static_cast<unsigned char>(b.at(a + i))) << (8 * i);
        }

        return v;
    };

    // The existing guarded dispatch adapter binds this read and done pair to OWN
    // synchronous RAM copy and its completed-operation status, never a physical UFS.
    for (const auto &p :
         std::array<std::array<std::uint64_t, 3>, 4>{{{0x9b78, 0x42fb0, 0x1da3c},
                                                      {0x9b60, 0x42fa8, 0x1da78},
                                                      {0x9c08, 0x42fe0, 0x1d970},
                                                      {0x9ad0, 0x42f68, 0x1d9c0}}}) {
        need(wide(original, p[0]) == p[1] && wide(original, p[0] + 8) == 1027 &&
                     wide(original, p[0] + 16) == p[2],
             "PASSION_B_DMA_VTABLE_RELA");
    }
}

std::string adapt_qnx_passion_11713_startupmgr(std::string_view original, std::string_view reader,
                                               std::string_view large_reader) {
    need(reader.size() == 176, "PASSION_B_READER_SOURCE");
    need(large_reader.size() == reader.size(), "PASSION_B_READER_SIZE");

    for (std::size_t i = 0; i < reader.size(); ++i) {
        if (i < 0x3c || i >= 0x40) {
            need(large_reader[i] == reader[i], "PASSION_B_READER_OTHER_BYTES");
        }
    }

    auto le = [](std::string_view b, std::size_t n) {
        std::uint32_t w = 0;

        for (unsigned i = 0; i < 4; ++i) {
            w |= std::uint32_t(static_cast<unsigned char>(b[n + i])) << (8 * i);
        }

        return w;
    };
    need(le(reader, 0x3c) == 0xd2b5ff85 && le(large_reader, 0x3c) == 0xd2bc9f85,
         "PASSION_B_READER_ADDRESS");
    validate_qnx_passion_11713_ifs2_names(original);
    validate_qnx_passion_11713_sync_dma(original);
    auto output = adapt_qnx_passion_startupmgr_entry(original, reader);

    for (const auto &x :
         std::array<std::array<std::uint32_t, 3>, 3>{{{0x30b1c + 0x3c, 0xd2b5ff85, 0xd2bc9f85},
                                                      {0x27018, 0xd2b18005, 0xd2bc9f65},
                                                      {0x27140, 0xd2b18006, 0xd2bc9f66}}}) {
        need(le(output, x[0]) == x[1], "PASSION_B_MANAGER_PREIMAGE");
        put(output, x[0], x[2], 4);
    }

    // One immutable virtual IFS volume is selected by either OEM boot-slot request.
    // Original partition lookup, complete128B entry, errors and LBA geometry are preserved.
    for (const auto &site : std::array<std::array<std::uint32_t, 3>, 2>{
                 {{0xf5f8, 0x91057800, 0x100e4c80}, {0xf634, 0x91065000, 0x100e4aa0}}}) {
        need(le(output, site[0]) == site[1], "PASSION_B_IFS2_NAME_PREIMAGE");
        put(output, site[0], site[2], 4);
    }

    // OWN read returns success only after its actual mmap-backed copy. It emits
    // no UFS IRQ. The skipped 44278/44288 callbacks are guarded pure return0;
    // 442A0 only returns physical IRQ297 for InterruptUnmask. No cleanup is
    // skipped. Retain the original done/status test and chunk accounting.
    need(le(output, 0x10134) == 0x34000240, "PASSION_B_DMA_BRANCH_PREIMAGE");
    put(output, 0x10134, 0x34000320, 4);

    return output;
}

std::string assemble_qnx_passion_11713_kernel(std::string_view original, std::string_view packed,
                                              std::string_view host_pin) {
    auto output = assemble_qnx_passion_kernel(original, packed, host_pin);
    const auto prepared = prepare_qnx_passion_11713_bootstrap(original);

    output.replace(0x3000, prepared.startup.size(), prepared.startup);
    put(output, 0x3024, output.size() - 0x3000, 4);
    put(output, 0x69104, 0, 4);
    std::uint32_t sum = 0;

    for (std::size_t at = 0x3000; at < 0x69104; at += 4) {
        std::uint32_t v = 0;

        for (unsigned i = 0; i < 4; ++i) {
            v |= std::uint32_t(static_cast<unsigned char>(output[at + i])) << (8 * i);
        }

        sum += v;
    }

    put(output, 0x69104, 0 - sum, 4);
    need(output.substr(0x69108) == packed, "PASSION_B_PACKED_PARITY");

    return output;
}

void validate_qnx_passion_11713_bootdev(std::string_view original) {
    need(original.size() == 2254664, "PASSION_B_CONFIG_STARTUP_SIZE");
}

QnxPassion11713Bootstrap prepare_qnx_passion_11713_config_bootstrap(std::string_view original) {
    validate_qnx_passion_11713_bootdev(original);

    // Preserve the original BOOT_CONFIG extraction and complete chip-specific validator.
    // Chip16A accepts selectors0/1/8 and already publishes tag1; selector4 is unsupported.
    return prepare_qnx_passion_11713_bootstrap(original);
}

void validate_qnx_passion_11713_config_device(std::string_view original) {
    need(original.size() == 217630, "PASSION_B_CONFIG_MANAGER_SIZE");
    using namespace std::string_view_literals;

    for (const auto &item : std::array<std::pair<std::size_t, std::string_view>, 7>{
                 {{0x2ad2f, "devb-ufs-qualcomm\0"sv},
                  {0x2ad41, "devb_ufs_t\0"sv},
                  {0x2ad4c, "name=uda\0"sv},
                  {0x2ad89, "ufs\0"sv},
                  {0x2ad8d, "ioport=0x1D84000,irq=297,pm=600:300,bs=pm,gear=3\0"sv},
                  {0x2ad20, "/dev/disk/uda5\0"sv},
                  {0x2ae6a, "/dev/disk/uda0\0"sv}}}) {
        need(original.substr(item.first, item.second.size()) == item.second,
             "PASSION_B_CONFIG_DEVICE_STRING");
    }

    need(original.substr(0x33680, 8) == std::string(8, 0), "PASSION_B_CONFIG_ARGV_TERMINATOR");
}

std::string adapt_qnx_passion_11713_config_startupmgr(std::string_view original,
                                                      std::string_view reader,
                                                      std::string_view large_reader) {
    validate_qnx_passion_11713_config_device(original);
    auto out = adapt_qnx_passion_11713_startupmgr(original, reader, large_reader);
    auto slot = [&](std::size_t at, std::size_t bytes, std::string_view value) {
        need(value.size() + 1 <= bytes, "PASSION_B_CONFIG_SLOT");
        std::string replacement(bytes, 0);
        replacement.replace(0, value.size(), value);
        out.replace(at, bytes, replacement);
    };
    slot(0x2ad2f, 18, "devb-ram");
    slot(0x2ad89, 4, "ram");
    slot(0x2ad8d, 49, "capacity=65536,blksize=4096,address=0xE4FC0000");
    // The genuine waitfor path follows the real sole volume; do not invent physical LUN5.
    put(out, 0xaaf0 + 16, 0x2ae6a, 8);

    return out;
}

namespace {
std::uint32_t config_crc(std::string_view s) {
    std::uint32_t v = ~0U;

    for (unsigned char c : s) {
        v ^= c;

        for (unsigned i = 0; i < 8; ++i) {
            v = (v >> 1) ^ ((v & 1) ? 0xedb88320 : 0);
        }
    }

    return ~v;
}
} // namespace

QnxIfs2Gpt qnx_passion_11713_config_gpt(std::uint64_t bytes) {
    need(bytes == 200929280 && 0xe5000000ULL + bytes <= 0xf1000000ULL &&
                 0xf1020000ULL <= 0xf4fbe000ULL,
         "PASSION_B_CONFIG_EXTENT");
    auto out = qnx_passion_gpt(bytes);
    need(out.cache.size() == 8192 && out.primary.size() == 12288 && out.backup.size() == 8192,
         "PASSION_B_CONFIG_GPT_SIZE");
    std::string entries = out.cache.substr(4096);
    need(entries.substr(128) == std::string(4096 - 128, 0), "PASSION_B_CONFIG_GPT_EMPTY");
    std::string e(128, 0);
    e.replace(0, 16, entries.substr(0, 16));
    constexpr std::array<unsigned char, 16> own_guid{0xe9, 0xc8, 0x40, 0x5e, 0x52, 0xce,
                                                     0x47, 0x4d, 0x9e, 0x75, 0xb2, 0xcc,
                                                     0x72, 0xc2, 0x14, 0x44};

    for (unsigned i = 0; i < own_guid.size(); ++i) {
        e[16 + i] = static_cast<char>(own_guid[i]);
    }

    need(e.substr(16, 16) != entries.substr(16, 16), "PASSION_B_CONFIG_GUID");
    put(e, 32, 49216, 8);
    put(e, 40, 49247, 8);
    constexpr std::string_view name = "config_data";

    for (unsigned i = 0; i < name.size(); ++i) {
        put(e, 56 + i * 2, static_cast<unsigned char>(name[i]), 2);
    }

    entries.replace(128, 128, e);
    auto update = [&](std::string &image, std::size_t header, std::size_t array) {
        image.replace(array, 4096, entries);
        put(image, header + 88, config_crc(entries), 4);
        put(image, header + 16, 0, 4);
        put(image, header + 16, config_crc(std::string_view(image).substr(header, 92)), 4);
    };
    update(out.cache, 0, 4096);
    update(out.primary, 4096, 8192);
    update(out.backup, 4096, 0);

    return out;
}

void validate_qnx_passion_11713_config_gpt(const QnxIfs2Gpt &images, std::uint64_t bytes) {
    const auto expected = qnx_passion_11713_config_gpt(bytes);

    need(images.cache == expected.cache && images.primary == expected.primary &&
                 images.backup == expected.backup,
         "PASSION_B_CONFIG_GPT_METADATA");
}

std::string qnx_passion_11713_blank_config() {
    return std::string(131072, 0);
}

std::string assemble_qnx_passion_11713_config_kernel(std::string_view original,
                                                     std::string_view packed,
                                                     std::string_view host_pin) {
    validate_qnx_passion_11713_bootdev(original);

    return assemble_qnx_passion_11713_kernel(original, packed, host_pin);
}

void validate_qnx_passion_11713_ifs_integrity_source(std::string_view original) {
    need(original.size() == 217630, "PASSION_B_IFS_HASH_MANAGER_SIZE");
    auto value = [](std::string_view b, std::size_t at) {
        std::uint64_t v = 0;

        for (unsigned i = 0; i < 8; ++i) {
            v |= std::uint64_t(static_cast<unsigned char>(b.at(at + i))) << (8 * i);
        }

        return v;
    };

    for (const auto &a :
         std::array<std::array<std::uint64_t, 3>, 4>{{{0x8708, 0x40ee8, 0x2a5ec},
                                                      {0x8738, 0x40ef8, 0x2a5fc},
                                                      {0x87e0, 0x40f60, 0x2a6de},
                                                      {0x8810, 0x40f70, 0x2a6eb}}}) {
        need(value(original, a[0]) == a[1] && value(original, a[0] + 8) == 1027 &&
                     value(original, a[0] + 16) == a[2],
             "PASSION_B_IFS_HASH_RECORD_RELA");
    }

    need(original.substr(0x2a5ec, 16) == std::string_view("ifs_display.img\0", 16) &&
                 original.substr(0x2a6de, 13) == std::string_view("ifs_disk.img\0", 13),
         "PASSION_B_IFS_HASH_RECORD_NAME");
}
} // namespace voyemu
