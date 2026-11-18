#include "core/process.hpp"
#include "error.hpp"
#include "rsa.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <array>
#include <cstring>
#include <stdexcept>

namespace voyemu::os {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_CRYPTO", "invalid pinned RSA identity or signature");
}

struct CngKey {
    BCRYPT_KEY_HANDLE value = nullptr;
    BCRYPT_ALG_HANDLE algorithm = nullptr;

    ~CngKey() {
        if (value) {
            BCryptDestroyKey(value);
        }

        if (algorithm) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    }

    CngKey() = default;
    CngKey(const CngKey &) = delete;
    CngKey &operator=(const CngKey &) = delete;
};
struct Certificate {
    PCCERT_CONTEXT value = nullptr;

    explicit Certificate(std::string_view der)
        : value(CertCreateCertificateContext(X509_ASN_ENCODING,
                                             reinterpret_cast<const BYTE *>(der.data()),
                                             static_cast<DWORD>(der.size()))) {
        if (!value) {
            invalid();
        }

        const auto *oid = value->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId;

        if (!oid || std::string_view(oid) != szOID_RSA_RSA) {
            CertFreeCertificateContext(value);
            value = nullptr;
            invalid();
        }
    }

    ~Certificate() {
        if (value) {
            CertFreeCertificateContext(value);
        }
    }

    Certificate(const Certificate &) = delete;
    Certificate &operator=(const Certificate &) = delete;
};

std::string export_public(BCRYPT_KEY_HANDLE key) {
    ULONG size = 0;

    if (BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0) < 0 ||
        size < sizeof(BCRYPT_RSAKEY_BLOB) || size > 4096) {
        invalid();
    }

    std::string result(size, '\0');

    if (BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB,
                        reinterpret_cast<PUCHAR>(result.data()), size, &size, 0) < 0 ||
        size != result.size()) {
        invalid();
    }

    return result;
}

void import_private(const RsaPrivateKey &rsa, CngKey &key) {
    const auto modulus = rsa.modulus, exponent = rsa.public_exponent,
               private_exponent = rsa.private_exponent, prime1 = rsa.prime1, prime2 = rsa.prime2,
               exponent1 = rsa.exponent1, exponent2 = rsa.exponent2, coefficient = rsa.coefficient;
    BCRYPT_RSAKEY_BLOB header{};

    header.Magic = BCRYPT_RSAFULLPRIVATE_MAGIC;
    header.BitLength = static_cast<ULONG>(modulus.size() * 8);
    header.cbPublicExp = static_cast<ULONG>(exponent.size());
    header.cbModulus = static_cast<ULONG>(modulus.size());
    header.cbPrime1 = static_cast<ULONG>(prime1.size());
    header.cbPrime2 = static_cast<ULONG>(prime2.size());
    std::string blob(reinterpret_cast<const char *>(&header), sizeof header);
    struct Scrub {
        std::string &bytes;

        ~Scrub() {
            if (!bytes.empty()) {
                SecureZeroMemory(bytes.data(), bytes.size());
            }
        }
    } scrub{blob};
    blob += exponent;
    blob += modulus;
    blob += prime1;
    blob += prime2;
    auto append = [&](std::string_view value, std::size_t width) {
        if (value.size() > width) {
            invalid();
        }

        blob.append(width - value.size(), '\0');
        blob += value;
    };
    append(exponent1, prime1.size());
    append(exponent2, prime2.size());
    append(coefficient, prime1.size());
    append(private_exponent, modulus.size());

    if (BCryptOpenAlgorithmProvider(&key.algorithm, BCRYPT_RSA_ALGORITHM, nullptr, 0) < 0 ||
        BCryptImportKeyPair(key.algorithm, nullptr, BCRYPT_RSAFULLPRIVATE_BLOB, &key.value,
                            reinterpret_cast<PUCHAR>(blob.data()), static_cast<ULONG>(blob.size()),
                            0) < 0) {
        invalid();
    }
}

void import_certificate(const Certificate &certificate, CngKey &key) {
    if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING,
                                     &certificate.value->pCertInfo->SubjectPublicKeyInfo, 0,
                                     nullptr, &key.value)) {
        invalid();
    }

    const auto blob = export_public(key.value);
    BCRYPT_RSAKEY_BLOB header{};
    std::memcpy(&header, blob.data(), sizeof header);

    if (header.Magic != BCRYPT_RSAPUBLIC_MAGIC || header.BitLength < 2048 ||
        header.BitLength > 16384) {
        invalid();
    }
}
} // namespace

// CNG signs with a key imported from its own private key blob.
std::string rsa_sign_sha256(const RsaPrivateKey &rsa, std::string_view certificate,
                            std::string_view message) {
    CngKey key, public_key;

    import_private(rsa, key);
    Certificate cert(certificate);
    import_certificate(cert, public_key);

    if (export_public(key.value) != export_public(public_key.value)) {
        invalid();
    }

    const auto hash = sha256_bytes(message);
    std::array<unsigned char, 32> digest{};

    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<unsigned char>(std::stoul(hash.substr(i * 2, 2), nullptr, 16));
    }

    BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
    ULONG size = 0;

    if (BCryptSignHash(key.value, &padding, digest.data(), static_cast<ULONG>(digest.size()),
                       nullptr, 0, &size, BCRYPT_PAD_PKCS1) < 0 ||
        size < 256 || size > 2048) {
        invalid();
    }

    std::string signature(size, '\0');

    if (BCryptSignHash(key.value, &padding, digest.data(), static_cast<ULONG>(digest.size()),
                       reinterpret_cast<PUCHAR>(signature.data()), size, &size,
                       BCRYPT_PAD_PKCS1) < 0 ||
        size != signature.size() ||
        BCryptVerifySignature(
                public_key.value, &padding, digest.data(), static_cast<ULONG>(digest.size()),
                reinterpret_cast<PUCHAR>(signature.data()), size, BCRYPT_PAD_PKCS1) < 0) {
        invalid();
    }

    return signature;
}

std::string rsa_public_key_info(std::string_view certificate) {
    Certificate cert(certificate);
    CngKey public_key;

    import_certificate(cert, public_key);
    DWORD size = 0;
    const auto *info = &cert.value->pCertInfo->SubjectPublicKeyInfo;

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, info, 0, nullptr, nullptr,
                             &size) ||
        !size || size > 4096) {
        invalid();
    }

    std::string encoded(size, '\0');

    if (!CryptEncodeObjectEx(X509_ASN_ENCODING, X509_PUBLIC_KEY_INFO, info, 0, nullptr,
                             encoded.data(), &size) ||
        size != encoded.size()) {
        invalid();
    }

    return encoded;
}
} // namespace voyemu::os
