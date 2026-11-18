#include "os/rsa.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

using namespace voyemu;

// The library failure that load_libcrypto reports, or empty when it loads.
static std::string failure(const std::string &library) {
    try {
        os::load_libcrypto(library);
    } catch (const std::runtime_error &error) {
        return error.what();
    }

    return {};
}

int main() {
    try {
        const auto absent = failure("libvoyemu-absent-crypto.so.0");

        if (!absent.starts_with("APK_CRYPTO_LIBRARY: libvoyemu-absent-crypto.so.0 is required")) {
            throw std::runtime_error("absent libcrypto not reported: " + absent);
        }

        // The C library loads but has none of the OpenSSL functions.
        if (failure("libc.so.6") != "APK_CRYPTO_LIBRARY: libc.so.6 lacks d2i_PrivateKey") {
            throw std::runtime_error("incomplete libcrypto not reported");
        }

        std::cout << "libcrypto loading failures passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
