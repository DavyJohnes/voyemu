#include "apk_identity.hpp"
#include "model.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

int main() {
    try {
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("apk-identity-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const std::string valid = "-----BEGIN CERTIFICATE-----\nTQ==\n-----END CERTIFICATE-----\n";

        if (decode_pinned_apk_certificate_pem(valid, sha256_bytes(valid), sha256_bytes("M")) !=
            "M") {
            throw std::runtime_error("PEM bytes changed");
        }

        for (const std::string invalid :
             {std::string("-----BEGIN CERTIFICATE-----\nTR==\n-----END CERTIFICATE-----\n"),
              std::string("-----BEGIN CERTIFICATE-----\nTQ=A\n-----END CERTIFICATE-----\n"),
              valid + "extra"}) {
            bool rejected = false;
            try {
                decode_pinned_apk_certificate_pem(invalid, sha256_bytes(invalid),
                                                  sha256_bytes("M"));
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("noncanonical PEM accepted");
            }
        }

        const auto key = root / "key", pem = root / "cert";
        std::ofstream(key) << "not a private key";
        const auto key_pin = sha256_file(key);
        auto reject = [&](const std::string &text, const std::string &base) {
            std::ofstream out(pem, std::ios::binary);
            out << text;
            out.close();
            bool failed = false;
            try {
                load_pinned_apk_identity(key, key_pin, pem, sha256_file(pem), base);
            } catch (...) {
                failed = true;
            }

            if (!failed) {
                throw std::runtime_error("bad identity accepted");
            }
        };
        reject("-----BEGIN CERTIFICATE-----\nTQ==\n-----END CERTIFICATE-----\n",
               sha256_bytes("different base"));
        reject("-----BEGIN CERTIFICATE-----\nTR==\n-----END CERTIFICATE-----\n", sha256_bytes("M"));
        reject("-----BEGIN CERTIFICATE-----\nTQ=A\n-----END CERTIFICATE-----\n", sha256_bytes("M"));
        reject("-----BEGIN CERTIFICATE-----\nTQ==\n-----END CERTIFICATE-----\nextra",
               sha256_bytes("M"));
        reject("garbage", sha256_bytes("M"));
        reject("certificate", "");
        std::cout << "APK identity guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
