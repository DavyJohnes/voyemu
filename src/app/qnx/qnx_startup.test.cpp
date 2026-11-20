#include "qnx_startup.hpp"
#include "model.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>
using namespace voyemu;
namespace {
void cpu_name_fixture(std::string_view original, std::string_view changed) {
    auto need = [](bool yes) {
        if (!yes) {
            throw std::runtime_error("A11 CPU name fallback fixture");
        }
    };
    auto word = [](std::string_view b, std::size_t at) {
        std::uint32_t n = 0;

        for (unsigned i = 0; i < 4; ++i) {
            n |= std::uint32_t(static_cast<unsigned char>(b[at + i])) << (8 * i);
        }

        return n;
    };

    qnx_a11_cpu_name_guard(original);
    const auto branch = word(changed, 0x4f34), load = word(changed, 0x4f38);
    need(branch == 0xb40001d6 && load == 0x582dead4 && (branch & 31) == 22 && (load & 31) == 20);
    auto offset = [](std::uint32_t w) {
        const auto n = (w >> 5) & 0x7ffff;

        return std::int64_t(n & 0x40000 ? std::int64_t(n) - 0x80000 : n) * 4;
    };
    need(0x85702134 + offset(branch) == 0x8570216c && 0x85702138 + offset(load) == 0x8575de90);

    for (const auto name : {0ULL, 0x8572acf8ULL}) {
        std::uint64_t regs[32];

        for (unsigned i = 0; i < 32; ++i) {
            regs[i] = 0x1000 + i;
        }

        regs[22] = name;
        const auto old20 = regs[20];
        auto pc = std::uint64_t(0x85702134);
        need((branch & 0xff000000) == 0xb4000000);
        pc = regs[branch & 31] ? pc + 4 : pc + offset(branch);

        if (pc == 0x85702138) {
            need((load & 0xff000000) == 0x58000000);
            const auto address = pc + offset(load);
            need(address == 0x8575de90);
            regs[load & 31] = word(original, 0x60c90) |
                              (std::uint64_t(word(original, 0x60c94)) << 32);
            pc += 4;
        }

        need(pc == (name ? 0x8570213cULL : 0x8570216cULL) &&
             regs[20] == (name ? 0x8575dba0ULL : old20));

        for (unsigned i = 0; i < 32; ++i) {
            if (i != 20) {
                need(regs[i] == (i == 22 ? name : 0x1000 + i));
            }
        }
    }

    need(changed.substr(0x4f3c, 0x34) == original.substr(0x4f3c, 0x34) &&
         changed.substr(0x60c90, 8) == original.substr(0x60c90, 8));

    for (const std::size_t at :
         {0x4ef8,  0x4efc,  0x4f14,  0x4f1c,  0x4f34,  0x4f38,  0x4f3c,  0x4f40,
          0x4f44,  0x4f48,  0x4f50,  0x4f64,  0x4f68,  0x4f6c,  0x4f70,  0x4f74,
          0x4f78,  0x4f7c,  0x4f80,  0x4f84,  0x4f88,  0x25bbc, 0x25bc4, 0x25bc8,
          0x25bdc, 0x25be0, 0x25be4, 0x60c90, 0x609a0, 0x60900, 0x2daf8, 0x2dbd0}) {
        auto bad = std::string(original);
        bad[at] ^= 1;
        bool rejected = false;
        try {
            qnx_a11_cpu_name_guard(bad);
        } catch (...) {
            rejected = true;
        }

        need(rejected);
    }

    auto twice = std::string(original);
    twice.replace(0x4f34, 8, changed.substr(0x4f34, 8));
    bool rejected = false;
    try {
        qnx_a11_cpu_name_guard(twice);
    } catch (...) {
        rejected = true;
    }

    need(rejected);
}

void gic_device_fixture(std::string_view original, std::string_view changed) {
    auto need = [](bool yes) {
        if (!yes) {
            throw std::runtime_error("A11 GIC Device fixture");
        }
    };
    auto get = [](std::string_view s, std::size_t at) {
        std::uint64_t n = 0;

        for (unsigned i = 0; i < 8; ++i) {
            n |= std::uint64_t(static_cast<unsigned char>(s[at + i])) << (8 * i);
        }

        return n;
    };
    auto put = [](std::string &s, std::size_t at, std::uint64_t n) {
        for (unsigned i = 0; i < 8; ++i) {
            s[at + i] = static_cast<char>(n >> (8 * i));
        }
    };
    const auto descriptor = qnx_a11_gic_device_mapping(original);

    need(get(changed, 0x45000) == descriptor && descriptor == 0x0060000008000401ULL);
    need((descriptor & 3) == 1 && ((descriptor >> 2) & 7) == 0 && ((descriptor >> 6) & 3) == 0 &&
         ((descriptor >> 8) & 3) == 0 && ((descriptor >> 10) & 1) == 1 &&
         (descriptor & (3ULL << 53)) == (3ULL << 53) && (get(original, 0x5cc78) & 255) == 0);

    for (std::size_t slot = 0; slot < 512; ++slot) {
        if (slot != 64) {
            need(get(changed, 0x44e00 + slot * 8) == get(original, 0x44e00 + slot * 8));
        }
    }

    auto table = std::string(changed.substr(0x44e00, 4096));
    const auto before = table;

    // Replay the exact constructor's nine root0-L2 writes; slot64 is retained.
    for (const auto &[slot, value] : {std::pair<std::size_t, std::uint64_t>{0, 0x8574741f},
                                      {1, 0x8574841f},
                                      {3, 0x8574941f},
                                      {5, 0x8574a41f},
                                      {14, 0x8574341f},
                                      {15, 0x8574441f},
                                      {97, 0x8574541f},
                                      {163, 0x8574641f},
                                      {190, 0x8574b41f}}) {
        put(table, slot * 8, value);
    }

    need(get(table, 64 * 8) == descriptor && get(table, 63 * 8) == get(original, 0x44ff8) &&
         get(table, 65 * 8) == get(original, 0x45008));

    for (const auto address : {0x08000000ULL, 0x08000004ULL, 0x0800ffffULL, 0x080a0000ULL,
                               0x0819ffffULL, 0x081fffffULL}) {
        need(((address >> 21) & 511) == 64);
        need((descriptor & 0x0000ffffffe00000ULL) + (address & 0x1fffff) == address);
    }

    need(((0x07ffffffULL >> 21) & 511) == 63 && ((0x08200000ULL >> 21) & 511) == 65);

    for (std::size_t slot = 0; slot < 512; ++slot) {
        if (slot != 0 && slot != 1 && slot != 3 && slot != 5 && slot != 14 && slot != 15 &&
            slot != 97 && slot != 163 && slot != 190) {
            need(get(table, slot * 8) == get(before, slot * 8));
        }
    }

    for (const std::size_t at : {0x4234, 0x42f4, 0x54c8, 0x54cc, 0x54e4, 0xacf0, 0x5cc68, 0x5cc78,
                                 0x453e8, 0x63e00, 0x44ff8, 0x45000, 0x45008}) {
        auto bad = std::string(original);
        bad[at] ^= 1;
        bool rejected = false;
        try {
            qnx_a11_gic_device_mapping(bad);
        } catch (...) {
            rejected = true;
        }

        need(rejected);
    }

    auto twice = std::string(original);
    put(twice, 0x45000, descriptor);
    bool rejected = false;
    try {
        qnx_a11_gic_device_mapping(twice);
    } catch (...) {
        rejected = true;
    }

    need(rejected);
}

void boot_query_fixture() {
    auto need = [](bool value) {
        if (!value) {
            throw std::runtime_error("A11 boot query fixture");
        }
    };
    auto put = [](std::string &s, std::size_t at, std::uint64_t n, unsigned count) {
        for (unsigned i = 0; i < count; ++i) {
            s[at + i] = static_cast<char>(n >> (8 * i));
        }
    };
    const std::pair<std::size_t, std::uint32_t> guards[] = {
            {0x13058, 0xf0000281}, {0x1305c, 0xf9417421}, {0x13060, 0xf9504021},
            {0x13064, 0xb9401021}, {0x13068, 0xb9000001}, {0x1306c, 0x52800000},
            {0x13070, 0xd65f03c0}, {0x13110, 0xa9be7bfd}, {0x1311c, 0xaa0003f3},
            {0x13120, 0xf0000280}, {0x13124, 0xf9417400}, {0x13128, 0xf9504400},
            {0x1312c, 0x940004e8}, {0x13138, 0xb900027f}, {0x13144, 0xb9000260},
            {0x13148, 0x52800000}, {0x1314c, 0xf9400bf3}, {0x13150, 0xa8c27bfd},
            {0x13154, 0xd65f03c0}, {0x144cc, 0xb9401000}, {0x144d0, 0x6b1f001f},
            {0x130f0, 0x528000c0}, {0x131c8, 0x528000c0}, {0x131e4, 0xf0000281},
            {0x131e8, 0xf9017820}, {0x13aa4, 0xf0000281}, {0x13aa8, 0xf9417821},
            {0x13aac, 0xf9400021}, {0x13ab0, 0xd61f0020}, {0x66c0, 0xb9007fff},
            {0x66dc, 0x9101f3e0},  {0x66f8, 0x940034eb},  {0x66fc, 0x94003316},
            {0x6708, 0xb9407fe0},  {0x13ab8, 0xd2800000}, {0x13ac0, 0x97fffff9},
            {0x13ac4, 0x6b1f001f}, {0x13acc, 0x1a9f17e0}, {0x13b18, 0x9100d3a0},
            {0x13b1c, 0x97ffffe2}, {0x13b4c, 0x35000620}, {0x13c7c, 0x9100d3a0},
            {0x13c80, 0x97ffff89}, {0x13cb0, 0x35000620}, {0x13e60, 0x912ce294},
            {0x13e68, 0xf9000714}, {0x13e70, 0x912ce294}, {0x13e78, 0x91004294},
            {0x13e88, 0xf90006f4}, {0x13fd0, 0x910193a0}, {0x13fd4, 0x97fffeb4},
            {0x13fd8, 0x350001a0}, {0x13fdc, 0xb94067a0}, {0x1400c, 0x52800000}};
    std::string raw(0x60958, 0);

    for (const auto &[at, word] : guards) {
        put(raw, at, word, 4);
    }

    for (const auto &[at, value] : {std::pair<std::size_t, std::uint64_t>{0x60938, 0x85710310},
                                    {0x60940, 0x85710e38},
                                    {0x60948, 0x85710258},
                                    {0x60950, 0x85710cd4}}) {
        put(raw, at, value, 8);
    }

    qnx_a11_boot_storage_query_guard(raw);

    for (const auto &[at, word] : guards) {
        (void)word;
        auto bad = raw;
        bad[at] ^= 1;
        bool rejected = false;
        try {
            qnx_a11_boot_storage_query_guard(bad);
        } catch (...) {
            rejected = true;
        }

        need(rejected);
    }

    for (const std::size_t at : {0x60938, 0x60940, 0x60948, 0x60950}) {
        auto bad = raw;
        bad[at] ^= 1;
        bool rejected = false;
        try {
            qnx_a11_boot_storage_query_guard(bad);
        } catch (...) {
            rejected = true;
        }

        need(rejected);
    }

    auto twice = raw;
    put(twice, 0x60938, 0x8a000080, 8);
    bool rejected = false;
    try {
        qnx_a11_boot_storage_query_guard(twice);
    } catch (...) {
        rejected = true;
    }

    need(rejected);
}

void ram_mapping_fixture() {
    auto check = [](bool value) {
        if (!value) {
            throw std::runtime_error("A11 RAM mapping fixture");
        }
    };
    auto put = [](std::string &data, std::size_t at, std::uint64_t value, unsigned bytes) {
        for (unsigned i = 0; i < bytes; ++i) {
            data[at + i] = static_cast<char>(value >> (8 * i));
        }
    };
    auto get = [](std::string_view data, std::size_t at) {
        std::uint64_t value = 0;

        for (unsigned i = 0; i < 8; ++i) {
            value |= std::uint64_t(static_cast<unsigned char>(data[at + i])) << (8 * i);
        }

        return value;
    };
    std::string original(0x69108, 0);

    for (const auto &pair : {std::pair<std::size_t, std::uint32_t>{0x3000, 0x00ff7eeb},
                             {0x300c, 0x85701000},
                             {0x3014, 0x85700200},
                             {0x3020, 0x66108},
                             {0x3fc0, 0x940086c0},
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
                             {0x6854, 0x94003d25}}) {
        put(original, pair.first, pair.second, 4);
    }

    original[0x2be5b] = 40;
    put(original, 0x60d18, 0x85759e68, 8);
    put(original, 0x60e08, 0x85759e78, 8);
    put(original, 0x5cc68, 0x85000519, 8);
    put(original, 0x5cc78, 0xff000044eeaa0400ULL, 8);
    put(original, 0x43e00, 0x8000041d, 8);
    put(original, 0x43f58, 0x8560041d, 8);
    put(original, 0x44600, 0xa000041d, 8);
    const auto descriptor = qnx_a11_ram_identity_mapping(original);
    check(descriptor == 0x4000041d && (descriptor & 3) == 1 && ((descriptor >> 2) & 7) == 7 &&
          ((descriptor >> 6) & 3) == 0 && ((descriptor >> 8) & 3) == 0 &&
          ((descriptor >> 10) & 1) == 1);
    check(((get(original, 0x5cc78) >> (7 * 8)) & 255) == 255 &&
          (get(original, 0x5cc68) & 63) == 25 && ((get(original, 0x5cc68) >> 14) & 3) == 0);
    // Replay the guarded OEM constructor: it fills root[0]/[2], never root[1].
    auto root = original.substr(0x63e00, 4096);
    put(root, 8, descriptor, 8);
    put(root, 0, 0x8574241f, 8);
    put(root, 16, 0x8574141f, 8);
    check(get(root, 8) == descriptor && get(root, 0) == 0x8574241f && get(root, 16) == 0x8574141f);

    for (std::size_t i = 3; i < 512; ++i) {
        check(get(root, i * 8) == 0);
    }

    for (const auto address : {0x40000000ULL, 0x50000000ULL, 0x500fffffULL, 0x7fffffffULL}) {
        check(((address >> 30) & 511) == 1);
        check((descriptor & 0x0000ffffc0000000ULL) + (address & 0x3fffffff) == address);
    }

    check(((0x3fffffffULL >> 30) & 511) == 0 && ((0x80000000ULL >> 30) & 511) == 2);
    // Replay only the OEM constructor's L2slot47 write over the two owned blocks.
    auto l2 = original.substr(0x43e00, 4096);
    put(l2, 80 * 8, 0x8a00041d, 8);
    put(l2, 384 * 8, 0xb000041d, 8);
    put(l2, 47 * 8, 0x8574c41f, 8);
    check((get(root, 16) & 0x0000fffffffff000ULL) == 0x85741000);

    for (const auto address : {0x8a000000ULL, 0x8a000008ULL, 0x8a000072ULL, 0x8a1fffffULL,
                               0xb0000000ULL, 0xb0003fffULL, 0xa0000000ULL, 0xa0003fffULL}) {
        const auto slot = (address >> 21) & 511;
        const auto block = get(l2, slot * 8);
        check((block & 3) == 1 && ((block >> 2) & 7) == 7 && ((block >> 10) & 1) == 1 &&
              ((block >> 6) & 3) == 0 && !(block & (1ULL << 53)));
        check((block & 0x0000ffffffe00000ULL) + (address & 0x1fffff) == address);
    }

    check(get(l2, 81 * 8) == 0 && get(l2, 385 * 8) == 0 &&
          get(l2, 256 * 8) == get(original, 0x44600));

    for (std::size_t i = 0; i < 512; ++i) {
        if (i != 47 && i != 80 && i != 384) {
            check(get(l2, i * 8) == get(original, 0x43e00 + i * 8));
        }
    }

    // Numeric guard negatives intentionally bypass model SHA, exercising actual layout guards.
    for (const std::size_t at :
         {0x300c,  0x2be5b, 0x4088,  0x4040,  0x42a8,  0x42f4,  0x42fc,  0x5cc68,
          0x5cc78, 0x60d18, 0x60e08, 0x43e00, 0x43f58, 0x63e00, 0x63e08, 0x44080,
          0x44a00, 0x44600, 0x423c,  0x4240,  0x427c,  0x35e0,  0x6854}) {
        auto bad = original;
        bad[at] ^= 1;
        bool rejected = false;
        try {
            qnx_a11_ram_identity_mapping(bad);
        } catch (...) {
            rejected = true;
        }

        check(rejected);
    }

    auto duplicate = original;
    put(duplicate, 0x63e08, descriptor, 8);
    bool rejected = false;
    try {
        qnx_a11_ram_identity_mapping(duplicate);
    } catch (...) {
        rejected = true;
    }

    check(rejected);
    rejected = false;
    try {
        qnx_a11_ram_identity_mapping(original.substr(0, 0x63e08));
    } catch (...) {
        rejected = true;
    }

    check(rejected);
}

void misc_failure_fixture() {
    auto check = [](bool value) {
        if (!value) {
            throw std::runtime_error("A11 misc failure fixture");
        }
    };
    auto put = [](std::string &data, std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            data[at + i] = static_cast<char>(value >> (i * 8));
        }
    };
    auto get = [](std::string_view data, std::size_t at) {
        std::uint32_t value = 0;

        for (unsigned i = 0; i < 4; ++i) {
            value |= std::uint32_t(static_cast<unsigned char>(data[at + i])) << (i * 8);
        }

        return value;
    };
    const std::pair<std::size_t, std::uint32_t> guards[] = {
            {0x13158, 0xa9bd7bfd}, {0x13160, 0xa90153f3}, {0x13164, 0xaa0003f3},
            {0x13168, 0xf9504014}, {0x1316c, 0xf90013f5}, {0x13170, 0xf9501015},
            {0x13174, 0x940006ac}, {0x13178, 0x2a0003e0}, {0x1317c, 0xd2840002},
            {0x13180, 0xaa1303e1}, {0x13184, 0x9b157c03}, {0x13188, 0xf9400280},
            {0x1318c, 0x9400094b}, {0x131c8, 0x528000c0}, {0x131d4, 0xa94153f3},
            {0x131d8, 0xf94013f5}, {0x131dc, 0xa8c37bfd}, {0x131e0, 0xd65f03c0},
            {0x14c54, 0xb9488a60}, {0x14c58, 0xf9400bf3}, {0x14c5c, 0xa8c27bfd},
            {0x14c60, 0xd65f03c0}, {0x133f8, 0x97ffff58}, {0x133fc, 0x6b1f001f},
            {0x13400, 0x9a9f0260}, {0x13594, 0x97fffef1}, {0x13598, 0x35000140}};
    std::string raw(0x14c64, 0);

