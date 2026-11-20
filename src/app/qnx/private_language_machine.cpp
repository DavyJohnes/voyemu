#include "qnx_prepare.hpp"
#include <iostream>

// Minimal supported producer path: source-coherent QNX closure of a qinggan11 adapter profile into
// a private .local/build output. Never starts a VM, never touches machines/.
int main(int argc, char **argv) {
    try {
        if (argc != 4) {
            std::cerr << "usage: private_language_machine ROOT PROFILE OUTPUT\n";

            return 2;
        }

        const std::filesystem::path root = std::filesystem::canonical(argv[1]);
        const std::filesystem::path output = argv[3];
        const auto receipt = voyemu::prepare_qnx_qinggan11_images(root, argv[2], output);
        std::cout << receipt.at("images").dump(2) << "\n";
        std::cout << "GUEST " << receipt.at("guest_build").dump(2) << "\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ERR: " << error.what() << "\n";

        return 1;
    }
}
