#include "qnx_host_adapt.hpp"
#include "check.hpp"
#include "error.hpp"
#include "little_endian.hpp"
#include "process.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace voyemu {
namespace {
using namespace std::string_view_literals;

constexpr CodeCheck need{"QNX host adaptation check failed"};

std::uint64_t field(std::string_view bytes, std::size_t at, unsigned width) {
    return le_read(bytes, at, width, need, "QNX_HOST_BOUNDS");
}

void put(std::string &bytes, std::size_t at, std::uint64_t value, unsigned width) {
    le_write(bytes, at, value, width, need, "QNX_HOST_BOUNDS");
}

void replace(std::string &bytes, std::string_view before, std::string_view after, unsigned expected,
             bool clear = false) {
    need(!before.empty() && after.size() <= before.size(), "QNX_HOST_STRING_CAPACITY");
    unsigned count = 0;

    for (std::size_t at = 0; (at = bytes.find(before, at)) != std::string::npos;
         at += before.size()) {
        ++count;
    }

    if (count != expected) {
        throw VoyemuError("QNX_HOST_STRING_COUNT",
                          std::string(before.substr(0, before.find(char(0)))) + " expected=" +
                                  std::to_string(expected) + " actual=" + std::to_string(count));
    }

    for (std::size_t at = 0; (at = bytes.find(before, at)) != std::string::npos;
         at += before.size()) {
        if (clear) {
            bytes.replace(at, before.size(), std::string(before.size(), 0));
        }

        bytes.replace(at, after.size(), after);
    }
}

void digest(std::string &bytes, std::string_view marker, std::string_view value) {
    need(sha256_text(value), "QNX_HOST_DIGEST");
    const auto at = bytes.find(marker);
    need(at != std::string::npos && bytes.find(marker, at + 1) == std::string::npos,
         "QNX_HOST_DIGEST_MARKER");
    const auto offset = at + marker.size();
    need(offset + 65 <= bytes.size() && bytes[offset + 64] == 0 &&
                 sha256_text(std::string_view(bytes).substr(offset, 64)),
         "QNX_HOST_DIGEST_SLOT");
    bytes.replace(offset, 64, value);
}
} // namespace

std::string adapt_qnx_boot_script(std::string_view original) {
    std::string result(original);

    replace(result, "-D\0SLOG2_PHYS\0"sv,
            "-s\0"
            "64000     \0"sv,
            1);
    replace(result, "/dev/ser1\0"sv, "/dev/text\0"sv, 2);
    replace(result, "/dev/slog2\0"sv, "/proc/boot\0"sv, 1);
    replace(result, "/dev/bmetrics\0"sv, "/proc/boot/./\0"sv, 1);
    replace(result, "/bin/startupmgr\0-i\0ifs2\0-m\0/\0"sv, "/bin/startupmgr\0-i\0ifs2\0-v\0v\0"sv,
            1);
    replace(result, "PATH=/bin:/usr/bin:/usr/sbin:/usr/lib64:/etc:/sbin:/scripts"sv,
            "PATH=/proc/boot/bin:/proc/boot:/bin:/usr/bin:/sbin\0"sv, 10);
    replace(result, "LD_LIBRARY_PATH=/lib64/:/usr/lib64:/lib64/dll:/lib/:/usr/lib:/lib64/dll/pci"sv,
            "LD_LIBRARY_PATH=/proc/boot/lib64/:/proc/boot/lib64/dll/:/usr/lib64/:/lib64/"sv, 10);

    return result;
}

std::string adapt_qnx_procnto_environment(std::string_view original) {
    std::string result(original);

    replace(result, "PATH=/bin:/usr/bin:/usr/sbin:/sbin:/scripts\0"sv,
            "PATH=/proc/boot/bin/:/bin/:/usr/bin/:/sbin/\0"sv, 1);
    replace(result, "LD_LIBRARY_PATH=/lib64:/lib64/dll:/usr/lib:/usr/lib64:/lib::"sv,
            "LD_LIBRARY_PATH=/proc/boot/lib64:/proc/boot/lib64/dll:/lib64"sv, 1);

    return result;
}