    for (const auto &[at, value] : guards) {
        put(raw, at, value);
    }

    const auto branch = qnx_a11_misc_failure_guard(raw);
    check((branch & 0xff00001f) == 0xb4000014);
    const auto displacement = static_cast<std::int32_t>(branch << 8) >> 13;
    check(0x13178 + displacement * 4 == 0x131c8);
    // Interpret the guarded CBZ/error MOV: absent storage propagates error, never success.
    const auto error = (get(raw, 0x131c8) >> 5) & 0xffff;
    check(error == 6);
    std::string output(8192, 'X'), before = output;
    unsigned reads = 0;
    auto run = [&](std::uint64_t handle) {
        const auto next = handle == 0 ? std::size_t(0x13178 + displacement * 4)
                                      : std::size_t(0x1317c);

        if (next == 0x131c8) {
            return error;
        }

        ++reads;

        return std::uint32_t(0);
    };
    check(run(0) == 6 && reads == 0 && output == before);
    const auto getter = error == 0 ? std::uintptr_t(0x1234) : 0;
    check(getter == 0);
    // Non-NULL fallthrough leaves the exact read BL and 8KiB arguments in place.
    const auto x0 = std::uint64_t(std::uint32_t(0xffff000000000200ULL));
    check(x0 == 512 && std::uint32_t(x0) == x0 && get(raw, 0x1317c) == 0xd2840002 &&
          get(raw, 0x1318c) == 0x9400094b);
    check(run(0x1234) == 0 && reads == 1);

