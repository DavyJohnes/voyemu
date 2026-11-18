// Host-only execution of the compiled OWN callback. The model never opens guest memory.
#include "process.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using Word = std::uint64_t;
using Arguments = std::array<Word, 5>;
constexpr Word a_base = 0xa0000000, b_base = 0xe5000000;
constexpr Word token = 0x10000000, block_size = 4096, backing_blocks = 49055;
constexpr Word startup_offset = block_size, startup_size = 20548012;
constexpr Word startup_blocks = startup_size / block_size + 1;
constexpr Word marker_offset = block_size + 20543912 - 4, oem_marker = 0xd3ec2e7f;
constexpr std::string_view copy_sha =
        "c5fc787c631eb2a03376c4b759f22b7343f4de07700c114cb968d0bf1d61cc81";
constexpr std::string_view prefix_sha =
        "dcf566d7e5cb3d6987a66b6e376839f5383f734a8236be0d527ff1931869b200";
constexpr std::string_view prior_table_sha =
        "be6529489f21dc41e0808d9b8dc0c52169a2ccc015104c0aca1f97d1c8a8edc2";

void need(bool condition, const char *reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}

std::string read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file) {
        throw std::runtime_error("cannot open " + path.string());
    }

    const auto size = file.tellg();
    need(size >= 0, "cannot get input size");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    need(bool(file.read(bytes.data(), static_cast<std::streamsize>(size))), "cannot read input");

    return bytes;
}

Word little_endian(std::string_view bytes, std::size_t offset, unsigned width) {
    need(offset <= bytes.size() && width <= bytes.size() - offset, "truncated input");
    Word value = 0;

    for (unsigned i = 0; i < width; ++i) {
        value |= Word(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    }

    return value;
}

int signed_field(unsigned value, unsigned bits) {
    const int sign = 1 << (bits - 1);

    return static_cast<int>(value) - ((value & sign) ? 2 * sign : 0);
}

struct Mapping {
    Word base;
    std::string &bytes;
    bool writable;
};

struct OutsideBacking : std::runtime_error {
    Word address;

    explicit OutsideBacking(Word addr)
        : std::runtime_error("outside owned backing"), address(addr) {}
};

char *access(std::span<const Mapping> mappings, Word address, bool write) {
    for (const auto &mapping : mappings) {
        if (address < mapping.base) {
            continue;
        }

        const auto offset = address - mapping.base;

        if (offset > mapping.bytes.size() || 16 > mapping.bytes.size() - offset) {
            continue;
        }

        need(!write || mapping.writable, "write to pristine B");

        return mapping.bytes.data() + offset;
    }

    throw OutsideBacking(address);
}

struct Observation {
    Word result = 0, reads = 0, writes = 0;
    unsigned steps = 0;
    bool barrier = false;
};

// Decode only the AArch64 instructions used by the two callbacks. PC is a byte offset
// into .text because all branch displacements are relative to the current instruction.
Observation execute(std::string_view code, const Arguments &args,
                    std::span<const Mapping> mappings = {}) {
    std::array<Word, 32> reg{};

    for (unsigned i = 0; i < 31; ++i) {
        reg[i] = 0x12340000 + i;
    }

    std::copy(args.begin(), args.end(), reg.begin());
    const auto initial = reg;
    const auto put = [&](unsigned dst, Word value) {
        if (dst != 31) {
            reg[dst] = value;
        }
    };
    int pc = 0;
    bool zero = false, carry = false;
    Observation observed;

    for (unsigned step = 1; step <= 6000000; ++step) {
        need(pc >= 0 && pc % 4 == 0, "invalid instruction address");
        const auto word = static_cast<std::uint32_t>(little_endian(code, pc, 4));
        const unsigned rd = word & 31, rn = (word >> 5) & 31, rm = (word >> 16) & 31;
        const auto branch = [&] {
            return pc + 4 * signed_field((word >> 5) & 0x7ffff, 19);
        };
        int next = pc + 4;

        if ((word & 0xff000000) == 0x34000000) { // CBZ Wt
            if (static_cast<std::uint32_t>(reg[rd]) == 0) {
                next = branch();
            }
        } else if ((word & 0xffc00000) == 0xd3400000) { // UBFM Xd (UXTW, LSL)
            const unsigned rotate = (word >> 16) & 63, end = (word >> 10) & 63;
            const Word all_bits = ~Word{0};
            const auto value = end < rotate
                                       ? (reg[rn] & (all_bits >> (63 - end))) << (64 - rotate)
                                       : (reg[rn] >> rotate) & (all_bits >> (63 - (end - rotate)));
            put(rd, value);
        } else if ((word & 0x7f800000) == 0x52800000 ||
                   (word & 0x7f800000) == 0x12800000) { // MOVZ, MOVN
            Word value = Word((word >> 5) & 0xffff) << (16 * ((word >> 21) & 3));

            if ((word & 0x40000000) == 0) {
                value = ~value;
            }

            put(rd, (word & 0x80000000) ? value : static_cast<std::uint32_t>(value));
        } else if ((word & 0xffe0fc00) == 0x8b000000) { // ADD Xd, Xn, Xm
            put(rd, reg[rn] + reg[rm]);
        } else if ((word & 0xffe0fc00) == 0xeb000000 ||
                   (word & 0xffc00000) == 0xf1000000) { // SUBS, CMP
            const Word lhs = reg[rn];
            const Word rhs = (word & 0x10000000) ? ((word >> 10) & 0xfff) : reg[rm];
            put(rd, lhs - rhs);
            zero = lhs == rhs;
            carry = lhs >= rhs;
        } else if ((word & 0xff000010) == 0x54000000) { // B.cond
            const auto condition = word & 15;
            need(condition == 1 || condition == 3 || condition == 8,
                 "unsupported branch condition");

            if ((condition == 1 && !zero) || (condition == 3 && !carry) ||
                (condition == 8 && carry && !zero)) {
                next = branch();
            }
        } else if ((word & 0xffc00000) == 0xa8c00000 ||
                   (word & 0xffc00000) == 0xa8800000) { // LDP, STP post-index
            const Word address = reg[rn];
            const unsigned rt2 = (word >> 10) & 31;
            const bool load = (word & 0x00400000) != 0;
            auto *bytes = access(mappings, address, !load);

            if (load) {
                put(rd, little_endian(std::string_view(bytes, 16), 0, 8));
                put(rt2, little_endian(std::string_view(bytes, 16), 8, 8));
                observed.reads += 16;
            } else {
                for (unsigned i = 0; i < 8; ++i) {
                    bytes[i] = static_cast<char>(reg[rd] >> (8 * i));
                    bytes[8 + i] = static_cast<char>(reg[rt2] >> (8 * i));
                }

                observed.writes += 16;
            }

            put(rn, address + std::size_t{8} * signed_field((word >> 15) & 127, 7));
        } else if (word == 0xd5033f9f) { // DSB SY
            observed.barrier = true;
        } else if (word == 0xd65f03c0) { // RET
            need(std::equal(reg.begin() + 19, reg.begin() + 31, initial.begin() + 19),
                 "callee/LR clobbered");
            observed.steps = step;
            observed.result = reg[0];

            return observed;
        } else {
            throw std::runtime_error("unsupported OWN opcode");
        }

        pc = next;
    }

    throw std::runtime_error("instruction bound");
}
} // namespace

