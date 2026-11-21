#include "android_adb_test_host.hpp"
#include <fstream>
#include <iterator>
#include <stdexcept>

std::string read_probe_output(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);

    if (!file) {
        throw std::runtime_error("missing probe output");
    }

    return std::string(std::istreambuf_iterator<char>(file), {});
}

bool await_probe_child(std::int64_t) {
    return true;
}
