#include "qnx_startup.hpp"
#include "check.hpp"
#include "error.hpp"
#include "model.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>
namespace voyemu {
namespace {
struct Site {
    std::size_t offset;
    std::uint32_t before, after;
};
constexpr std::array sites{
        Site{0x3c54, 0xd503201f, 0xf9400403},
        Site{0x3c58, 0xd503201f, 0xd360fc63},
        Site{0x3c5c, 0xd503201f, 0xd65f03c0},
        Site{0x9f10, 0xb9400c03, 0x97ffe751},
        Site{0x9f34, 0xb9400e61, 0xf9400661},
        Site{0x9f3c, 0xeb21405f, 0xeb41805f},
        Site{0x325c, 0x94007f85, 0x52802d40},
        Site{0x4400, 0x94007b1c, 0x52802d40},
        Site{0x4b08, 0x9400795a, 0x52802d40},
        Site{0x33f0, 0x3400043a, 0x14000021},
        Site{0x2296c, 0x54fffee3, 0xd503201f},
        Site{0x5088, 0x52800262, 0x52800362},
        Site{0x4b44, 0xd2a2f4c1, 0xd2a10141},
        Site{0x4b48, 0xd2a2f400, 0xd2a10000},
        Site{0x4c64, 0xd2a2f4c1, 0xd2a10141},
        Site{0x4c68, 0xd2a2f400, 0xd2a10000},
        Site{0x4cc0, 0xd2a2f4c1, 0xd2a10141},
        Site{0x4cc4, 0xd2a2f400, 0xd2a10000},
        Site{0x59dc, 0x35000ee0, 0xf2a126f7},
        Site{0x59e0, 0xf9404ff7, 0xd503201f},
        Site{0x59d8, 0x94003200, 0xd2980017},
        Site{0x59f8, 0x94003621, 0x52800000},
        Site{0x5af4, 0x940035e8, 0x9523f4c3},
        Site{0x34d4, 0x9400091b, 0x94000000 | (((0x8a000008 - 0x857006d4) >> 2) & 0x3ffffff)},
        Site{0x13e18, 0x52848000, 0x52800000}};
constexpr std::size_t start = 0x3000, extent = 0x57108, last = start + extent - 4;

constexpr CodeCheck need{"QNX startup image differs"};

std::uint32_t word(std::string_view data, std::size_t offset, bool big = false) {
    need(offset + 4 <= data.size(), "QNX_STARTUP_BOUNDS");
    std::uint32_t value = 0;

    for (unsigned i = 0; i < 4; ++i) {
        value |= std::uint32_t(static_cast<unsigned char>(data[offset + i]))
                 << ((big ? 3 - i : i) * 8);
    }

    return value;
}

void put(std::string &data, std::size_t offset, std::uint32_t value, bool big = false) {
    need(offset + 4 <= data.size(), "QNX_STARTUP_BOUNDS");

    for (unsigned i = 0; i < 4; ++i) {
        data[offset + i] = static_cast<char>(value >> ((big ? 3 - i : i) * 8));
    }
}
} // namespace

void qnx_a11_cpu_name_guard(std::string_view original) {
    need(original.size() >= 0x60c98 && original.size() <= 16 * 1024 * 1024,
         "QNX_A11_CPU_NAME_BOUNDS");
    constexpr std::array guards{Site{0x4ef8, 0x9400832e, 0},  Site{0x4efc, 0xaa0003f6, 0},
                                Site{0x4f14, 0xd5380019, 0},  Site{0x4f1c, 0xb8216859, 0},
                                Site{0x4f34, 0xf00002d4, 0},  Site{0x4f38, 0xf9474a94, 0},
                                Site{0x4f3c, 0xf9400293, 0},  Site{0x4f40, 0xb4000173, 0},
                                Site{0x4f44, 0x91002294, 0},  Site{0x4f48, 0xaa1603e1, 0},
                                Site{0x4f50, 0x9400965c, 0},  Site{0x4f64, 0xf8408693, 0},
                                Site{0x4f68, 0xb5ffff13, 0},  Site{0x4f6c, 0x90000140, 0},
                                Site{0x4f70, 0x91374000, 0},  Site{0x4f74, 0xaa0003f4, 0},
                                Site{0x4f78, 0x94002a22, 0},  Site{0x4f7c, 0x5280bb81, 0},
                                Site{0x4f80, 0x79003ae0, 0},  Site{0x4f84, 0xb90006e1, 0},
                                Site{0x4f88, 0xd53800a2, 0},  Site{0x25bbc, 0xb9400000, 0},
                                Site{0x25bc4, 0x97ffff9f, 0}, Site{0x25bc8, 0xb40000a0, 0},
                                Site{0x25bdc, 0xd2800000, 0}, Site{0x25be0, 0xa8c17bf3, 0},
                                Site{0x25be4, 0xd65f03c0, 0}};

    for (const auto &site : guards) {
        need(word(original, site.offset) == site.before, "QNX_A11_CPU_NAME_INSTRUCTION");
    }

    need(word(original, 0x60c90) == 0x8575dba0 && word(original, 0x60c94) == 0 &&
                 original.substr(0x2dbd0, 8) == std::string_view("Unknown\0", 8) &&
                 word(original, 0x609a0) == 0x8575db00 && word(original, 0x609a4) == 0 &&
                 word(original, 0x60900) == 0x8572acf8 && word(original, 0x60904) == 0 &&
                 original.substr(0x2daf8, 8) == std::string_view("SA6155P\0", 8),
         "QNX_A11_CPU_NAME_FALLBACK");
    // Missing optional SMEM137 name takes the OEM Unknown path; valid names retain the original
    // table pointer and loop.
}

std::uint64_t qnx_a11_gic_device_mapping(std::string_view original) {
    need(original.size() >= 0x64e00 && original.size() <= 16 * 1024 * 1024,
         "QNX_A11_GIC_MAPPING_BOUNDS");
    auto wide = [&](std::size_t at) {
        return std::uint64_t(word(original, at)) | (std::uint64_t(word(original, at + 4)) << 32);
    };
    constexpr std::array guards{Site{0x4234, 0xb0000202, 0}, Site{0x4238, 0x91040042, 0},
                                Site{0x4284, 0xd1040043, 0}, Site{0x42a8, 0x9000030c, 0},
                                Site{0x42ac, 0x9100018d, 0}, Site{0x42f4, 0xf9000183, 0},
                                Site{0x54c8, 0xd2a2f4c1, 0}, Site{0x54cc, 0xd2a2f400, 0},
                                Site{0x54e4, 0x940015ef, 0}, Site{0xacf0, 0xb9400775, 0}};

    for (const auto &site : guards) {
        need(word(original, site.offset) == site.before, "QNX_A11_GIC_INSTRUCTION");
    }

    need(wide(0x5cc78) == 0xff000044eeaa0400ULL && wide(0x5cc68) == 0x85000519 &&
                 wide(0x453e8) == 0x0060000017a00401ULL,
         "QNX_A11_GIC_DEVICE_ATTRIBUTES");
    need(wide(0x63e00) == 0 && wide(0x44ff8) == 0 && wide(0x45000) == 0 && wide(0x45008) == 0,
         "QNX_A11_GIC_MAPPING_SOURCE");

    // SMP8: GICD08000000..0800ffff and actual GICR080a0000..0819ffff.
    // Device Attr0, AF and PXN/UXN match the adjacent OEM Device descriptor.
    return 0x0060000008000401ULL;
}

void qnx_a11_boot_storage_query_guard(std::string_view original) {
    need(original.size() >= 0x60958 && original.size() <= 16 * 1024 * 1024,
         "QNX_A11_BOOT_QUERY_BOUNDS");
    constexpr std::array guards{Site{0x13058, 0xf0000281, 0}, Site{0x1305c, 0xf9417421, 0},
                                Site{0x13060, 0xf9504021, 0}, Site{0x13064, 0xb9401021, 0},
                                Site{0x13068, 0xb9000001, 0}, Site{0x1306c, 0x52800000, 0},
                                Site{0x13070, 0xd65f03c0, 0}, Site{0x13110, 0xa9be7bfd, 0},
                                Site{0x1311c, 0xaa0003f3, 0}, Site{0x13120, 0xf0000280, 0},
                                Site{0x13124, 0xf9417400, 0}, Site{0x13128, 0xf9504400, 0},
                                Site{0x1312c, 0x940004e8, 0}, Site{0x13138, 0xb900027f, 0},
                                Site{0x13144, 0xb9000260, 0}, Site{0x13148, 0x52800000, 0},
                                Site{0x1314c, 0xf9400bf3, 0}, Site{0x13150, 0xa8c27bfd, 0},
                                Site{0x13154, 0xd65f03c0, 0}, Site{0x144cc, 0xb9401000, 0},
                                Site{0x144d0, 0x6b1f001f, 0}, Site{0x130f0, 0x528000c0, 0},
                                Site{0x131c8, 0x528000c0, 0}, Site{0x131e4, 0xf0000281, 0},
                                Site{0x131e8, 0xf9017820, 0}, Site{0x13aa4, 0xf0000281, 0},
                                Site{0x13aa8, 0xf9417821, 0}, Site{0x13aac, 0xf9400021, 0},
                                Site{0x13ab0, 0xd61f0020, 0}, Site{0x66c0, 0xb9007fff, 0},
                                Site{0x66dc, 0x9101f3e0, 0},  Site{0x66f8, 0x940034eb, 0},
                                Site{0x66fc, 0x94003316, 0},  Site{0x6708, 0xb9407fe0, 0},
                                Site{0x13ab8, 0xd2800000, 0}, Site{0x13ac0, 0x97fffff9, 0},
                                Site{0x13ac4, 0x6b1f001f, 0}, Site{0x13acc, 0x1a9f17e0, 0},
                                Site{0x13b18, 0x9100d3a0, 0}, Site{0x13b1c, 0x97ffffe2, 0},
                                Site{0x13b4c, 0x35000620, 0}, Site{0x13c7c, 0x9100d3a0, 0},
                                Site{0x13c80, 0x97ffff89, 0}, Site{0x13cb0, 0x35000620, 0},
                                Site{0x13e60, 0x912ce294, 0}, Site{0x13e68, 0xf9000714, 0},
                                Site{0x13e70, 0x912ce294, 0}, Site{0x13e78, 0x91004294, 0},
                                Site{0x13e88, 0xf90006f4, 0}, Site{0x13fd0, 0x910193a0, 0},
                                Site{0x13fd4, 0x97fffeb4, 0}, Site{0x13fd8, 0x350001a0, 0},
                                Site{0x13fdc, 0xb94067a0, 0}, Site{0x1400c, 0x52800000, 0}};

    for (const auto &site : guards) {
        need(word(original, site.offset) == site.before, "QNX_A11_BOOT_QUERY_INSTRUCTION");
    }

    auto wide = [&](std::size_t at) {
        return std::uint64_t(word(original, at)) | (std::uint64_t(word(original, at + 4)) << 32);
    };
    need(wide(0x60938) == 0x85710310 && wide(0x60940) == 0x85710e38 &&
                 wide(0x60948) == 0x85710258 && wide(0x60950) == 0x85710cd4,
         "QNX_A11_BOOT_QUERY_TABLE");
}

std::uint32_t qnx_a11_misc_failure_guard(std::string_view original) {
    need(original.size() >= 0x14c64 && original.size() <= 16 * 1024 * 1024, "QNX_A11_MISC_BOUNDS");
    constexpr std::array guards{Site{0x13158, 0xa9bd7bfd, 0}, Site{0x13160, 0xa90153f3, 0},
                                Site{0x13164, 0xaa0003f3, 0}, Site{0x13168, 0xf9504014, 0},
                                Site{0x1316c, 0xf90013f5, 0}, Site{0x13170, 0xf9501015, 0},
                                Site{0x13174, 0x940006ac, 0}, Site{0x13178, 0x2a0003e0, 0},
                                Site{0x1317c, 0xd2840002, 0}, Site{0x13180, 0xaa1303e1, 0},
                                Site{0x13184, 0x9b157c03, 0}, Site{0x13188, 0xf9400280, 0},
                                Site{0x1318c, 0x9400094b, 0}, Site{0x131c8, 0x528000c0, 0},
                                Site{0x131d4, 0xa94153f3, 0}, Site{0x131d8, 0xf94013f5, 0},
                                Site{0x131dc, 0xa8c37bfd, 0}, Site{0x131e0, 0xd65f03c0, 0},
                                Site{0x14c54, 0xb9488a60, 0}, Site{0x14c58, 0xf9400bf3, 0},
                                Site{0x14c5c, 0xa8c27bfd, 0}, Site{0x14c60, 0xd65f03c0, 0},
                                Site{0x133f8, 0x97ffff58, 0}, Site{0x133fc, 0x6b1f001f, 0},
                                Site{0x13400, 0x9a9f0260, 0}, Site{0x13594, 0x97fffef1, 0},
                                Site{0x13598, 0x35000140, 0}};

    for (const auto &site : guards) {
        need(word(original, site.offset) == site.before, "QNX_A11_MISC_INSTRUCTION");
    }

    // The exact block-size callee ends with LDR W0: X0 is already zero-extended.
    // CBZ X20 takes the existing error=6 epilogue; non-NULL storage reads are intact.
    return 0xb4000294;
}

std::uint64_t qnx_a11_ram_identity_mapping(std::string_view original) {
    need(original.size() >= 0x69108 && original.size() <= 16 * 1024 * 1024,
         "QNX_A11_RAM_MAPPING_BOUNDS");
    auto wide = [&](std::size_t at) {
        return std::uint64_t(word(original, at)) | (std::uint64_t(word(original, at + 4)) << 32);
    };
    need(word(original, 0x3000) == 0x00ff7eeb && word(original, 0x300c) == 0x85701000 &&
                 word(original, 0x3014) == 0x85700200 && word(original, 0x3020) == 0x66108,
         "QNX_A11_RAM_MAPPING_LAYOUT");
    // Guard the chip-id table selection, unchanged OEM constructor, and TTBR/TCR writes.
    constexpr std::array<std::array<std::uint32_t, 2>, 19> instructions{{{0x3fc0, 0x940086c0},
                                                                         {0x3fd0, 0x90000141},
                                                                         {0x3fd4, 0x91011021},
                                                                         {0x3fd8, 0x38604820},
                                                                         {0x3fdc, 0x10000061},
                                                                         {0x3fe0, 0x8b208820},
                                                                         {0x3fe4, 0xd61f0000},
                                                                         {0x4088, 0x94000062},
                                                                         {0x4040, 0xd5182041},
                                                                         {0x4044, 0xd5182000},
                                                                         {0x42a8, 0x9000030c},
                                                                         {0x42ac, 0x9100018d},
                                                                         {0x42f4, 0xf9000183},
                                                                         {0x42fc, 0xf90009ab},
                                                                         {0x423c, 0x9000020b},
                                                                         {0x4240, 0x9100016b},
                                                                         {0x427c, 0xf900bd63},
                                                                         {0x35e0, 0x94000c24},
                                                                         {0x6854, 0x94003d25}}};

    for (const auto &site : instructions) {
        need(word(original, site[0]) == site[1], "QNX_A11_RAM_MAPPING_INSTRUCTION");
    }

    need(static_cast<unsigned char>(original[0x2be5b]) == 40 && wide(0x60d18) == 0x85759e68 &&
                 wide(0x60e08) == 0x85759e78,
         "QNX_A11_RAM_MAPPING_SELECTION");
    need(wide(0x5cc68) == 0x85000519 && wide(0x5cc78) == 0xff000044eeaa0400ULL,
         "QNX_A11_RAM_MAPPING_ATTRIBUTES");
    need(std::all_of(original.begin() + 0x63e00, original.begin() + 0x64e00,
                     [](char c) {
                         return c == 0;
                     }),
         "QNX_A11_RAM_MAPPING_ROOT");
    need(wide(0x43e00) == 0x8000041d && wide(0x43f58) == 0x8560041d && wide(0x44600) == 0xa000041d,
         "QNX_A11_RAM_MAPPING_ADJACENT_RAM");
    // The pinned own restore executes at8a000000 and copies16KiB B→A before its tail-call.
    // The OEM constructor updates only L2slot47, preserving these two file-backed slots.
    need(wide(0x44080) == 0 && wide(0x44a00) == 0, "QNX_A11_RESTORE_RAM_MAPPING_SOURCE");

    // Same Normal-WB Attr7/AF/AP/SH bits as OEM RAM, with a 1GiB identity block.
    return 0x4000041d;
}

std::string prepare_qnx_startup(std::string_view original, std::string_view pin) {
    need(original.size() >= start + extent && original.size() <= 16 * 1024 * 1024,
         "QNX_STARTUP_BOUNDS");
    need(sha256_text(pin) && sha256_bytes(original) == pin, "QNX_STARTUP_INPUT_SHA");

    for (const auto &site : sites) {
        need(word(original, site.offset) == site.before, "QNX_STARTUP_INCOMPATIBLE_SITE");
    }

    need(original.substr(0x2c948, 16) == std::string_view("ifs2@0xA0000000\0", 16) &&
                 word(original, 0x2c968, true) == 0xa0000000,
         "QNX_STARTUP_IFS_NODE");
    need(original.substr(0x2cdd8, 4) == "gvm_" && original.substr(0x2c8e8, 4) == "RESE",
         "QNX_STARTUP_GPT_NODES");
    std::string output(original);

    for (const auto &site : sites) {
        put(output, site.offset, site.after);
    }

    for (const auto &[offset, address, size] : std::array<std::array<std::uint32_t, 3>, 2>{
                 {{0x2ce04, 0x8c000000, 0x1000}, {0x2c914, 0x8c001000, 0x4000}}}) {
        put(output, offset, 0, true);
        put(output, offset + 4, address, true);
        put(output, offset + 8, size, true);
    }

    put(output, last, 0);
    std::uint32_t sum = 0;

    for (std::size_t offset = start; offset < last; offset += 4) {
        sum += word(output, offset);
    }

    put(output, last, 0 - sum);
    sum = 0;

    for (std::size_t offset = start; offset <= last; offset += 4) {
        sum += word(output, offset);
    }

    need(sum == 0, "QNX_STARTUP_CHECKSUM");

    return output;
}

static std::string qinggan11_startup(std::string_view original, std::string_view pin, bool older) {
    const std::string_view expected =
            older ? "dbb3bc2c6f9b0308e38627cf6a3e3b6757ba98c4f5413334526c26d812ab08f0"
                  : "a476a1e3d3aaa51d855d2d91c412a962efb6ef1e3e63540ec739779d35ce3ce8";

    need(pin == expected && original.size() == 7340032 && sha256_bytes(original) == expected,
         "QNX_QINGGAN11_STARTUP_SHA");
    need(word(original, 0x3000) == 0x00ff7eeb && word(original, 0x300c) == 0x85701000 &&
                 word(original, 0x3014) == 0x85700200 && word(original, 0x3020) == 0x66108 &&
                 word(original, 0x3024) == (older ? 0x2d46d0 : 0x2d45a8) &&
                 word(original, 0x302c) == 6704052,
         "QNX_QINGGAN11_STARTUP_LAYOUT");

    if (older) {
        need(word(original, 0x32d9c, true) == 0x095dc000, "QNX_QINGGAN11_611_STARTUP_FDT_SIZE");
    }

    constexpr std::array local_sites{
            Site{0x322c, 0x94008a25, 0x52802d40},
            Site{0x339c, 0x940089c9, 0x52802d40},
            Site{0x3ec4, 0x940086ff, 0x52802d40},
            Site{0x3fc0, 0x940086c0, 0x52802d40},
            Site{0x4d20, 0x94008368, 0x52802d40},
            Site{0x5488, 0x9400818e, 0x52802d40},
            Site{0x3510, 0x34000377, 0x1400001b},
            Site{0x253bc, 0x54fffee3, 0xd503201f},
            Site{0x54c8, 0xd2a2f4c1, 0xd2a10141},
            Site{0x54cc, 0xd2a2f400, 0xd2a10000},
            Site{0x5608, 0xd2a2f4c1, 0xd2a10141},
            Site{0x560c, 0xd2a2f400, 0xd2a10000},
            Site{0x5664, 0xd2a2f4c1, 0xd2a10141},
            Site{0x5668, 0xd2a2f400, 0xd2a10000},
            Site{0x5bd8, 0x52800263, 0x52800363},
            Site{0x44b4, 0xd503201f, 0xf9400661},
            Site{0x44b8, 0xd503201f, 0xd360fc21},
            Site{0x44bc, 0xd503201f, 0xd65f03c0},
            Site{0xa860, 0xb9400e61,
                 0x94000000U | (static_cast<std::uint32_t>((0x44b4 - 0xa860) / 4) & 0x3ffffffU)},
            Site{0xa898, 0xb9400e60, 0xf9400660},
            Site{0xa89c, 0xeb20405f, 0xeb40805f},
            Site{0x6720, 0x94003712, 0xd2a20016},
            Site{0x6724, 0x35001320, 0xd503201f},
            Site{0x6730, 0xf94057f6, 0xd503201f},
            Site{0x6754, 0x94003d5f, 0x52800000},
            Site{0x35e0, 0x94000c24,
                 0x94000000U | (static_cast<std::uint32_t>((0x8a000008ULL - 0x857007e0ULL) / 4) &
                                0x3ffffffU)},
            Site{0x6854, 0x94003d25,
                 0x94000000U | (static_cast<std::uint32_t>((0x8a000000ULL - 0x85703a54ULL) / 4) &
                                0x3ffffffU)},
            Site{0x1677c, 0x52848000, 0x52800000}};

    for (const auto &site : local_sites) {
        need(word(original, site.offset) == site.before, "QNX_QINGGAN11_STARTUP_INSTRUCTION");
    }

    const auto ram = qnx_a11_ram_identity_mapping(original);
    const auto misc = qnx_a11_misc_failure_guard(original);
    qnx_a11_boot_storage_query_guard(original);
    const auto device = qnx_a11_gic_device_mapping(original);
    qnx_a11_cpu_name_guard(original);
    need(original.substr(0x33268, 4) == "gvm_" && original.substr(0x32c28, 4) == "RESE" &&
                 original.substr(0x32d74, 16) == std::string_view("ifs2@0xA0000000\0", 16),
         "QNX_QINGGAN11_STARTUP_FDT_NODE");
    need(word(original, 0x3328c, true) == 16 && word(original, 0x32c4c, true) == 16 &&
                 word(original, 0x32d90, true) == 0 && word(original, 0x32d94, true) == 0xa0000000,
         "QNX_QINGGAN11_STARTUP_FDT_LAYOUT");
    std::uint32_t sum = 0;

    for (std::size_t at = 0x3000; at < 0x69108; at += 4) {
        sum += word(original, at);
    }

    need(sum == 0, "QNX_QINGGAN11_STARTUP_CHECKSUM");
    std::string output(original);

    for (const auto &site : local_sites) {
        put(output, site.offset, site.after);
    }

    for (const auto &[offset, before_low, address, size] :
         std::array<std::array<std::uint32_t, 4>, 2>{
                 {{0x33294, 0, 0x8c000000, 0x1000}, {0x32c54, 0x01f00000, 0x8c001000, 0x4000}}}) {
        need(word(original, offset, true) == 1 && word(original, offset + 4, true) == before_low &&
                     word(original, offset + 8, true) == 0,
             "QNX_QINGGAN11_STARTUP_FDT_REG");
        put(output, offset, 0, true);
        put(output, offset + 4, address, true);
        put(output, offset + 8, 0, true);
        put(output, offset + 12, size, true);
    }

    put(output, 0x4f34, 0xb40001d6);
    put(output, 0x4f38, 0x582dead4);
    put(output, 0x45000, static_cast<std::uint32_t>(device));
    put(output, 0x45004, static_cast<std::uint32_t>(device >> 32));
    put(output, 0x60938, 0x8a000080);
    put(output, 0x6093c, 0);
    put(output, 0x60948, 0x8a0000c0);
    put(output, 0x6094c, 0);
    put(output, 0x13178, misc);
    put(output, 0x44080, 0x8a00041d);
    put(output, 0x44084, 0);
    put(output, 0x44a00, 0xb000041d);
    put(output, 0x44a04, 0);
    put(output, 0x63e08, static_cast<std::uint32_t>(ram));
    put(output, 0x63e0c, static_cast<std::uint32_t>(ram >> 32));
    put(output, 0x69104, 0);
    sum = 0;

    for (std::size_t at = 0x3000; at < 0x69104; at += 4) {
        sum += word(output, at);
    }

    put(output, 0x69104, 0 - sum);

    return output;
}

std::string prepare_qnx_qinggan11_startup(std::string_view original, std::string_view pin) {
    return qinggan11_startup(original, pin, false);
}

std::string prepare_qnx_qinggan11_611_startup(std::string_view original, std::string_view pin) {
    return qinggan11_startup(original, pin, true);
}

static std::string qinggan11_kernel(std::string_view original, std::string_view pin,
                                    std::string_view packed, bool older) {
    auto output = qinggan11_startup(original, pin, older);
    auto wide = [&](std::size_t at) {
        return std::uint64_t(word(original, at)) | (std::uint64_t(word(original, at + 4)) << 32);
    };

    need(original.substr(0, 6) == std::string_view("\x7f"
                                                   "ELF\x02\x01",
                                                   6) &&
                 wide(0x20) == 0x40 && word(original, 0x34) == 0x00380040 &&
                 word(original, 0x38) == 0x00400003,
         "QNX_QINGGAN11_ELF_HEADER");
    need(word(original, 0xb0) == 1 && word(original, 0xb4) == 7 && wide(0xb8) == 0x3000 &&
                 wide(0xc0) == 0x85700200 && wide(0xc8) == 0x85700200 &&
                 wide(0xd0) == (older ? 0x2d46d0 : 0x2d45a8) &&
                 wide(0xd8) == (older ? 0x2d46d0 : 0x2d45a8) && wide(0xe0) == 0x200 &&
                 wide(0x90) == 0x859d5000,
         "QNX_QINGGAN11_ELF_SEGMENT");
    const auto declared = std::uint64_t(0x66108) + packed.size();
    need(declared <= wide(0x90) - wide(0xc8), "QNX_QINGGAN11_ELF_SEGMENT_OVERLAP");
    // QEMU loads PT_LOAD p_filesz bytes: keep the complete grown payload inside both ELF extents.
    const auto extent = static_cast<std::uint32_t>(std::max<std::uint64_t>(wide(0xd0), declared));
    put(output, 0xd0, extent);
    put(output, 0xd8, extent);
    constexpr std::size_t stream = 0x69108;
    const std::size_t end = older ? 0x2d76d0 : 0x2d75a8;
    need(!packed.empty() && packed.size() % 4 == 0 && packed.size() <= original.size() - stream &&
                 std::all_of(original.begin() + end, original.end(),
                             [](char c) {
                                 return static_cast<unsigned char>(c) == 255;
                             }),
         "QNX_QINGGAN11_KERNEL_STREAM_BOUNDS");
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < packed.size(); at += 4) {
        sum += word(packed, at);
    }