std::string adapt_qnx_qinggan11_boot_script(std::string_view original) {
    std::string result(original);

    replace(result, "-D\0SLOG2_PHYS\0"sv,
            "-s\0"
            "64000     \0"sv,
            1);
    replace(result, "/dev/ser1\0"sv, "/dev/text\0"sv, 2);
    replace(result, "/dev/slog2\0"sv, "/proc/boot\0"sv, 1);
    replace(result, "/dev/bmetrics\0"sv, "/proc/boot/./\0"sv, 1);
    replace(result, "/bin/startupmgr\0-i\0ifs2\0-m\0/\0"sv, "/bin/startupmgr\0-i\0ifs2\0-v\0v\0"sv,
            1);
    replace(result, "PATH=/bin:/usr/bin:/usr/sbin:/usr/lib64:/etc:/sbin:/scripts"sv,
            "PATH=/proc/boot/bin:/proc/boot:/bin:/usr/bin:/sbin\0"sv, 10);
    replace(result, "LD_LIBRARY_PATH=/lib64/:/usr/lib64:/lib64/dll:/lib/:/usr/lib:/lib64/dll/pci"sv,
            "LD_LIBRARY_PATH=/proc/boot/lib64/:/proc/boot/lib64/dll/:/usr/lib64/:/lib64/"sv, 10);
    // Repack only the final console record, preserving every declared argument and environment
    // field.
    constexpr std::size_t last = 0xd60, old_size = 0x170;
    need(original.size() == 3796 && field(original, last, 2) == old_size &&
                 field(original, last + 2, 2) == 0 && field(original, last + 4, 4) == 0x200 &&
                 field(original, last + 8, 2) == 0 && field(original, last + 10, 1) == 5 &&
                 field(original, last + 11, 1) == 13,
         "QNX_QINGGAN11_BOOT_RECORD");
    std::vector<std::string> fields;
    std::size_t cursor = last + 12;

    for (unsigned i = 0; i < 19; ++i) {
        const auto zero = original.find('\0', cursor);
        need(zero != std::string_view::npos && zero < last + old_size, "QNX_QINGGAN11_BOOT_FIELDS");
        fields.emplace_back(original.substr(cursor, zero - cursor));
        cursor = zero + 1;
    }

    need(original.substr(cursor).find_first_not_of('\0') == std::string_view::npos &&
                 fields[0] == "on" && fields[1] == "on" && fields[2] == "-ae" &&
                 fields[3] == "ksh" && fields[4] == "-c" && fields[5] == "login" &&
                 fields.back().starts_with("PS1="),
         "QNX_QINGGAN11_BOOT_CONSOLE");
    fields[5] =
            R"BOOT((b=/proc/boot/bin;c=/proc/boot/lib64/graphics.conf;w(){ echo $2;n=0;until test -$1 $2;do [ $((n+=1)) -lt 300 ]||exit 1;sleep .1;done;};for f in devb-ram screen ssplash ksh;do w x $b/$f;done;w r $c;test -e /dev/pipe||$b/pipe& w e /dev/pipe;echo x|read x||exit 1;$b/screen -c $c&$b/ksh $b/ssplash)&exec login)BOOT";
    unsigned path_count = 0, library_count = 0;

    for (std::size_t i = 6; i < fields.size(); ++i) {
        if (fields[i].starts_with("PATH=")) {
            fields[i] = "PATH=/proc/boot/bin:/proc/boot:/bin:/usr/bin:/sbin";
            ++path_count;
        }

        if (fields[i].starts_with("LD_LIBRARY_PATH=")) {
            fields[i] =
                    "LD_LIBRARY_PATH=/proc/boot/lib64/:/proc/boot/lib64/dll/:/usr/lib64/:/lib64/";
            ++library_count;
        }
    }

    need(path_count == 1 && library_count == 1, "QNX_QINGGAN11_BOOT_ENVIRONMENT");
    std::string record(original.substr(last, 12));

    for (const auto &value : fields) {
        record += value;
        record += '\0';
    }

    record.resize((record.size() + 3) & ~std::size_t(3), 0);
    need(record.size() >= old_size && record.size() - old_size <= 300,
         "QNX_QINGGAN11_BOOT_CAPACITY");
    put(record, 0, record.size(), 2);
    result.resize(last);
    result += record;
    result.append(4, 0);
    need(result.size() <= 4096, "QNX_QINGGAN11_BOOT_CAPACITY");

    return result;
}

