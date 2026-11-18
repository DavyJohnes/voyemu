#include "apk_base.hpp"
#include "model.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

static void check(bool value, const char *reason) {
    if (!value) {
        throw std::runtime_error(reason);
    }
}

static std::string le(std::uint64_t value, unsigned size) {
    std::string out;

    for (unsigned i = 0; i < size; ++i) {
        out += char(value >> (8 * i));
    }

    return out;
}

static std::string lp(const std::string &value) {
    return le(value.size(), 4) + value;
}

static std::string fixture(const std::string &certificate, bool duplicate = false,
                           bool multiple = false, const std::string &terminal = {}) {
    const auto data = lp("digest") + lp(lp(certificate)) + lp("") + terminal;
    const auto signer = lp(data) + lp("signature") + lp("key");
    const auto value = lp(lp(signer) + (multiple ? lp(signer) : ""));
    const auto pair = le(4 + value.size(), 8) + le(0x7109871a, 4) + value;
    const auto pairs = pair + (duplicate ? pair : "");
    const auto block = le(pairs.size() + 24, 8) + pairs + le(pairs.size() + 24, 8) +
                       "APK Sig Block 42";

    return block + le(0x06054b50, 4) + le(0, 2) + le(0, 2) + le(0, 2) + le(0, 2) + le(0, 4) +
           le(block.size(), 4) + le(0, 2);
}

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string_view(argv[1]) == "--inspect") {
            const auto cert = pinned_apk_v2_certificate(std::filesystem::canonical(argv[2]),
                                                        argv[3]);
            std::cout << Json{{"certificate_sha256", sha256_bytes(cert)},
                              {"certificate_bytes", cert.size()},
                              {"signature_verification", false}}
                                 .dump()
                      << '\n';

            return 0;
        }

        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-apk-base-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::filesystem::remove_all(p);
            }
        } cleanup{root};
        const auto file = root / "base.apk";
        const std::string cert("\x30\x03\x02\x01\x01", 5);
        auto write = [&](const std::string &bytes) {
            std::ofstream out(file, std::ios::binary);
            out.write(bytes.data(), bytes.size());
            out.close();

            return sha256_file(file);
        };
        auto pin = write(fixture(cert));
        check(pinned_apk_v2_certificate(file, pin) == cert,
              "certificate extraction preserves selected bytes");
        pin = write(fixture(cert, false, false, lp("")));
        check(pinned_apk_v2_certificate(file, pin) == cert,
              "AOSP fourth empty signed-data field preserves exact certificate");

        for (const auto &terminal :
             std::vector<std::string>{lp("x"), lp("") + lp(""), std::string(3, '\0'),
                                      std::string("\0\0\0\1", 4), lp("") + "x"}) {
            pin = write(fixture(cert, false, false, terminal));
            bool malformed = false;
            try {
                pinned_apk_v2_certificate(file, pin);
            } catch (...) {
                malformed = true;
            }

            check(malformed, "nonempty/truncated/repeated/trailing signed-data fields rejected");
        }

        for (unsigned mode = 0; mode < 5; ++mode) {
            auto bytes = fixture(cert, mode == 0, mode == 1);

            if (mode == 2) {
                bytes[0] ^= 1;
            }

            if (mode == 3) {
                bytes[bytes.size() - 22 - 1] ^= 1;
            }

            if (mode == 4) {
                bytes += 'x';
            }

            const auto hash = write(bytes);
            bool rejected = false;
            try {
                pinned_apk_v2_certificate(file, hash);
            } catch (...) {
                rejected = true;
            }

            check(rejected, "ambiguous or malformed signing container accepted");
        }

        pin = write(fixture(cert));
        bool rejected = false;
        try {
            pinned_apk_v2_certificate(file, std::string(64, '0'));
        } catch (...) {
            rejected = true;
        }

        check(rejected, "unpinned base accepted");
        std::cout << "pinned base certificate container, ambiguity and bounds guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
