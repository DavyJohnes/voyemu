#include "embedded_data.hpp"
#include <cstdlib>
#include <functional>
#include <iostream>
#include <miniz.h>
#include <stdexcept>
#include <vector>
using namespace voyemu;

namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string failure(const std::function<void()> &action) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
}

// A raw deflate stream, as src/app/embedded_deflate.cmake embeds it.
std::vector<unsigned char> deflate(const std::string &bytes) {
    std::size_t length = 0;
    auto *stream = static_cast<unsigned char *>(tdefl_compress_mem_to_heap(
            bytes.data(), bytes.size(), &length, TDEFL_DEFAULT_MAX_PROBES));
    check(stream != nullptr, "fixture deflate");
    std::vector<unsigned char> result(stream, stream + length);
    mz_free(stream);

    return result;
}
} // namespace

int main() {
    try {
        std::string bytes;

        for (int index = 0; index < 4096; ++index) {
            bytes += "{\"index\":" + std::to_string(index) + "}\n";
        }

        bytes.push_back('\0');
        const auto stream = deflate(bytes);
        check(stream.size() < bytes.size() && inflate_embedded(stream, bytes.size()) == bytes,
              "the embedded bytes inflate to their source");
        check(failure([&] {
                  inflate_embedded(stream, bytes.size() - 1);
              }).starts_with("EMBEDDED_DATA:") &&
                      failure([&] {
                          inflate_embedded(stream, bytes.size() + 1);
                      }).starts_with("EMBEDDED_DATA:"),
              "a size other than the source size is rejected");
        check(failure([&] {
                  inflate_embedded(std::span(stream).first(stream.size() / 2), bytes.size());
              }).starts_with("EMBEDDED_DATA:"),
              "a truncated stream is rejected");
        std::cout << "embedded data ok\n";

        return EXIT_SUCCESS;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return EXIT_FAILURE;
    }
}