std::string adapt_qnx_qinggan11_procnto_environment(std::string_view original) {
    std::string result(original);

    replace(result, "PATH=/bin:/usr/bin:/usr/sbin:/sbin:/scripts\0"sv,
            "PATH=/proc/boot/bin/:/bin/:/usr/bin/:/sbin/\0"sv, 1);
    replace(result, "LD_LIBRARY_PATH=/lib64:/lib64/dll:/usr/lib:/usr/lib64:/lib::"sv,
            "LD_LIBRARY_PATH=/proc/boot/lib64:/proc/boot/lib64/dll:/lib64"sv, 1);

    return result;
}

std::string adapt_qnx_qinggan11_symlinks(std::string_view original) {
    std::string result(original);

    // Select the SA8155 vector and pass its fixed driver paths to pathmgr_symlink.
    for (const auto &[at, before, after] : std::array<std::array<std::uint32_t, 3>, 2>{
                 {{0xf40, 0xb4000100, 0x1400000d}, {0x12c0, 0xb4000bc0, 0x14000094}}}) {
        need(field(result, at, 4) == before, "QNX_QINGGAN11_SYMLINK_INSTRUCTION");
        put(result, at, after, 4);
    }

    replace(result,
            "/lib64/libclock_asic_8195.so\0/lib64/libtlmm_startup_driver_asic_8195.so\0/lib64/libqcpecfg-host-8195\0"sv,
            "/lib64/libdalconfig-sa8155_adp_star_v1.0.0.so\0/lib64/libqcpecfg-host-8155.so\0"sv, 1,
            true);

    for (const auto &[at, target, before, after] : std::array<std::array<std::uint64_t, 4>, 2>{
                 {{0x9a8, 0x12058, 0x16f2, 0x1707}, {0x9f0, 0x12070, 0x17b3, 0x1735}}}) {
        need(field(result, at, 8) == target && field(result, at + 8, 8) == 1027 &&
                     field(result, at + 16, 8) == before,
             "QNX_QINGGAN11_SYMLINK_RELOCATION");
        put(result, at + 16, after, 8);
    }

    return result;
}

std::string adapt_qnx_symlinks(std::string_view original) {
    std::string result(original);

    for (const auto &[at, before, after] : std::array<std::array<std::uint32_t, 3>, 2>{
                 {{0xf00, 0xb40000c0, 0x1400000b}, {0x1268, 0xb4000880, 0x14000072}}}) {
        need(field(result, at, 4) == before, "QNX_SYMLINK_INSTRUCTION");
        put(result, at, after, 4);
    }

    replace(result,
            "/lib64/libclock_asic_8195.so\0/lib64/libtlmm_startup_driver_asic_8195.so\0/lib64/libqcpecfg-host-8195\0"sv,
            "/lib64/libdalconfig-sa8155_adp_star_v1.0.0.so\0/lib64/libqcpecfg-host-8155.so\0"sv, 1,
            true);

    for (const auto &[at, target, before, after] : std::array<std::array<std::uint64_t, 4>, 2>{
                 {{0x990, 0x12058, 0x15af, 0x15c4}, {0x9d8, 0x12070, 0x1670, 0x15f2}}}) {
        need(field(result, at, 8) == target && field(result, at + 8, 8) == 1027 &&
                     field(result, at + 16, 8) == before,
             "QNX_SYMLINK_RELOCATION");
        put(result, at + 16, after, 8);
    }

    return result;
}

