#pragma once
#include <string>
#include <string_view>
namespace voyemu {
// APK v2 algorithm 0x0103. Imports an in-memory RSA PKCS8 key without a Keychain write.
// Both key bytes and DER certificate are pinned; certificate public key must match.
// Returns raw signature bytes after verifying them using the certificate public key.
std::string sign_apk_rsa_sha256(std::string_view pkcs8, const std::string &key_sha256,
                                std::string_view certificate_der,
                                const std::string &certificate_sha256,
                                std::string_view signed_data);
// DER SubjectPublicKeyInfo for the pinned RSA certificate.
std::string apk_rsa_public_key_info(std::string_view certificate_der,
                                    const std::string &certificate_sha256);
} // namespace voyemu