    for (const auto &[at, value] : guards) {
        (void)value;
        auto bad = raw;
        bad[at] ^= 1;
        bool rejected = false;
        try {
            qnx_a11_misc_failure_guard(bad);
        } catch (...) {
            rejected = true;
        }

        check(rejected);
    }

    auto duplicate = raw;
    put(duplicate, 0x13178, branch);
    bool rejected = false;
    try {
        qnx_a11_misc_failure_guard(duplicate);
    } catch (...) {
        rejected = true;
    }

    check(rejected);
    rejected = false;
    try {
        qnx_a11_misc_failure_guard(raw.substr(0, 0x14c60));
    } catch (...) {
        rejected = true;
    }

    check(rejected);
}

void qinggan11_elf_fixture(std::string_view raw, std::string_view packed) {
    auto check = [](bool yes) {
        if (!yes) {
            throw std::runtime_error("Free ELF payload extent fixture");
        }
    };
    auto get = [](std::string_view b, std::size_t at) {
        std::uint64_t v = 0;

        for (unsigned i = 0; i < 8; ++i) {
            v |= std::uint64_t(static_cast<unsigned char>(b.at(at + i))) << (8 * i);
        }

        return v;
    };
    constexpr std::string_view pin =
            "a476a1e3d3aaa51d855d2d91c412a962efb6ef1e3e63540ec739779d35ce3ce8";

    check(sha256_bytes(packed) ==
          "21864b96671d2183b9e34a135ad0e4710fd3fe364597cf1d37355cdc27bb9519");
    const auto changed = assemble_qnx_qinggan11_kernel(raw, pin, packed);
    const auto declared = std::uint64_t(0x66108) + packed.size();
    check(get(changed, 0xd0) >= declared && get(changed, 0xd8) >= declared);
    check(get(changed, 0xd0) == declared && get(changed, 0xd8) == declared);
    check(get(changed, 0xc8) + get(changed, 0xd8) <= get(raw, 0x90) &&
          get(changed, 0xb8) + get(changed, 0xd0) <= changed.size());
    check(changed.substr(0x69108, packed.size()) == packed &&
          get(changed, 0xb8) + get(changed, 0xd0) == 0x69108 + packed.size());
    // An ELF loader copying exactly p_filesz must include the final byte, unlike the original
    // segment extent.
    check(0x3000 + get(raw, 0xd0) + 196 == 0x69108 + packed.size());
    const auto loaded = changed.substr(get(changed, 0xb8), get(changed, 0xd0));
    check(loaded.substr(0x66108) == packed);
    const auto identity = assemble_qnx_qinggan11_kernel(raw, pin,
                                                        raw.substr(0x69108, 0x2d75a8 - 0x69108));
    check(identity == prepare_qnx_qinggan11_startup(raw, pin));
    const auto short_image = assemble_qnx_qinggan11_kernel(raw, pin, std::string(4, 0));
    check(get(short_image, 0xd0) == get(raw, 0xd0) && get(short_image, 0xd8) == get(raw, 0xd8));
    // Checksum-valid synthetic streams test the exact geometry boundary, not codec validity.
    constexpr std::size_t capacity = 0x859d5000 - 0x85700200 - 0x66108;
    const auto boundary = assemble_qnx_qinggan11_kernel(raw, pin, std::string(capacity, 0));
    check(get(boundary, 0xc8) + get(boundary, 0xd8) == get(raw, 0x90));

    for (unsigned mode = 0; mode < 8; ++mode) {
        bool refused = false;
        try {
            if (mode == 0) {
                assemble_qnx_qinggan11_kernel(raw, pin, std::string(capacity + 4, 0));
            } else if (mode == 1) {
                assemble_qnx_qinggan11_kernel(raw, pin, std::string(3, 0));
            } else if (mode == 2) {
                assemble_qnx_qinggan11_kernel(raw, pin, "");
            } else if (mode == 3) {
                auto bad = std::string(packed);
                bad[4] ^= 1;
                assemble_qnx_qinggan11_kernel(raw, pin, bad);
            } else {
                auto bad = std::string(raw);
                bad[mode == 4 ? 0xd0 : mode == 5 ? 0xd8 : mode == 6 ? 0x90 : 0xb8] ^= 1;
                assemble_qnx_qinggan11_kernel(bad, sha256_bytes(bad), packed);
            }
        } catch (...) {
            refused = true;
        }

        check(refused);
    }

    for (std::size_t at = 0; at < 0x3000; ++at) {
        if (at < 0xd0 || at >= 0xe0) {
            check(changed[at] == raw[at]);
        }
    }

    check(changed.size() == raw.size());
    std::cout
            << "Actual complete packed payload survives PT_LOAD file/memory extents; next segment nonoverlap, identity/shrink parity and eight malformed/overflow guards passed\n";
}
} // namespace

