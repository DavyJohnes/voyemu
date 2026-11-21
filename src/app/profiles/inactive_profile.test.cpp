#include "inactive_profile.hpp"
#include "inactive_profile_test_host.hpp"
#include <fstream>
#include <iostream>
#include <thread>

int main(int argc, char **argv) try {
    return inactive_profile_case(argc, argv);
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
