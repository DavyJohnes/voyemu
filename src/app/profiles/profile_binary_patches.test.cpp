#include "profile_binary_patches.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--music") {
            const auto patch = plan_profile_binary_patch(std::filesystem::canonical(argv[2]),
                                                         argv[3], "MusicConcate");
            patch.revalidate();

            if (patch.removals.size() != 2 || !patch.additions.empty() ||
                patch.provenance.at("full_node_supported") != false) {
                throw std::runtime_error("Music partial scope mismatch");
            }

            std::cout << patch.provenance.dump() << '\n';

            return 0;
        }

        if (argc == 9 && std::string(argv[1]) == "--ime") {
            const auto patch = plan_profile_binary_patch(
                    std::filesystem::canonical(argv[2]), argv[3], argv[4],
                    {{"qgime-handwrite", std::filesystem::canonical(argv[5])},
                     {"qgime-pinyin", std::filesystem::canonical(argv[6])}},
                    std::filesystem::canonical(argv[7]), argv[8]);
            patch.revalidate();

            if (patch.removals.size() != 2 || patch.additions.size() != 2 ||
                patch.provenance.at("machine_ready") != false) {
                throw std::runtime_error("IME partial scope mismatch");
            }

            std::cout << patch.provenance.dump() << '\n';

            return 0;
        }

        if (argc != 1) {
            throw std::runtime_error(
                    "usage: --music ROOT PROFILE | --ime ROOT PROFILE NODE HANDWRITE_BUILD PINYIN_BUILD COMPILER PIN");
        }

        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-binary-patches-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        auto refused = [&](const std::string &node, const std::string &expected) {
            try {
                plan_profile_binary_patch(root, "test-profile", node);
            } catch (const std::exception &e) {
                if (std::string(e.what()).find(expected) != std::string::npos) {
                    return;
                }

                throw;
            }

            throw std::runtime_error("binary patch guard accepted invalid request");
        };
        refused("Launcher", "unsupported binary step");
        refused("QGIME", "exact native producers required");
        std::cout << "partial binary patch node and producer guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
