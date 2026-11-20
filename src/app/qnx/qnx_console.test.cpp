#include "qnx_console.hpp"
#include "model.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace voyemu;
namespace {
constexpr std::array<std::uint32_t, 28> own_words{
        0xd2800007U, 0xf2a00007U, 0xf2c00007U, 0xf2e00007U, 0x52806028U, 0xb90030e8U, 0xb94018e8U,
        0x372fffe8U, 0xb90000e1U, 0xd65f03c0U, 0x53001c01U, 0xd2a12007U, 0x17fffff8U, 0x7100041fU,
        0x540000a8U, 0x1031ef89U, 0x8b205129U, 0xd2a12008U, 0xf9000128U, 0xd65f03c0U, 0xd2a12007U,
        0xb94018e8U, 0x372000a8U, 0xb94000e8U, 0x7101c51fU, 0x54000041U, 0x140007b7U, 0xd65f03c0U};

std::string own() {
    std::string s;

    for (auto w : own_words) {
        for (unsigned i = 0; i < 4; ++i) {
            s.push_back(static_cast<char>(w >> (8 * i)));
        }
    }

    return s;
}

std::string read(const char *path) {
    std::ifstream f(path, std::ios::binary);

    if (!f) {
        throw std::runtime_error("fixture input missing");
    }

    return {std::istreambuf_iterator<char>(f), {}};
}

std::uint32_t word(std::string_view b, std::size_t at) {
    assert(at + 4 <= b.size());
    std::uint32_t w = 0;

    for (unsigned i = 0; i < 4; ++i) {
        w |= std::uint32_t(static_cast<unsigned char>(b[at + i])) << (8 * i);
    }

    return w;
}

void put(std::string &b, std::size_t at, std::uint64_t value, unsigned n = 4) {
    assert(at + n <= b.size());

    for (unsigned i = 0; i < n; ++i) {
        b[at + i] = static_cast<char>(value >> (8 * i));
    }
}

std::int64_t sign(std::uint64_t n, unsigned bits) {
    return static_cast<std::int64_t>(n << (64 - bits)) >> (64 - bits);
}

struct Machine {
    std::array<std::uint64_t, 31> x{};
    std::array<unsigned char, 32> records{};
    std::vector<std::uint32_t> flags;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> writes;
    std::vector<std::uint64_t> reads;
    std::size_t flag_at = 0;
    std::uint32_t rx = 0;
    bool z = false, c = false, returned = false, boot_entry = false;
    unsigned steps = 0;
    std::uint64_t sp = 0x1122334455667788ULL, base = 0x09000000, physical = 0x09000000;
    std::string tx;

    std::uint64_t load(std::uint64_t addr, unsigned n) {
        reads.push_back(addr);
        const auto pa = addr >= base && addr < base + 0x1000 ? physical + (addr - base) : addr;

        if (pa == 0x09000018 && n == 4) {
            assert(!flags.empty());

            return flags[std::min(flag_at++, flags.size() - 1)];
        }

        if (pa == 0x09000000 && n == 4) {
            return rx;
        }

        if (pa >= 0xa90000 && pa < 0xa91000) {
            return n == 8 ? 0x0080008000800080ULL : 0x00800080;
        }

        throw std::runtime_error("unexpected MMIO read");
    }

    void store(std::uint64_t addr, std::uint64_t v, unsigned n) {
        writes.push_back({addr, v});

        if (addr >= 0x857655f8 && addr + n <= 0x85765618) {
            assert(n == 8);

            for (unsigned i = 0; i < n; ++i) {
                records[addr - 0x857655f8 + i] = static_cast<unsigned char>(v >> (8 * i));
            }

            return;
        }

        const auto pa = addr >= base && addr < base + 0x1000 ? physical + (addr - base) : addr;

        if (pa == 0x09000030 && n == 4) {
            assert(v == 0x301);

            return;
        }

        if (pa == 0x09000000 && n == 4) {
            tx.push_back(static_cast<char>(v));

            return;
        }

        if (pa >= 0xa90000 && pa < 0xa91000) {
            return;
        }

        throw std::runtime_error("unexpected MMIO write");
    }

