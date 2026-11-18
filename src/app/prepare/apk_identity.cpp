#include "apk_identity.hpp"
#include "apk_crypto.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <utility>
namespace voyemu {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_IDENTITY",
                      "explicit pinned identity matching the selected base required");
}

std::string read(const std::filesystem::path &path, const std::string &expected,
                 std::size_t maximum) {
    if (!sha256_text(expected) || std::filesystem::is_symlink(path) ||
        !std::filesystem::is_regular_file(path)) {
        invalid();
    }

    const auto size = std::filesystem::file_size(path);

    if (!size || size > maximum) {
        invalid();
    }

    std::ifstream file(path, std::ios::binary);
    std::string bytes(size, '\0');
    file.read(bytes.data(), bytes.size());

    if (!file || file.peek() != std::char_traits<char>::eof() || sha256_bytes(bytes) != expected) {
        invalid();
    }

    return bytes;
}

std::string certificate_der(std::string_view pem) {
    constexpr std::string_view begin = "-----BEGIN CERTIFICATE-----",
                               end = "-----END CERTIFICATE-----";

    if (!pem.starts_with(begin)) {
        invalid();
    }

    pem.remove_prefix(begin.size());

    if (pem.starts_with("\r\n")) {
        pem.remove_prefix(2);
    } else if (pem.starts_with("\n")) {
        pem.remove_prefix(1);
    } else {
        invalid();
    }

    const auto ending = pem.find(end);

    if (ending == std::string_view::npos) {
        invalid();
    }

    const auto tail = pem.substr(ending + end.size());

    if (!tail.empty() && tail != "\n" && tail != "\r\n") {
        invalid();
    }

    std::string encoded;

    for (const auto c : pem.substr(0, ending)) {
        if (c == '\r' || c == '\n') {
            continue;
        }

        encoded += c;
    }

    if (encoded.empty() || encoded.size() % 4) {
        invalid();
    }

    constexpr std::string_view alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;

    for (std::size_t at = 0; at < encoded.size(); at += 4) {
        unsigned n = 0, padding = 0;

        for (unsigned i = 0; i < 4; ++i) {
            const auto c = encoded[at + i];

            if (c == '=') {
                if (i < 2 || at + 4 != encoded.size()) {
                    invalid();
                }

                ++padding;
                n <<= 6;
            } else {
                const auto v = alphabet.find(c);

                if (v == std::string_view::npos || padding) {
                    invalid();
                }

                n = (n << 6) | v;
            }
        }

        if (padding > 2 || (padding == 1 && (n & 255)) || (padding == 2 && (n & 65535))) {
            invalid();
        }

        output += char(n >> 16);

        if (padding < 2) {
            output += char(n >> 8);
        }

        if (!padding) {
            output += char(n);
        }
    }

    if (output.empty() || output.size() > 65536) {
        invalid();
    }

    return output;
}
} // namespace

std::string decode_pinned_apk_certificate_pem(const std::string &pem, const std::string &pem_pin,
                                              const std::string &base_pin) {
    if (pem.empty() || pem.size() > 131072 || !sha256_text(pem_pin) || !sha256_text(base_pin) ||
        sha256_bytes(pem) != pem_pin) {
        invalid();
    }

    auto cert = certificate_der(pem);

    if (sha256_bytes(cert) != base_pin) {
        invalid();
    }

    return cert;
}

ApkSigningIdentity load_pinned_apk_identity(const std::filesystem::path &key,
                                            const std::string &key_pin,
                                            const std::filesystem::path &pem,
                                            const std::string &pem_pin,
                                            const std::string &base_pin) {
    if (!sha256_text(base_pin)) {
        invalid();
    }

    auto key_bytes = read(key, key_pin, 16384);
    const auto pem_bytes = read(pem, pem_pin, 131072);
    auto cert = decode_pinned_apk_certificate_pem(pem_bytes, pem_pin, base_pin);
    // Validate DER, RSA type and the key/certificate pair before returning any identity.
    sign_apk_rsa_sha256(key_bytes, key_pin, cert, base_pin, "Voyemu APK identity validation");

    return {std::move(key_bytes), key_pin, std::move(cert), base_pin};
}
} // namespace voyemu
