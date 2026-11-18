#pragma once
#include <filesystem>
#include <string>
namespace voyemu {
// Decodes one pinned PEM block; X.509/RSA validity is checked by the identity loader.
std::string decode_pinned_apk_certificate_pem(const std::string &pem, const std::string &pem_sha256,
                                              const std::string &base_certificate_sha256);
struct ApkSigningIdentity {
    std::string pkcs8, key_sha256, certificate_der, certificate_sha256;
};
// All inputs are explicit; base_certificate_sha256 must come from the selected clean base.
// Read-only, bounded; no Keychain access or identity fallback. Never logs key bytes.
ApkSigningIdentity load_pinned_apk_identity(const std::filesystem::path &key,
                                            const std::string &key_sha256,
                                            const std::filesystem::path &certificate_pem,
                                            const std::string &pem_sha256,
                                            const std::string &base_certificate_sha256);
} // namespace voyemu
