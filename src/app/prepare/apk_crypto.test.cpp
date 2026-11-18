#include "apk_crypto.hpp"
#include "process.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
using namespace voyemu;

static std::string bytes(const std::filesystem::path &file) {
    std::ifstream stream(file, std::ios::binary);

    if (!stream) {
        throw std::runtime_error("missing fixture " + file.string());
    }

    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// The fixtures are an RSA 2048 PKCS #8 key with its self-signed certificate, the certificate of
// another key, and the signature and SubjectPublicKeyInfo that OpenSSL computed for them; the
// PKCS #1 v1.5 signature is deterministic, so every host must produce the same bytes.
static void pinned_identity_checks(const std::filesystem::path &fixtures) {
    const auto key = bytes(fixtures / "key.pk8"), certificate = bytes(fixtures / "certificate.der"),
               other = bytes(fixtures / "other-certificate.der");
    const std::string message = "Voyemu APK signing test message";

    if (sign_apk_rsa_sha256(key, sha256_bytes(key), certificate, sha256_bytes(certificate),
                            message) != bytes(fixtures / "signature.bin")) {
        throw std::runtime_error("host RSA signature differs from the fixture");
    }

    if (apk_rsa_public_key_info(certificate, sha256_bytes(certificate)) !=
        bytes(fixtures / "public-key.der")) {
        throw std::runtime_error("host SubjectPublicKeyInfo differs from the fixture");
    }

    bool rejected = false;
    try {
        sign_apk_rsa_sha256(key, sha256_bytes(key), other, sha256_bytes(other), message);
    } catch (const std::runtime_error &error) {
        rejected = std::string_view(error.what()).starts_with("APK_CRYPTO:");
    }

    if (!rejected) {
        throw std::runtime_error("certificate of another key accepted");
    }
}

int main(int argc, char **argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("usage: apk_crypto_test FIXTURES");
        }

        pinned_identity_checks(argv[1]);
        const std::string cert = "invalid certificate", message = "signed APK data";

        for (const auto &key :
             {std::string(), std::string("\x30\x80", 2), std::string("\x30\x81\x01\0", 4),
              std::string("\x30\x03\x02\x01\x01", 5), std::string(16385, 'x')}) {
            bool rejected = false;
            try {
                sign_apk_rsa_sha256(key, sha256_bytes(key), cert, sha256_bytes(cert), message);
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("malformed identity accepted");
            }
        }

        bool rejected = false;
        try {
            sign_apk_rsa_sha256("key", std::string(64, '0'), cert, sha256_bytes(cert), message);
        } catch (...) {
            rejected = true;
        }

        if (!rejected) {
            throw std::runtime_error("wrong key pin accepted");
        }

        std::cout << "APK crypto signature and input guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