    void run(std::string_view blob, unsigned entry) {
        std::int64_t pc = entry;

        for (steps = 0; steps < 128; ++steps) {
            assert(pc >= 0 && static_cast<std::size_t>(pc + 4) <= blob.size());
            auto w = word(blob, pc);
            auto next = pc + 4;
            unsigned d = w & 31, n = (w >> 5) & 31;
            const bool sf = w >> 31;

            if ((w & 0x7f800000U) == 0x52800000U) {
                assert(d < 31);
                x[d] = std::uint64_t((w >> 5) & 65535) << (((w >> 21) & 3) * 16);

                if (!sf) {
                    x[d] = std::uint32_t(x[d]);
                }
            } else if ((w & 0x7f800000U) == 0x72800000U) {
                assert(d < 31);
                auto shift = ((w >> 21) & 3) * 16;
                x[d] = (x[d] & ~(65535ULL << shift)) | (std::uint64_t((w >> 5) & 65535) << shift);

                if (!sf) {
                    x[d] = std::uint32_t(x[d]);
                }
            } else if (w == 0x53001c01) {
                x[1] = x[0] & 255;
            } else if ((w & 0x9f000000U) == 0x10000000U) {
                assert(d < 31);
                auto imm = sign((((w >> 5) & 0x7ffff) << 2) | ((w >> 29) & 3), 21);
                x[d] = 0x857017cc + pc + imm;
            } else if (w == 0x8b205129) {
                x[9] += std::uint64_t(std::uint32_t(x[0])) * 16;
            } else if ((w & 0xffe0fc00U) == 0x8b000000U) {
                assert(d < 31 && n < 31 && ((w >> 16) & 31) < 31);
                x[d] = x[n] + x[(w >> 16) & 31];
            } else if ((w & 0xffc00000U) == 0xb9400000U || (w & 0xffc00000U) == 0xf9400000U) {
                assert(d < 31 && n < 31);
                unsigned bytes = (w & (1U << 30)) ? 8 : 4;
                x[d] = load(x[n] + static_cast<std::size_t>((w >> 10) & 4095) * bytes, bytes);
            } else if ((w & 0xffc00000U) == 0xb9000000U || (w & 0xffc00000U) == 0xf9000000U) {
                assert(d < 31 && n < 31);
                unsigned bytes = (w & (1U << 30)) ? 8 : 4;
                store(x[n] + static_cast<std::size_t>((w >> 10) & 4095) * bytes,
                      bytes == 4 ? std::uint32_t(x[d]) : x[d], bytes);
            } else if ((w & 0x7f000000U) == 0x37000000U) {
                assert(d < 31);
                unsigned bit = ((w >> 19) & 31) | ((w >> 26) & 32);

                if (x[d] & (1ULL << bit)) {
                    next = pc + sign((w >> 5) & 0x3fff, 14) * 4;
                }
            } else if ((w & 0x7f00001fU) == 0x7100001fU) {
                assert(n < 31);
                auto v = sf ? x[n] : std::uint32_t(x[n]);
                auto imm = std::uint64_t((w >> 10) & 4095) << (((w >> 22) & 1) ? 12 : 0);
                z = v == imm;
                c = v >= imm;
            } else if ((w & 0xff000010U) == 0x54000000U) {
                unsigned cond = w & 15;
                bool take = cond == 0 ? z : cond == 1 ? !z : cond == 8 ? (c && !z) : false;
                assert(cond == 0 || cond == 1 || cond == 8);

                if (take) {
                    next = pc + sign((w >> 5) & 0x7ffff, 19) * 4;
                }
            } else if ((w & 0xfc000000U) == 0x14000000U) {
                next = pc + sign(w & 0x3ffffff, 26) * 4;

                if (0x857017cc + next == 0x85703710) {
                    boot_entry = true;

                    return;
                }
            } else if (w == 0x9263014b) {
                x[11] = x[10] & 0x20000000;
            } else if (w == 0x9240014b) {
                x[11] = x[10] & 1;
            } else if (w == 0xd65f03c0) {
                returned = true;

                return;
            } else {
                throw std::runtime_error("unsupported fixture instruction");
            }

            pc = next;
        }
    }
};

std::string mapped(std::string blob, std::uint64_t base) {
    for (unsigned i = 0; i < 4; ++i) {
        put(blob, static_cast<std::size_t>(i) * 4,
            (word(blob, static_cast<std::size_t>(i) * 4) & ~(65535U << 5)) |
                    (std::uint32_t((base >> (i * 16)) & 65535) << 5));
    }

    return blob;
}

Machine machine() {
    Machine m;

    for (unsigned i = 0; i < 31; ++i) {
        m.x[i] = 0x1234567800000000ULL + i;
    }

    m.x[30] = 0x8570077c;
    m.records.fill(0xa5);

    return m;
}

void preserved(const Machine &m) {
    for (unsigned i = 19; i < 30; ++i) {
        assert(m.x[i] == 0x1234567800000000ULL + i);
    }

    assert(m.x[30] == 0x8570077c && m.sp == 0x1122334455667788ULL);
}

void semantics(std::string_view blob) {
    assert(blob.size() == 112);
    auto copy = machine();
    copy.base = 0xffffff80403ce000ULL;
    copy.flags = {32, 32, 0};
    copy.x[1] = 'A';
    copy.run(mapped(std::string(blob), copy.base), 0);
    assert(copy.returned && copy.tx == "A" && copy.flag_at == 3 && copy.x[1] == 'A');
    preserved(copy);
    auto early = machine();
    early.flags = {0};
    early.x[0] = 0x162;
    early.run(blob, 40);
    assert(early.returned && early.tx == "b" && early.x[0] == 0x162);
    preserved(early);

    for (unsigned index : {0U, 1U, 2U, 0xffffffffU}) {
        auto m = machine();
        m.x[0] = index;
        m.run(blob, 52);
        assert(m.returned && m.reads.empty() && m.x[0] == index);
        preserved(m);

        for (unsigned i = 0; i < 32; ++i) {
            auto expected =
                    (index < 2 && i >= index * 16 && i < index * 16 + 8)
                            ? static_cast<unsigned char>(0x09000000ULL >> ((i - index * 16) * 8))
                            : 0xa5;
            assert(m.records[i] == expected);
        }
    }

    for (unsigned ch : {0U, 0x20U, 0x71U}) {
        auto m = machine();
        m.flags = {ch ? 0U : 16U};
        m.rx = ch;
        m.run(blob, 80);
        assert(m.boot_entry == (ch == 0x71) && m.returned == (ch != 0x71));
        assert(m.reads.size() == (ch ? 2U : 1U) && m.tx.empty());
        preserved(m);
    }

    auto busy = machine();
    busy.base = 0xffffff80403ce000ULL;
    busy.flags = {32};
    busy.x[1] = 'X';
    busy.run(mapped(std::string(blob), busy.base), 0);
    assert(!busy.returned && busy.tx.empty() && busy.steps == 128);
    preserved(busy);
}
} // namespace