std::string adapt_qnx_passion_symlinks(std::string_view original) {
    std::string result(original);

    // The board token uname() reports here matches none of SA61/SA815/SA8195, so the OEM
    // helper skips both link groups; take the SA8155 target triplet unconditionally.
    need(field(result, 0x1034, 4) == 0xb40000a0, "QNX_PASSION_SYMLINK_INSTRUCTION");
    put(result, 0x1034, 0x1400000a, 4);
    // The dalconfig link name is built from uname().version, which this machine cannot supply,
    // and the OEM normalisation keeps only two of its tokens; hand the whole shipped suffix to
    // the "%s%s.so" call by replacing the built string pointer right before it and mapping that
    // suffix out of the .text slack.
    constexpr std::size_t slack = 0x1c50;
    static constexpr std::string_view suffix = "sa8155_mega_mon_v11.0.33";
    need(field(result, 0xb0, 4) == 1 && field(result, 0xd0, 8) == slack &&
                 field(result, 0xd8, 8) == slack,
         "QNX_PASSION_SYMLINK_PHDR");

    for (std::size_t at = 0; at < suffix.size() + 1; ++at) {
        need(result[slack + at] == 0, "QNX_PASSION_SYMLINK_SLACK");
    }

    need(field(result, 0x1488, 4) == 0xaa1903e0 && field(result, 0x148c, 4) == 0x97fffea9,
         "QNX_PASSION_SYMLINK_CONTEXT");
    // The QCPE Config link picks "normal" out of uname().version, so this machine builds
    // "<target>_unknown.so"; take the plain "%s.so" branch the board would take.
    need(field(result, 0x1548, 4) == 0x35000140, "QNX_PASSION_SYMLINK_QCPECFG");
    put(result, 0x1548, 0xd503201f, 4);
    result.replace(slack, suffix.size() + 1, std::string(suffix) + '\0');
    put(result, 0xd0, slack + 32, 8);
    put(result, 0xd8, slack + 32, 8);
    put(result, 0x1488, 0x90000000, 4);
    put(result, 0x148c, 0x91314000, 4);
    need(std::string_view(result).substr(slack, suffix.size()) == suffix,
         "QNX_PASSION_SYMLINK_SLACK_READBACK");

    return result;
}

static std::string adapt_qnx_qinggan11_startupmgr_gpt_impl(std::string_view original) {
    std::string result(original);
    // The new loader iterates LUNs and passes address/size output pointers in x1/x2.
    constexpr std::array<std::uint32_t, 3> sites[]{
            std::array<std::uint32_t, 3>{0x268a8, 0x35000580, 0x52820001},
            {0x268ac, 0xb9404fa1, 0xb90002c1},
            {0x268d8, 0xf9402ba5, 0xd2b18005},
            {0x268fc, 0x94000499, 0x9400003f}};

    for (const auto &[offset, before, after] : sites) {
        need(field(result, offset, 4) == before, "QNX_QINGGAN11_GPT_INSTRUCTION");
        put(result, offset, after, 4);
    }

    constexpr std::array<std::uint32_t, 7> before{0xd1000442, 0xd2800003, 0x78637825, 0x2a0303e6,
                                                  0x340000e5, 0x93407c64, 0x91000463};
    constexpr std::array<std::uint32_t, 7> helper{0x52820006, 0xb9000046, 0xd2b18006, 0x914004c6,
                                                  0xf9000026, 0x52800000, 0xd65f03c0};

    for (unsigned i = 0; i < helper.size(); ++i) {
        need(field(result, 0x269f8 + i * 4, 4) == before[i], "QNX_QINGGAN11_GPT_HELPER_SLOT");
        put(result, 0x269f8 + i * 4, helper[i], 4);
    }

    return result;
}

