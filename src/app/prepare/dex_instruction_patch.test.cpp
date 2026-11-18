#include "dex_instruction_patch.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
using namespace voyemu;
namespace {
void check(bool value) {
    if (!value) {
        throw std::runtime_error("DEX literal test failed");
    }
}

std::string hex(const std::string &value) {
    std::string b;

    for (std::size_t i = 0; i < value.size(); i += 2) {
        b += static_cast<char>(std::stoul(value.substr(i, 2), nullptr, 16));
    }

    return b;
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 3 &&
            (std::string(argv[1]) == "--real" || std::string(argv[1]) == "--navi-real")) {
            std::ifstream in(argv[2], std::ios::binary);
            check(bool(in));
            std::string source((std::istreambuf_iterator<char>(in)), {});
            const bool navi = std::string(argv[1]) == "--navi-real";
            const auto output = navi ? patch_navi_msaa(source)
                                     : patch_dex_literal(source, launcher_ethernet_guard_recipe());

            if (navi) {
                check(source.size() == output.size());

                for (std::size_t i = 0; i < source.size(); ++i) {
                    if (i < 8 || i >= 32) {
                        check(source[i] == output[i] || (i >= 3755344 && i < 3755346) ||
                              (i >= 3755966 && i < 3755970) || (i >= 3755986 && i < 3755990));
                    }
                }

                bool failed = false;
                try {
                    patch_navi_msaa(output);
                } catch (const std::runtime_error &) {
                    failed = true;
                }

                check(failed);
            }

            std::cout << "{\"output_sha256\":\"" << sha256_bytes(output)
                      << "\",\"bytes\":" << output.size() << ",\"guest_verified\":false}\n";

            return 0;
        }

        check(argc == 1);
        const auto source = hex(
                "6465780a303335007e317ae3b7e668c12dcca12e747eb185b25e6ce0287169a134010000700000007856341200000000000000000000000004000000700000000300000080000000010000008c0000000000000000000000010000009800000001000000a000000074000000c0000000e8000000f3000000f6000000f90000000000000001000000020000000200000002000000c000000000000000030000000000000001000000ffffffff00000000ffffffff24010000e000000000000000010000000100000002000100000000002001000003000000130009000f000000000001000009c801094c4578616d706c653b00014900015a00016600756e746f756368656420616e6e6f746174696f6e732f64656275672066697874757265000101000000000000000000000000000000000000");
        const DexLiteralPatch recipe = {
                "5d7b9df51cd4e6d76abebcd21f449c3d19c23b33e91803a3fb8c5ed45b49efe9",
                "LExample;",
                "f",
                "(I)Z",
                "0f5e67221ac5e6559b0ea44785713d37ad93b45beaae834d6d64b78f8505cd9d",
                "8878d6f1b4eb115608d1258b90dabfb2d03e051becb8b45ed8416b99cd21befd",
                9,
                0,
                3,
                2,
                1,
                0,
                0,
                0,
                9,
                1};
        const auto output = patch_dex_literal(source, recipe);
        check(output ==
              hex("6465780a30333500f530e358bace89aa96cc67ebde10502e897c38778798632334010000700000007856341200000000000000000000000004000000700000000300000080000000010000008c0000000000000000000000010000009800000001000000a000000074000000c0000000e8000000f3000000f6000000f90000000000000001000000020000000200000002000000c000000000000000030000000000000001000000ffffffff00000000ffffffff24010000e000000000000000010000000100000002000100000000002001000003000000130001000f000000000001000009c801094c4578616d706c653b00014900015a00016600756e746f756368656420616e6e6f746174696f6e732f64656275672066697874757265000101000000000000000000000000000000000000"));

        for (std::size_t i = 0; i < source.size(); ++i) {
            if (i < 8 || i >= 32) {
                check(source[i] == output[i] || i == 218);
            }
        }

        auto rejected = [&](const std::string &input, const DexLiteralPatch &p) {
            try {
                patch_dex_literal(input, p);
            } catch (const std::runtime_error &) {
                return;
            }

            throw std::runtime_error("accepted bad recipe/input");
        };
        auto bad = recipe;
        bad.owner = "LOther;";
        rejected(source, bad);
        bad = recipe;
        bad.method = "other";
        rejected(source, bad);
        bad = recipe;
        bad.prototype = "()Z";
        rejected(source, bad);
        bad = recipe;
        bad.access = 1;
        rejected(source, bad);
        bad = recipe;
        bad.registers = 3;
        rejected(source, bad);
        bad = recipe;
        bad.code_unit = 2;
        rejected(source, bad);
        bad = recipe;
        bad.reg = 1;
        rejected(source, bad);
        bad = recipe;
        bad.before = 8;
        rejected(source, bad);
        bad = recipe;
        bad.output_sha256 = std::string(64, '0');
        rejected(source, bad);
        bad = recipe;
        bad.instructions_sha256 = std::string(64, '0');
        rejected(source, bad);
        bad = recipe;
        bad.source_sha256 = std::string(64, '0');
        rejected(source, bad);
        auto corrupted = source;
        corrupted[8] ^= 1;
        bad = recipe;
        bad.source_sha256 = sha256_bytes(corrupted);
        rejected(corrupted, bad);
        corrupted = source;
        corrupted[12] ^= 1;
        bad = recipe;
        bad.source_sha256 = sha256_bytes(corrupted);
        rejected(corrupted, bad);
        rejected(output, recipe);
        rejected(source.substr(0, 31), recipe);
        bool failed = false;
        try {
            patch_navi_msaa(source);
        } catch (const std::runtime_error &) {
            failed = true;
        }

        check(failed);
        std::cout << "DEX literal synthetic tests passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
