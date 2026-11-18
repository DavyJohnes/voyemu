#include "qnx_bootstrap.hpp"
#include "process.hpp"
#include "qnx_passion_smem.hpp"
#include <cassert>
#include <initializer_list>
#include <iostream>

static void passion_bank_test() {
    using voyemu::qnx_passion_smem_bank_valid;
    assert(qnx_passion_smem_bank_valid(0x40400000, 0x6fc00000));
    assert(qnx_passion_smem_bank_valid(0x50000000, 0x60000000));

    for (auto base : {0x3ffff000ULL, 0x40000000ULL, 0x40100000ULL, 0x40200000ULL, 0x40300000ULL,
                      0x40301000ULL, 0x40302000ULL}) {
        assert(!qnx_passion_smem_bank_valid(base, 0xb0000000 - base));
    }

    assert(!qnx_passion_smem_bank_valid(0x40400001, 0x6fbfffff));
    assert(!qnx_passion_smem_bank_valid(0x40400000, 0x6fc01000));
    assert(!qnx_passion_smem_bank_valid(0x40400000, 0x6fbff000));
    assert(!qnx_passion_smem_bank_valid(0x50001000, 0x5ffff000));
    assert(!qnx_passion_smem_bank_valid(0xfffffffffffff000ULL, 0x1000));
    const auto memory = voyemu::qnx_passion_smem_memory();
    auto u64 = [&](unsigned offset) {
        std::uint64_t v = 0;

        for (unsigned i = 0; i < 8; i++) {
            v |= std::uint64_t(static_cast<unsigned char>(memory.at(offset + i))) << (8 * i);
        }

        return v;
    };
    assert(memory.size() == 2 * 1024 * 1024 && u64(0x1058) == 0x40400000 &&
           u64(0x1088) == 0x6fc00000);
    auto baseline = voyemu::qnx_smem_memory();

    for (unsigned i = 0; i < 8; i++) {
        baseline[0x1058 + i] = memory[0x1058 + i];
        baseline[0x1088 + i] = memory[0x1088 + i];
    }

    assert(baseline == memory); // No other Qualcomm SMEM fields change.
}

int main() {
    try {
        passion_bank_test();
        const auto bytes = voyemu::qnx_smem_memory();

        if (voyemu::sha256_bytes(bytes) !=
            "613cabce3ed6e0e9cbce818bbb6560ea0e08dfa977ae12f7bd175bc26d6d3127") {
            throw std::runtime_error("SMEM source parity");
        }

        bool failed = false;
        try {
            voyemu::qnx_smem_memory(1, 1);
        } catch (...) {
            failed = true;
        }

        if (!failed) {
            throw std::runtime_error("unaligned bank accepted");
        }

        std::cout
                << "SMEM constructed from memory geometry; exact accepted SHA and invalid bank guard passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
