#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
namespace {
void need(bool x) {
    if (!x) {
        throw std::runtime_error("boot storage query fixture");
    }
}

std::string read(const char *path) {
    std::ifstream f(path, std::ios::binary);

    need(bool(f));

    return {std::istreambuf_iterator<char>(f), {}};
}

std::uint64_t get(std::string_view b, std::size_t at, unsigned size) {
    need(at <= b.size() && size <= b.size() - at);
    std::uint64_t n = 0;

    for (unsigned i = 0; i < size; ++i) {
        n |= std::uint64_t((unsigned char)b[at + i]) << (i * 8);
    }

    return n;
}

struct Observed {
    bool failure = false, tail = false;
    unsigned reads = 0;
};

Observed execute(std::string_view bytes, unsigned which, bool context, bool handle,
                 bool valid_output = true) {
    constexpr std::uint64_t base = 0x8a000000, output = 0x50007000, ctx = 0x50008000,
                            object = 0x5000b000, ret = 0x12345678;
    const auto original = which ? 0x85710258ULL : 0x85710310ULL;
    std::array<std::uint64_t, 31> registers{};

    for (unsigned i = 0; i < 31; ++i) {
        registers[i] = 0x100000 + i;
    }

    registers[0] = valid_output ? output : 0;
    registers[30] = ret;
    const auto initial = registers;
    std::uint64_t pc = base + (which ? 0xc0 : 0x80);
    Observed result;

    for (unsigned step = 0; step < 16; ++step) {
        need(pc >= base && pc + 4 <= base + bytes.size());
        const auto at = std::size_t(pc - base);
        const auto word = std::uint32_t(get(bytes, at, 4));
        const auto next = pc + 4;

        if ((word & 0xff00001fU) == 0x58000009U) {
            const auto displacement = (static_cast<std::int32_t>(word << 8) >> 13) * 4;
            registers[9] = get(bytes, std::size_t(std::int64_t(pc - base) + displacement), 8);
        } else if ((word & 0xffc003ffU) == 0xf9400129U) {
            const auto address = registers[9] + static_cast<std::size_t>((word >> 10) & 0xfff) * 8;
            ++result.reads;

            if (address == 0x857632e8) {
                registers[9] = context ? ctx : 0;
            } else {
                need(address == ctx + (which ? 0x2080 : 0x2088));
                registers[9] = handle ? object : 0;
            }
        } else if ((word & 0xff000000U) == 0xb4000000U && ((word & 31) == 0 || (word & 31) == 9)) {
            if (registers[word & 31] == 0) {
                const auto displacement = (static_cast<std::int32_t>(word << 8) >> 13) * 4;
                pc = std::int64_t(pc) + displacement;
                continue;
            }
        } else if (word == 0xd61f0120U) {
            need(valid_output && context && handle && registers[9] == original);
            result.tail = true;

            for (unsigned i = 0; i < 31; ++i) {
                if (i != 9) {
                    need(registers[i] == initial[i]);
                }
            }

            return result;
        } else if (word == 0x528000c0U) {
            registers[0] = 6;
        } else if (word == 0xd65f03c0U) {
            need((!valid_output || !context || !handle) && registers[0] == 6 &&
                 registers[30] == ret);
            result.failure = true;

            for (unsigned i = 1; i < 31; ++i) {
                if (i != 9) {
                    need(registers[i] == initial[i]);
                }
            }

            return result;
        } else {
            need(false);
        }

        pc = next;
    }

    need(false);

    return result;
}
} // namespace

int main(int argc, char **argv) {
    try {
        need(argc == 4);
        const auto payload = read(argv[1]), kernel = read(argv[2]), reference = read(argv[3]);
        need(reference.size() == 115 && payload.size() == 256 &&
             payload.substr(0, 115) == reference);
        need(get(kernel, 0x60938, 8) == 0x8a000080 && get(kernel, 0x60948, 8) == 0x8a0000c0 &&
             get(kernel, 0x60940, 8) == 0x85710e38 && get(kernel, 0x60950, 8) == 0x85710cd4);

        for (unsigned which = 0; which < 2; ++which) {
            const auto no_output = execute(payload, which, true, true, false);
            need(no_output.failure && no_output.reads == 0);
            const auto no_context = execute(payload, which, false, false),
                       no_handle = execute(payload, which, true, false),
                       valid = execute(payload, which, true, true);
            need(no_context.failure && no_context.reads == 1 && no_handle.failure &&
                 no_handle.reads == 2 && valid.tail && valid.reads == 2);
        }

        auto bad = payload;
        bad[0xa0] = 0;
        bool rejected = false;
        try {
            execute(bad, 0, false, false);
        } catch (...) {
            rejected = true;
        }

        need(rejected);
        std::cout
                << "Own compiled query wrappers preserve args/LR/callee registers, honest failure6/output untouched, exact OEM table entries and first115bytes; no guest proof\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
