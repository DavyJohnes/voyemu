#include "qnx_prepare.hpp"
#include <iostream>

// Minimal supported producer path: source-coherent Free A11 7.3.1 QNX closure into a
// private .local/build output. Never starts a VM, never touches machines/.
int main(int argc, char **argv) {
    try {
        if (argc != 3) {
            std::cerr << "usage: private_language_machine ROOT OUTPUT\n";

            return 2;
        }

        const std::filesystem::path root = std::filesystem::canonical(argv[1]);
        const std::filesystem::path output = argv[2];
        const auto receipt = voyemu::prepare_qnx_qinggan11_images(
                root, "free-11-chinese-20260731-7.3.1", output);
        std::cout << receipt.at("images").dump(2) << "\n";
        std::cout << "GUEST " << receipt.at("guest_build").dump(2) << "\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "ERR: " << error.what() << "\n";

        return 1;
    }
}
