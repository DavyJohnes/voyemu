#include "apk_crypto.hpp"
#include "check.hpp"
#include "error.hpp"
#include "os/rsa.hpp"
#include "process.hpp"
#include <stdexcept>
namespace voyemu {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_CRYPTO", "invalid pinned RSA identity or signature");
}

struct Der {
    std::string_view bytes;

    std::string_view take(unsigned tag) {
        if (bytes.size() < 2 || static_cast<unsigned char>(bytes[0]) != tag) {
            invalid();
        }

        const auto first = static_cast<unsigned char>(bytes[1]);
        std::size_t size = first, header = 2;

        if (first & 128) {
            const unsigned count = first & 127;

            if (!count || count > 3 || bytes.size() < 2U + count || bytes[2] == 0) {
                invalid();
            }

            size = 0;

            for (unsigned i = 0; i < count; ++i) {
                size = (size << 8) | static_cast<unsigned char>(bytes[2 + i]);
            }

            header += count;

            if (size < 128) {
                invalid();
            }
        }

        if (header > bytes.size() || size > bytes.size() - header) {
            invalid();
        }

        auto value = bytes.substr(header, size);
        bytes.remove_prefix(header + size);

        return value;
    }
};

// The RSAPrivateKey (PKCS #1) DER with its positive minimal integers, the modulus of 2048 to 16384
// bits.
os::RsaPrivateKey rsa_private_key(std::string_view pkcs1) {
    Der container{pkcs1};
    Der sequence{container.take(0x30)};

    if (!container.bytes.empty() || sequence.take(0x02) != std::string_view("\0", 1)) {
        invalid();
    }

    auto integer = [&] {
        auto value = sequence.take(0x02);

        if (value.empty() || (static_cast<unsigned char>(value[0]) & 128)) {
            invalid();
        }

        if (value.size() > 1 && value[0] == 0) {
            value.remove_prefix(1);

            if (!(static_cast<unsigned char>(value[0]) & 128)) {
                invalid();
            }
        }

        return value;
    };
    os::RsaPrivateKey key;
    key.der = pkcs1;
    key.modulus = integer();
    key.public_exponent = integer();
    key.private_exponent = integer();
    key.prime1 = integer();
    key.prime2 = integer();
    key.exponent1 = integer();
    key.exponent2 = integer();
    key.coefficient = integer();

    if (!sequence.bytes.empty() || key.modulus.size() < 256 || key.modulus.size() > 2048 ||
        key.public_exponent.empty() || key.public_exponent.size() > 8 || key.prime1.empty() ||
        key.prime2.empty() || key.prime1.size() > key.modulus.size() ||
        key.prime2.size() > key.modulus.size() ||
        (static_cast<unsigned char>(key.modulus[0]) & 128) == 0) {
        invalid();
    }

    return key;
}
} // namespace

std::string sign_apk_rsa_sha256(std::string_view pkcs8, const std::string &key_pin,
                                std::string_view certificate, const std::string &cert_pin,
                                std::string_view message) {
    if (pkcs8.empty() || pkcs8.size() > 16384 || certificate.empty() ||
        certificate.size() > 65536 || message.empty() || message.size() > (1 << 20) ||
        !sha256_text(key_pin) || !sha256_text(cert_pin) || sha256_bytes(pkcs8) != key_pin ||
        sha256_bytes(certificate) != cert_pin) {
        invalid();
    }

    Der outer{pkcs8};
    Der sequence{outer.take(0x30)};

    if (!outer.bytes.empty() || sequence.take(0x02) != std::string_view("\0", 1)) {
        invalid();
    }

    const auto algorithm = sequence.take(0x30);
    const std::string_view rsa_algorithm("\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05\x00",
                                         13);

    if (algorithm != rsa_algorithm) {
        invalid();
    }

    const auto private_key = sequence.take(0x04);

    if (!sequence.bytes.empty() || private_key.empty()) {
        invalid();
    }

    return os::rsa_sign_sha256(rsa_private_key(private_key), certificate, message);
}

std::string apk_rsa_public_key_info(std::string_view certificate, const std::string &cert_pin) {
    if (certificate.empty() || certificate.size() > 65536 || !sha256_text(cert_pin) ||
        sha256_bytes(certificate) != cert_pin) {
        invalid();
    }

    return os::rsa_public_key_info(certificate);
}
} // namespace voyemu
