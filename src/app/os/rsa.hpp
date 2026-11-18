#pragma once
#include <string>
#include <string_view>

namespace voyemu::os {
// An RSA private key: its RSAPrivateKey (PKCS #1) DER and the unsigned big-endian integers in it.
struct RsaPrivateKey {
    std::string_view der, modulus, public_exponent, private_exponent, prime1, prime2, exponent1,
            exponent2, coefficient;
};
// The RSASSA-PKCS1-v1_5 SHA-256 signature of message by key, verified with the public key of the
// X.509 DER certificate, which must be the public half of key. Throws APK_CRYPTO when the host
// rejects the key, the certificate or the pair, or when the key is not 2048 to 16384 bits, and
// APK_CRYPTO_LIBRARY when the host lacks the system library that signs.
std::string rsa_sign_sha256(const RsaPrivateKey &key, std::string_view certificate,
                            std::string_view message);
// The DER SubjectPublicKeyInfo of the RSA key of 2048 to 16384 bits in the X.509 DER certificate;
// throws like rsa_sign_sha256.
std::string rsa_public_key_info(std::string_view certificate);
// Loads OpenSSL 3 libcrypto by file name from the system library directories and resolves every
// function the RSA backend calls; throws APK_CRYPTO_LIBRARY naming the library when it is absent or
// lacks one. Linux backend.
void load_libcrypto(const std::string &library);
} // namespace voyemu::os