static std::string adapt_qnx_qinggan11_startupmgr_dispatch_impl(std::string_view original,
                                                                std::string_view stub) {
    std::string result(original), readstub(stub);

    // Same source-owned RAM reader, relocated to this firmware's slot and mmap PLT.
    need(stub.size() == 140 && field(original, 0x144b4, 4) == 0xa9bd7bfd,
         "QNX_QINGGAN11_DISPATCH_STUB_SLOT");
    put(readstub, 0x3c,
        0x94000000U | (static_cast<std::uint32_t>((0xbe20 - 0x144b4 - 0x3c) / 4) & 0x3ffffffU), 4);
    result.replace(0x144b4, readstub.size(), readstub);
    // Verified disk-dispatch RELA targets and function identities in the A11 table.
    constexpr std::array<std::uint32_t, 4> relas[]{
            std::array<std::uint32_t, 4>{0x9878, 0x41e78, 0x1d29c, 0x144b4},
            {0x9890, 0x41e80, 0x1d36c, 0x1d390},
            {0x9950, 0x41ef0, 0x1d398, 0x1d390},
            {0x9968, 0x41f00, 0x1d2d8, 0x1d390},
            {0x9998, 0x41f10, 0x1d3c0, 0x1d390},
            {0x99e0, 0x41f28, 0x1d370, 0x1d390},
            {0x9a40, 0x41f68, 0x1d298, 0x1d390},
            {0x9a58, 0x41f70, 0x1d3e8, 0x1d390},
            {0x9ad0, 0x41fa8, 0x1d350, 0x1d390},
            {0x9ae8, 0x41fb0, 0x1d314, 0x144b4},
            {0x9b30, 0x41fc8, 0x1d364, 0x1d390},
            {0x9b78, 0x41fe0, 0x1d248, 0x1d390},
            {0x9b90, 0x41ff8, 0x1d270, 0x1d390}};

    for (const auto &[offset, target, before, after] : relas) {
        need(field(result, offset, 8) == target && field(result, offset + 8, 8) == 1027 &&
                     field(result, offset + 16, 8) == before,
             "QNX_QINGGAN11_DISPATCH_RELOCATION");
        put(result, offset + 16, after, 8);
    }

    need(field(result, 0x1d39c, 4) == 0xa9bf7bfd && field(result, 0x1d3a0, 4) == 0x910003fd,
         "QNX_QINGGAN11_DISPATCH_GEOMETRY");
    need(field(original, 0x9a70, 8) == 0x41f78 && field(original, 0x9a78, 8) == 1027 &&
                 field(original, 0x9a80, 8) == 0x1d39c,
         "QNX_QINGGAN11_DISPATCH_DENSITY_RELOCATION");
    put(result, 0x1d39c, 0x52a00020, 4);
    put(result, 0x1d3a0, 0xd65f03c0, 4);

    return result;
}

std::string adapt_qnx_qinggan11_startupmgr_gpt(std::string_view original) {
    return adapt_qnx_qinggan11_startupmgr_gpt_impl(original);
}

std::string adapt_qnx_qinggan11_startupmgr_dispatch(std::string_view original,
                                                    std::string_view stub) {
    return adapt_qnx_qinggan11_startupmgr_dispatch_impl(original, stub);
}

