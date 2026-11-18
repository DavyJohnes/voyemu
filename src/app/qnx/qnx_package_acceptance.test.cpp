#include "qnx_acceptance.hpp"
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
            std::cout
                    << "Developer: --run ROOT PROFILE REQUEST.json FRESH_OUTPUT; starts one bounded isolated cold QNX VM and exports actual Screen PNG\n";

            return 0;
        }

        if (argc != 6 || std::string(argv[1]) != "--run") {
            throw std::runtime_error("explicit --run ROOT PROFILE REQUEST OUTPUT required");
        }

        std::cout << accept_qnx_images(std::filesystem::canonical(argv[2]), argv[3],
                                       std::filesystem::canonical(argv[4]),
                                       std::filesystem::absolute(argv[5]))
                             .dump()
                  << '\n';

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