int main(int argc, char **argv) {
    constexpr auto usage =
            "Usage: qnx_passion_ifs2_copy_test blob prefix prepared_kernel [--prior-table PATH --next-table PATH]";

    try {
        const auto start = std::chrono::steady_clock::now();
        std::array<std::string_view, 3> positional;
        std::size_t supplied = 0;
        std::string_view prior_table, next_table;
        bool options = true;

        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];

            if (options && (arg == "-h" || arg == "--help")) {
                std::cout << usage << '\n';

                return 0;
            }

            if (options && arg == "--") {
                options = false;
                continue;
            }

            if (options && arg.starts_with("--")) {
                const auto separator = arg.find('=');
                const auto flag = arg.substr(0, separator);
                need(flag == "--prior-table" || flag == "--next-table", usage);
                auto &table = flag == "--prior-table" ? prior_table : next_table;

                if (separator == std::string_view::npos) {
                    need(i + 1 < argc && !std::string_view(argv[i + 1]).starts_with("--"), usage);
                    table = argv[++i];
                } else {
                    table = arg.substr(separator + 1);
                }
            } else {
                need(supplied < positional.size(), usage);
                positional[supplied++] = arg;
            }
        }

        need(supplied == positional.size() && prior_table.empty() == next_table.empty(), usage);
        const auto blob = read(positional[0]);
        need(blob.size() == 120 && voyemu::sha256_bytes(blob) == copy_sha,
             "OWN copy blob pin drift");
        const auto raw = read(positional[1]);
        need(voyemu::sha256_bytes(raw) == prefix_sha, "pristine B prefix pin drift");
        const auto kernel = read(positional[2]);
        need(little_endian(kernel, 0x6720, 4) == 0xd2a20016 &&
                     little_endian(kernel, 0x6724, 4) == 0xd503201f &&
                     little_endian(kernel, 0x6730, 4) == 0xd503201f,
             "OWN startup token producer drift");

        // Restore the genuine OEM startup end-block after modeling the prior zeroing.
        constexpr Word bytes = startup_blocks * block_size;
        auto pristine = raw.substr(startup_offset, bytes);
        need(pristine.size() == bytes && little_endian(pristine, marker_offset, 4) == oem_marker,
             "OEM marker drift");
        const auto saved = pristine;
        auto target = pristine;
        std::fill_n(target.begin() + startup_size - block_size, block_size, '\0');
        need(little_endian(target, marker_offset, 4) == 0,
             "startup target marker must start at zero");
        constexpr Arguments original{7, a_base + startup_offset, token + 1, startup_blocks,
                                     startup_offset};
        constexpr std::string_view noop("\x00\x00\x80\x52\xc0\x03\x5f\xd6", 8);
        const auto old = execute(noop, original);
        need(old.result == 0 && old.writes == 0 && little_endian(target, marker_offset, 4) == 0,
             "old no-op changed the target");

        if (!prior_table.empty()) {
            const auto before = read(prior_table), after = read(next_table);
            need(before.size() == 4096 && after.size() == 4096 &&
                         voyemu::sha256_bytes(before) == prior_table_sha,
                 "prior early map pin or table size drift");
            constexpr Word leaf_size = 0x200000, leaf_count = 96, config_base = 0xf1000000;

            for (unsigned i = 0; i < 512; ++i) {
                const auto expected = i >= 296 && i < 296 + leaf_count
                                              ? (b_base + (i - 296) * leaf_size) | 0x41d
                                              : 0;
                need(little_endian(after, static_cast<std::size_t>(i) * 8, 8) == expected,
                     "early-map attrs/PA/extent/otherzero");
            }

            need(b_base + leaf_count * leaf_size == config_base &&
                         b_base + raw.size() <= config_base,
                 "early map overlaps config");
            auto old_target = target;
            auto first_source = pristine.substr(0, leaf_size - startup_offset);
            const std::array maps{Mapping{a_base + startup_offset, old_target, true},
                                  Mapping{b_base + startup_offset, first_source, false}};
            try {
                execute(blob, original, maps);
                throw std::runtime_error("old single leaf unexpectedly completed");
            } catch (const OutsideBacking &fault) {
                need(fault.address == b_base + leaf_size, "old map must match actual FAR");
            }

            need(pristine.starts_with(first_source) && pristine == saved,
                 "pristine B changed during old-map run");
        }

        const std::array maps{Mapping{a_base + startup_offset, target, true},
                              Mapping{b_base + startup_offset, pristine, false}};
        const auto copied = execute(blob, original, maps);
        need(copied.result == 0 && copied.barrier && copied.writes == bytes && pristine == saved &&
                     target == pristine && little_endian(target, marker_offset, 4) == oem_marker,
             "full request failed to restore OEM bytes");

        // Empty mappings make any read or write before rejecting a guard fail the test.
        constexpr std::array<Arguments, 12> negatives{
                {{7, a_base + startup_offset, token + 1, 0, startup_offset},
                 {7, a_base, token - 1, 1, 0},
                 {7, a_base, token + backing_blocks, 1, 0},
                 {7, a_base, token, 0xffffffff, 0},
                 {7, b_base + startup_offset, token + 1, 1, startup_offset},
                 {7, a_base + startup_offset + 8, token + 1, 1, startup_offset},
                 {7, a_base + startup_offset, token + 1, 1, startup_offset + 1},
                 {7, a_base + startup_offset, token + 1, 1, 0},
                 {7, a_base, 0xffffffff, 1, 0},
                 {7, a_base, token, backing_blocks + 1, 0},
                 {7, a_base + startup_offset, 65, startup_blocks, startup_offset},
                 {7, a_base, 64, 1, 0}}};

        for (const auto &args : negatives) {
            const auto result = execute(blob, args);
            need(result.result == 0xffffffff && result.reads == 0 && result.writes == 0 &&
                         !result.barrier,
                 "invalid request crossed a copy guard");
        }

        // First and last backing blocks. The handle is ignored by the pinned reader.
        constexpr std::array boundaries{Word{0}, (backing_blocks - 1) * block_size};

        for (const auto delta : boundaries) {
            std::string a(block_size, '\0'), b(block_size, '\0');

            for (std::size_t i = 0; i < b.size(); ++i) {
                b[i] = static_cast<char>(i & 255);
            }

            const std::array boundary_maps{Mapping{a_base + delta, a, true},
                                           Mapping{b_base + delta, b, false}};
            const auto result = execute(
                    blob, {0xffffffff, a_base + delta, token + delta / block_size, 1, delta},
                    boundary_maps);
            need(a == b && result.writes == block_size, "backing boundary copy failed");
        }

        const auto elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::cout << std::boolalpha << "{\n"
                  << "  \"host_only\": true,\n"
                  << "  \"old_noop_RED_marker_zero\": true,\n"
                  << "  \"new_realcopy_GREEN_marker_restored\": true,\n"
                  << "  \"OEM_marker_authored\": false,\n"
                  << "  \"negative_cases\": " << negatives.size() << ",\n"
                  << "  \"boundary_cases\": " << boundaries.size() << ",\n"
                  << "  \"full_original_request\": {\n"
                  << "    \"result\": " << copied.result << ",\n"
                  << "    \"steps\": " << copied.steps << ",\n"
                  << "    \"reads\": " << copied.reads << ",\n"
                  << "    \"writes\": " << copied.writes << ",\n"
                  << "    \"barrier\": " << copied.barrier << "\n"
                  << "  },\n"
                  << "  \"source_B_unchanged\": true,\n"
                  << "  \"mapped_boundary\": ";

        if (prior_table.empty()) {
            std::cout << "null";
        } else {
            std::cout << "{\n"
                      << "    \"old_actual_FAR_RED\": \"0xe5200000\",\n"
                      << "    \"new_all96_identity_leaves\": true,\n"
                      << "    \"config_excluded\": true\n"
                      << "  }";
        }

        std::cout << ",\n  \"elapsed\": " << elapsed << "\n}\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