// Explicit developer fixture only: not selected by production preparation.
std::string qnx_qinggan11_developer_candidate(std::string_view original, std::string_view stub,
                                              std::string_view display, std::string_view disk) {
    const auto dispatch = adapt_qnx_qinggan11_startupmgr_dispatch_impl(original, stub),
               gpt = adapt_qnx_qinggan11_startupmgr_gpt_impl(original);
    struct Change {
        std::size_t at;
        std::string before, after;
    };
    std::vector<Change> changes;
    auto plan = [&](std::size_t at, std::string_view before, std::string_view after) {
        need(before.size() == after.size() && at + before.size() <= original.size() &&
                     original.substr(at, before.size()) == before,
             "QNX_QINGGAN11_DEVELOPER_EXPECTED");

        for (const auto &c : changes) {
            need(at + before.size() <= c.at || c.at + c.before.size() <= at,
                 "QNX_QINGGAN11_DEVELOPER_OVERLAP");
        }

        changes.push_back({at, std::string(before), std::string(after)});
    };
    auto word = [&](std::size_t at, std::uint64_t before, std::uint64_t after, unsigned width) {
        need(field(original, at, width) == before, "QNX_QINGGAN11_DEVELOPER_WORD");
        std::string a(width, 0), b(width, 0);
        put(a, 0, before, width);
        put(b, 0, after, width);
        plan(at, a, b);
    };

    for (const auto *stage : {&dispatch, &gpt}) {
        for (std::size_t at = 0; at < original.size();) {
            if ((*stage)[at] == original[at]) {
                ++at;
                continue;
            }

            const auto first = at;

            while (at < original.size() && (*stage)[at] != original[at]) {
                ++at;
            }

            plan(first, original.substr(first, at - first),
                 std::string_view(*stage).substr(first, at - first));
        }
    }

    // Preserve complete UFS prologue and default status before selecting its table.
    need(field(original, 0x14150, 4) == 0xa9be7bfd && field(original, 0x14158, 4) == 0x910003fd &&
                 field(original, 0x14168, 4) == 0xf9000bf3 &&
                 field(original, 0x1416c, 4) == 0x528002d3,
         "QNX_QINGGAN11_DEVELOPER_PROLOGUE");
    word(0x14170, 0x97ffe02c, 0x14000009, 4);
    word(0xed38, 0x91183000, 0x91184400, 4);
    // Both physical slot names resolve to the one immutable developer IFS2 partition; lookup errors
    // remain genuine.
    constexpr std::array<std::array<std::uint32_t, 2>, 7> lookup{{{0xf140, 0x3500021a},
                                                                  {0xf144, 0x900000e0},
                                                                  {0xf14c, 0x94005ec8},
                                                                  {0xf150, 0x340003c0},
                                                                  {0xf180, 0x900000e0},
                                                                  {0xf188, 0x94005eb9},
                                                                  {0xf18c, 0x340001e0}}};

    for (const auto &site : lookup) {
        need(field(original, site[0], 4) == site[1], "QNX_QINGGAN11_DEVELOPER_IFS2_LOOKUP");
    }

    need(original.substr(0x2b60c, 5) == "ifs2\0"sv && original.substr(0x2b7d2, 7) == "ifs2_a\0"sv &&
                 original.substr(0x2b808, 7) == "ifs2_b\0"sv,
         "QNX_QINGGAN11_DEVELOPER_IFS2_NAMES");
    word(0xf148, 0x911f4800, 0x91183000, 4);
    word(0xf184, 0x91202000, 0x91183000, 4);
    // The adapted RAM callback completes the real copy synchronously and has no DMA IRQ.
    constexpr std::array<std::array<std::uint32_t, 2>, 20> dma{
            {{0xf9e4, 0x52809784},  {0xfa04, 0x97fff13b},  {0xfa08, 0x94001287},
             {0xfa28, 0x94001270},  {0xfa2c, 0x34000240},  {0xfa30, 0x94001265},
             {0xfa78, 0x97fff0f6},  {0xfa8c, 0x97fff0a5},  {0xfa90, 0xb94083a0},
             {0xfa94, 0x94001258},  {0xfa98, 0x34000220},  {0xfadc, 0x4b1b02d6},
             {0xfae0, 0xb1b02f7},   {0xfae4, 0x17ffffb2},  {0x141d8, 0xb0000161},
             {0x141dc, 0xf947d821}, {0x141e0, 0xf9001401}, {0x143e8, 0xf0000164},
             {0x143ec, 0xf9407084}, {0x143f0, 0xd61f0080}}};

    for (const auto &site : dma) {
        need(field(original, site[0], 4) == site[1], "QNX_QINGGAN11_DEVELOPER_SYNC_DMA");
    }

    word(0xfa2c, 0x34000240, 0x34000320, 4);
    // This virtual display has one owner in the independent boot record, never the OEM hardware
    // launcher.
    need(field(original, 0x32138, 8) == 0 && field(original, 0x9ea8, 8) == 0x42128 &&
                 field(original, 0x9eb0, 8) == 1027 && field(original, 0x9eb8, 8) == 0x40ac0,
         "QNX_QINGGAN11_DISPLAY_EMPTY_VECTOR");

    for (const auto &[at, target] :
         std::array<std::array<std::uint32_t, 2>, 2>{{{0x86d8, 0x400e8}, {0x8888, 0x40200}}}) {
        need(field(original, at, 8) == target && field(original, at + 8, 8) == 1027,
             "QNX_QINGGAN11_DISPLAY_LAUNCH_RELA");
        word(at + 16, 0x42100, 0x42138, 8);
    }

    auto text = [&](std::size_t at, std::string_view before, std::string_view after) {
        need(after.size() <= before.size(), "QNX_QINGGAN11_DEVELOPER_CAPACITY");
        std::string padded(before.size(), 0);
        padded.replace(0, after.size(), after);
        plan(at, before, padded);
    };
    text(0x2a55b, "/dev/disk/uda5\0"sv, "/dev/hd0\0"sv);
    text(0x2a56a, "devb-ufs-qualcomm\0"sv, "devb-ram\0"sv);
    text(0x2a620, "ufs\0ioport=0x1D84000,irq=297,pm=600:300,bs=pm,gear=3\0"sv,
         "ram\0capacity=2097250,address=0xC0000000\0"sv);
    text(0x2a315, "/lib64/graphics.conf\0openwfd_splash\0"sv,
         "/proc/boot/lib64/graphics.conf\0pipe\0"sv);
    need(original.substr(0x2a5f4, 14) == "name=/ram/dma\0"sv &&
                 field(original, 0x28034, 4) == 0xd0000000 &&
                 field(original, 0x28038, 4) == 0x9117f400,
         "QNX_QINGGAN11_DEVELOPER_SHARED_SUFFIX");
    word(0x2a5f9, '/', 0, 1);
    constexpr std::array<std::uint32_t, 4> refs[]{
            std::array<std::uint32_t, 4>{0x8ea0, 0x40b78, 0x2a32a, 0x2a334},
            {0xa4d8, 0x423b8, 0x2a315, 0x2a315},
            {0xa4f0, 0x423c8, 0x2a32a, 0x2a334},
            {0xa808, 0x42518, 0x2a55b, 0x2a55b},
            {0x9188, 0x41078, 0x2a56a, 0x2a56a},
            {0xa820, 0x42528, 0x2a56a, 0x2a56a},
            {0xa8c8, 0x42560, 0x2a5f4, 0x2a5f4},
            {0xa910, 0x42578, 0x2a620, 0x2a620},
            {0xa928, 0x42580, 0x2a624, 0x2a624}};

    for (const auto &[at, target, before, after] : refs) {
        need(field(original, at, 8) == target && field(original, at + 8, 8) == 1027 &&
                     field(original, at + 16, 8) == before,
             "QNX_QINGGAN11_DEVELOPER_STRING_RELA");

        if (before != after) {
            word(at + 16, before, after, 8);
        }
    }

    auto hash = [&](std::size_t at, std::string_view marker, std::string_view value) {
        need(sha256_text(value) && original.substr(at, marker.size()) == marker,
             "QNX_QINGGAN11_DEVELOPER_DIGEST");
        at += marker.size();
        need(sha256_text(original.substr(at, 64)) && original[at + 64] == 0,
             "QNX_QINGGAN11_DEVELOPER_DIGEST_SLOT");
        plan(at, original.substr(at, 64), value);
    };
    hash(0x29eb5, "ifs_display.img\0"sv, display);
    hash(0x29fa7, "ifs_disk.img\0"sv, disk);
    // All source/stub/word/string/relocation and overlap guards precede this mutation.
    std::string result(original);

    for (const auto &c : changes) {
        result.replace(c.at, c.after.size(), c.after);
    }

    return result;
}