int main(int argc, char **argv) {
    try {
        ram_mapping_fixture();
        misc_failure_fixture();
        boot_query_fixture();

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11-older") {
            std::ifstream in(argv[2], std::ios::binary);

            if (!in) {
                throw std::runtime_error("older qinggan11 startup input");
            }

            const std::string raw((std::istreambuf_iterator<char>(in)), {}),
                    pin = "dbb3bc2c6f9b0308e38627cf6a3e3b6757ba98c4f5413334526c26d812ab08f0";
            const auto changed = prepare_qnx_qinggan11_older_startup(raw, pin),
                       packed = raw.substr(0x69108, 0x2d76d0 - 0x69108);

            if (assemble_qnx_qinggan11_older_kernel(raw, pin, packed) != changed) {
                throw std::runtime_error("older qinggan11 identity assembly");
            }

            for (unsigned i = 0; i < 3; ++i) {
                bool denied = false;
                try {
                    if (i == 0) {
                        prepare_qnx_qinggan11_startup(raw, pin);
                    } else if (i == 1) {
                        prepare_qnx_qinggan11_older_startup(raw, std::string(64, '0'));
                    } else {
                        auto bad = packed;
                        bad[4] ^= 1;
                        assemble_qnx_qinggan11_older_kernel(raw, pin, bad);
                    }
                } catch (...) {
                    denied = true;
                }

                if (!denied) {
                    throw std::runtime_error(
                            "older qinggan11 wrong version or corrupt stream accepted");
                }
            }

            std::cout << "older qinggan11 startup and kernel guards passed\n";

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--qinggan11-elf") {
            auto read = [](const char *name) {
                std::ifstream f(name, std::ios::binary);

                if (!f) {
                    throw std::runtime_error("ELF fixture input");
                }

                return std::string((std::istreambuf_iterator<char>(f)), {});
            };
            qinggan11_elf_fixture(read(argv[2]), read(argv[3]));

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--cpu-prepared") {
            auto read = [](const char *name) {
                std::ifstream f(name, std::ios::binary);

                if (!f) {
                    throw std::runtime_error("CPU fixture input");
                }

                return std::string((std::istreambuf_iterator<char>(f)), {});
            };
            const auto original = read(argv[2]), changed = read(argv[3]);
            cpu_name_fixture(original, changed);
            std::cout << "CPU missing-name fallback and valid-name register/table parity passed\n";

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--qinggan11") {
            std::ifstream input(argv[2], std::ios::binary);

            if (!input) {
                throw std::runtime_error("A11 startup fixture open");
            }

            const std::string raw((std::istreambuf_iterator<char>(input)), {});
            constexpr std::string_view pin =
                    "a476a1e3d3aaa51d855d2d91c412a962efb6ef1e3e63540ec739779d35ce3ce8";
            const auto changed = prepare_qnx_qinggan11_startup(raw, pin);
            gic_device_fixture(raw, changed);
            cpu_name_fixture(raw, changed);
            bool wrong_model = false;
            try {
                prepare_qnx_qinggan11_startup(
                        raw, "1e7c487aae4f4353621f8d0301b352cc974e9037d114ff3ec2556d048512ad28");
            } catch (...) {
                wrong_model = true;
            }

            if (!wrong_model) {
                throw std::runtime_error("Passion pin accepted by Free adapter");
            }

            std::uint32_t sum = 0;

            for (std::size_t at = 0x3000; at < 0x69108; at += 4) {
                std::uint32_t word = 0;

                for (unsigned i = 0; i < 4; ++i) {
                    word |= std::uint32_t(static_cast<unsigned char>(changed[at + i])) << (i * 8);
                }

                sum += word;
            }

            if (sum) {
                throw std::runtime_error("A11 startup output checksum");
            }

            if (changed.substr(0x63e08, 8) != std::string("\x1d\x04\0\x40\0\0\0\0", 8) ||
                changed.substr(0x63e00, 8) != raw.substr(0x63e00, 8) ||
                changed.substr(0x63e10, 4080) != raw.substr(0x63e10, 4080)) {
                throw std::runtime_error("A11 RAM mapping/root preservation");
            }

            if (changed.substr(0x13178, 4) != std::string("\x94\x02\0\xb4", 4)) {
                throw std::runtime_error("A11 misc failure branch");
            }

            if (changed.substr(0x44080, 8) != std::string("\x1d\x04\0\x8a\0\0\0\0", 8) ||
                changed.substr(0x44a00, 8) != std::string("\x1d\x04\0\xb0\0\0\0\0", 8) ||
                changed.substr(0x44600, 8) != raw.substr(0x44600, 8)) {
                throw std::runtime_error("A11 restore mapping/A preservation");
            }

            if (changed.substr(0x60938, 8) != std::string("\x80\0\0\x8a\0\0\0\0", 8) ||
                changed.substr(0x60948, 8) != std::string("\xc0\0\0\x8a\0\0\0\0", 8) ||
                changed.substr(0x60940, 8) != raw.substr(0x60940, 8) ||
                changed.substr(0x60950, 8) != raw.substr(0x60950, 8)) {
                throw std::runtime_error("A11 boot query pointers/adjacent preservation");
            }

            if (changed.size() != raw.size() || changed.substr(0x69108) != raw.substr(0x69108)) {
                throw std::runtime_error("A11 startup changed boot stream");
            }

            const auto packed = raw.substr(0x69108, 0x2d75a8 - 0x69108);

            if (assemble_qnx_qinggan11_kernel(raw, pin, packed) != changed) {
                throw std::runtime_error("A11 kernel identity stream parity");
            }

            for (unsigned which = 0; which < 4; ++which) {
                bool rejected = false;
                try {
                    if (which == 0) {
                        prepare_qnx_qinggan11_startup(changed, sha256_bytes(changed));
                    } else if (which == 1) {
                        auto bad = raw;
                        bad[0x322c] ^= 1;
                        prepare_qnx_qinggan11_startup(bad, sha256_bytes(bad));
                    } else if (which == 2) {
                        prepare_qnx_qinggan11_startup(raw, std::string(64, '0'));
                    } else {
                        auto bad = packed;
                        bad[4] ^= 1;
                        assemble_qnx_qinggan11_kernel(raw, pin, bad);
                    }
                } catch (...) {
                    rejected = true;
                }

                if (!rejected) {
                    throw std::runtime_error("A11 startup/source/kernel mutation accepted");
                }
            }

            std::cout
                    << "Exact A11 startup adaptation and identity kernel assembly passed; guest boot untested; "
                    << sha256_bytes(changed) << '\n';

            return 0;
        }

        if (argc == 3 || argc == 4) {
            std::ifstream in(argv[1], std::ios::binary);

            if (!in) {
                throw std::runtime_error("input open");
            }

            const std::string raw((std::istreambuf_iterator<char>(in)), {});
            const auto output = prepare_qnx_startup(raw, argv[2]);

            if (output.size() != raw.size() || output.substr(0x5a108) != raw.substr(0x5a108)) {
                throw std::runtime_error("compressed stream changed in startup stage");
            }

            if (argc == 4) {
                std::ifstream in_ref(argv[3], std::ios::binary);
                const std::string reference((std::istreambuf_iterator<char>(in_ref)), {});

                if (reference.size() != output.size() ||
                    reference.substr(0, 0x5a108) != output.substr(0, 0x5a108)) {
                    throw std::runtime_error("source startup differs from accepted prefix");
                }
            }

            bool failed = false;
            auto incompatible = raw;
            incompatible[0x325c] ^= 1;
            try {
                prepare_qnx_startup(incompatible, sha256_bytes(incompatible));
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("incompatible instruction accepted");
            }

            failed = false;
            try {
                prepare_qnx_startup(raw, std::string(64, '0'));
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("wrong OEM SHA accepted");
            }

            std::cout << "OEM startup adapted; original compressed stream unchanged; output SHA256 "
                      << sha256_bytes(output) << '\n';

            return 0;
        }

        bool failed = false;
        try {
            prepare_qnx_startup("", std::string(64, '0'));
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("empty image accepted");
        }

        std::string malformed(0x5a108, 0);
        failed = false;
        try {
            prepare_qnx_startup(malformed, sha256_bytes(malformed));
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("unknown startup accepted");
        }

        std::cout << "QNX startup input guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