    need(sum == 0, "QNX_QINGGAN11_KERNEL_STREAM_CHECKSUM");
    std::fill(output.begin() + stream, output.end(), static_cast<char>(255));
    output.replace(stream, packed.size(), packed);
    put(output, 0x3024, static_cast<std::uint32_t>(stream - 0x3000 + packed.size()));
    put(output, 0x69104, 0);
    sum = 0;

    for (std::size_t at = 0x3000; at < 0x69104; at += 4) {
        sum += word(output, at);
    }

    put(output, 0x69104, 0 - sum);

    return output;
}

std::string assemble_qnx_qinggan11_kernel(std::string_view original, std::string_view pin,
                                          std::string_view packed) {
    return qinggan11_kernel(original, pin, packed, false);
}

std::string assemble_qnx_qinggan11_611_kernel(std::string_view original, std::string_view pin,
                                              std::string_view packed) {
    return qinggan11_kernel(original, pin, packed, true);
}

std::string assemble_qnx_kernel(std::string_view original, std::string_view pin,
                                std::string_view packed) {
    auto output = prepare_qnx_startup(original, pin);
    constexpr std::size_t stream = 0x5a108;
    const auto end = std::uint64_t(word(original, 0x3024)) + 0x3000;

    need(end >= stream && end <= original.size() && packed.size() > 0 &&
                 packed.size() <= original.size() - stream && packed.size() % 4 == 0,
         "QNX_KERNEL_STREAM_BOUNDS");
    need(std::all_of(original.begin() + static_cast<std::ptrdiff_t>(end), original.end(),
                     [](char c) {
                         return static_cast<unsigned char>(c) == 255;
                     }),
         "QNX_KERNEL_ERASED_TAIL");
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < packed.size(); at += 4) {
        sum += word(packed, at);
    }

    need(sum == 0, "QNX_KERNEL_STREAM_CHECKSUM");
    std::fill(output.begin() + stream, output.end(), static_cast<char>(255));
    output.replace(stream, packed.size(), packed);
    put(output, 0x3024, static_cast<std::uint32_t>(stream - 0x3000 + packed.size()));
    put(output, last, 0);
    sum = 0;

    for (std::size_t at = start; at < last; at += 4) {
        sum += word(output, at);
    }

    put(output, last, 0 - sum);

    return output;
}
} // namespace voyemu