std::string adapt_qnx_startupmgr(std::string_view original, std::string_view stub,
                                 std::string_view display, std::string_view disk) {
    std::string result(original);
    // Existing RAM-read, launcher and GPT control-flow sites from the SA8155 adapter.
    constexpr std::array<std::uint32_t, 3> words[]{
            std::array<std::uint32_t, 3>{0xc340, 0xd00001b0, 0xd65f03c0},
            {0xcb4c, 0x34000340, 0xd503201f},
            {0xd3b8, 0x2a1403f3, 0x52800013},
            {0xef88, 0x9102c000, 0x9102d400},
            {0xf3b0, 0x54000160, 0x14000001},
            {0xf588, 0x97fff4de, 0x52800020},
            {0xf5d4, 0x35001ce0, 0xd503201f},
            {0xfa24, 0x34ffe200, 0x17ffff10},
            {0xfb44, 0x34000240, 0x14000012},
            {0xfb90, 0x97fff24c, 0x52800000},
            {0xfd7c, 0x97fff315, 0x52800000},
            {0x103b0, 0xb40012c0, 0xd503201f},
            {0x10400, 0x52800142, 0x52800022},
            {0x10404, 0x52827101, 0x52800021},
            {0x1040c, 0x97fff13d, 0x52800020},
            {0x1233c, 0x97ffe92d, 0xd503201f},
            {0x13e2c, 0x52800001, 0x14000010},
            {0x1d070, 0xa9bf7bfd, 0x52804020},
            {0x1d074, 0x910003fd, 0xd65f03c0},
            {0x279f8, 0x35000500, 0x52820001},
            {0x279fc, 0xb9403fa1, 0xb90002c1},
            {0x27a28, 0xf94023a5, 0xd2b18005},
            {0x27a4c, 0x9400030d, 0x94000023},
            {0x33658, 4, 7}};

    for (const auto &[at, before, after] : words) {
        need(field(result, at, 4) == before, "QNX_STARTUPMGR_INSTRUCTION");
        put(result, at, after, 4);
    }

    need(field(result, 0x14188, 4) == 0xa9bd7bfd && stub.size() == 140, "QNX_STARTUPMGR_STUB_SLOT");
    result.replace(0x14188, stub.size(), stub);
    constexpr std::array<std::uint32_t, 7> helper{0x52820001, 0xb90002c1, 0xd2b18006, 0x914004c6,
                                                  0xf90002a6, 0x52800000, 0xd65f03c0};
    need(field(result, 0x27ad8, 4) == 0xd1000442, "QNX_STARTUPMGR_HELPER_SLOT");

    for (unsigned i = 0; i < helper.size(); ++i) {
        put(result, 0x27ad8 + i * 4, helper[i], 4);
    }

    // RELA addends: RAM dispatch, absent hardware launch vectors, host renderer.
    constexpr std::array<std::uint32_t, 3> relas[]{
            std::array<std::uint32_t, 3>{0x8c78, 0x43158, 0x43188},
            {0x8cc0, 0x43008, 0x43010},
            {0x8d98, 0x43100, 0x43108},
            {0x8f00, 0x43158, 0x43188},
            {0x8f48, 0x43008, 0x43010},
            {0x9020, 0x43100, 0x43108},
            {0xa010, 0x1cf70, 0x14188},
            {0xa028, 0x1d040, 0x1d064},
            {0xa0e8, 0x1d06c, 0x1d064},
            {0xa100, 0x1cfac, 0x1d064},
            {0xa130, 0x1d094, 0x1d064},
            {0xa178, 0x1d044, 0x1d064},
            {0xa1d8, 0x1cf6c, 0x1d064},
            {0xa1f0, 0x1d0bc, 0x1d064},
            {0xa268, 0x1d024, 0x1d064},
            {0xa280, 0x1cfe8, 0x14188},
            {0xa2c8, 0x1d038, 0x1d064},
            {0xa310, 0x1cf1c, 0x1d064},
            {0xa328, 0x1cf44, 0x1d064},
            {0xa358, 0x41948, 0x418e0},
            {0xa370, 0x418e0, 0x41948},
            {0xa5b0, 0x41810, 0x417a8},
            {0xa5c8, 0x417a8, 0x41810},
            {0xa688, 0x42440, 0x424a8},
            {0xa6a0, 0x424a8, 0x42510},
            {0xa6b8, 0x42510, 0x41dc0},
            {0xa6d0, 0x41dc0, 0x425e0},
            {0xa6e8, 0x426b0, 0x41740},
            {0xa700, 0x42578, 0x41878},
            {0xa718, 0x425e0, 0x42440},
            {0xa730, 0x41e90, 0x426b0},
            {0xa748, 0x41e28, 0x42578},
            {0xa760, 0x42718, 0x41e90},
            {0xa778, 0x41740, 0x41e28},
            {0xa790, 0x41878, 0x42718},
            {0xac40, 0x2ae20, 0x2ae2a},
            {0xaf28, 0x2b199, 0x2b0b1}};

    for (const auto &[at, before, after] : relas) {
        need(field(result, at - 8, 8) == 1027 && field(result, at, 8) == before,
             "QNX_STARTUPMGR_RELOCATION");
        put(result, at, after, 8);
    }

    replace(result, "/dev/disk/uda5\0"sv, "/dev/hd0\0"sv, 1, true);
    replace(result, "devb-ufs-qualcomm\0"sv, "devb-ram\0"sv, 1, true);
    replace(result, "name=/ram/dma\0"sv, "name=\0"sv, 1, true);
    replace(result, "ufs\0ioport=0x1D84000,irq=297,pm=2000:1000,bs=pm\0"sv,
            "ram\0capacity=2097250,address=0xC0000000\0"sv, 1, true);
    replace(result, "/lib64/graphics.conf\0openwfd_splash\0"sv,
            "/proc/boot/lib64/graphics.conf\0pipe\0"sv, 1, true);
    replace(result, "active_gpt_header\0"sv, "gvm_ion_audio_mem\0"sv, 1);
    replace(result, "active_gpt_array\0"sv, "RESET_INFO_QNX_1\0"sv, 1);
    digest(result, "ifs_display.img\0"sv, display);
    digest(result, "ifs_disk.img\0"sv, disk);

    return result;
}
} // namespace voyemu