int main(int argc, char **argv) {
    try {
        auto blob = own();

        if (argc == 3 && std::string_view(argv[1]) == "--blob") {
            blob = read(argv[2]);
        }

        semantics(blob);

        if (argc == 3 && std::string_view(argv[1]) == "--prepared") {
            const auto p = read(argv[2]);
            assert(p.size() >= 0x463c);
            const auto body = p.substr(0x45cc, 112);

            if (body != blob) {
                assert(sha256_bytes(body) ==
                       "ef8a7402cdd390e13f469184b5704a66b885c2b3ea5b67ab825c4bae96bdedcc");
                auto legacy = machine();
                legacy.base = legacy.physical = 0xa90000;
                legacy.x[1] = 0x20;
                legacy.run(mapped(body, legacy.base), 0);
                assert(legacy.tx.empty() && !legacy.returned && legacy.steps == 128);
                throw std::runtime_error(
                        "actual old GENI polling repeats flash status and emits no PL011 byte");
            }
        }

        // --source ADAPTER ORIGINAL PREPARED: ADAPTER is the hardware_adapter of the original.
        if (argc == 5 && std::string_view(argv[1]) == "--source") {
            const auto adapter = qnx_adapter(std::string_view(argv[2])).adapter;
            const auto original = read(argv[3]), prepared = read(argv[4]);
            qnx_a11_console_layout_guard(original);
            const auto changed = adapt_qnx_pl011_console(adapter, original, sha256_bytes(original),
                                                         prepared, blob);

            for (const auto &spec : qnx_adapter_registry) {
                bool foreign_rejected = spec.adapter == adapter;
                try {
                    if (!foreign_rejected) {
                        adapt_qnx_pl011_console(spec.adapter, original, sha256_bytes(original),
                                                prepared, blob);
                    }
                } catch (const std::exception &) {
                    foreign_rejected = true;
                }

                assert(foreign_rejected);
            }

            assert(changed.size() == prepared.size());

            for (std::size_t i = 0; i < prepared.size(); ++i) {
                if (changed[i] != prepared[i]) {
                    assert((i >= 0x45cc && i < 0x463c) || (i >= 0x60ac8 && i < 0x60ad8) ||
                           (i >= 0x45040 && i < 0x45048) || (i >= 0x4584 && i < 0x4588) ||
                           (i >= 0x3578 && i < 0x357c) || (i >= 0x69104 && i < 0x69108));
                }
            }

            bool rejected = false;
            try {
                adapt_qnx_pl011_console(adapter, original, sha256_bytes(original), changed, blob);
            } catch (const std::exception &) {
                rejected = true;
            }

            assert(rejected);

            for (auto at : {0x60ac8U, 0x45040U, 0x60e10U, 0x3578U}) {
                auto bad = original;
                bad[at] ^= 1;
                rejected = false;
                try {
                    qnx_a11_console_layout_guard(bad);
                } catch (const std::exception &) {
                    rejected = true;
                }

                assert(rejected);
            }

            std::cout << "source/output guards and reapply rejection PASS\n";
        }

        std::cout << "real PL011 TX/RX sequence, ABI and bounded busy negative PASS\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
