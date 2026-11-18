#include "error.hpp"
#include "rsa.hpp"
#include <Security/Security.h>
#include <stdexcept>

namespace voyemu::os {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_CRYPTO", "invalid pinned RSA identity or signature");
}

template <class T> struct Ref {
    T value = nullptr;

    explicit Ref(T v = nullptr) : value(v) {}

    ~Ref() {
        if (value) {
            CFRelease(value);
        }
    }

    Ref(const Ref &) = delete;
    Ref &operator=(const Ref &) = delete;
};

Ref<CFDataRef> data(std::string_view bytes) {
    return Ref<CFDataRef>(CFDataCreate(
            kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(bytes.data()), bytes.size()));
}

std::string bytes(CFDataRef value) {
    if (!value) {
        invalid();
    }

    return std::string(reinterpret_cast<const char *>(CFDataGetBytePtr(value)),
                       CFDataGetLength(value));
}
} // namespace

// Security.framework imports the key in memory without a Keychain write.
std::string rsa_sign_sha256(const RsaPrivateKey &rsa, std::string_view certificate,
                            std::string_view message) {
    const auto encoded = data(rsa.der), certificate_data = data(certificate),
               message_data = data(message);

    if (!encoded.value || !certificate_data.value || !message_data.value) {
        invalid();
    }

    const void *keys[] = {kSecAttrKeyType, kSecAttrKeyClass};
    const void *values[] = {kSecAttrKeyTypeRSA, kSecAttrKeyClassPrivate};
    Ref<CFDictionaryRef> attributes(CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
                                                       &kCFTypeDictionaryKeyCallBacks,
                                                       &kCFTypeDictionaryValueCallBacks));

    if (!attributes.value) {
        invalid();
    }

    Ref<SecKeyRef> key(SecKeyCreateWithData(encoded.value, attributes.value, nullptr));
    Ref<SecCertificateRef> cert(
            SecCertificateCreateWithData(kCFAllocatorDefault, certificate_data.value));

    if (!key.value || !cert.value) {
        invalid();
    }

    Ref<SecKeyRef> public_key(SecKeyCopyPublicKey(key.value)),
            cert_key(SecCertificateCopyKey(cert.value));

    if (!public_key.value || !cert_key.value || SecKeyGetBlockSize(key.value) < 256 ||
        SecKeyGetBlockSize(key.value) > 2048) {
        invalid();
    }

    Ref<CFDataRef> key_bytes(SecKeyCopyExternalRepresentation(public_key.value, nullptr)),
            cert_bytes(SecKeyCopyExternalRepresentation(cert_key.value, nullptr));

    if (bytes(key_bytes.value) != bytes(cert_bytes.value) ||
        !SecKeyIsAlgorithmSupported(key.value, kSecKeyOperationTypeSign,
                                    kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256)) {
        invalid();
    }

    Ref<CFDataRef> signature(
            SecKeyCreateSignature(key.value, kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256,
                                  message_data.value, nullptr));

    if (!signature.value ||
        !SecKeyVerifySignature(cert_key.value, kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256,
                               message_data.value, signature.value, nullptr)) {
        invalid();
    }

    return bytes(signature.value);
}

std::string rsa_public_key_info(std::string_view certificate) {
    const auto encoded = data(certificate);

    if (!encoded.value) {
        invalid();
    }

    Ref<SecCertificateRef> cert(SecCertificateCreateWithData(kCFAllocatorDefault, encoded.value));

    if (!cert.value) {
        invalid();
    }

    Ref<SecKeyRef> key(SecCertificateCopyKey(cert.value));

    if (!key.value) {
        invalid();
    }

    Ref<CFDictionaryRef> attrs(SecKeyCopyAttributes(key.value));

    if (!attrs.value) {
        invalid();
    }

    const auto type = CFDictionaryGetValue(attrs.value, kSecAttrKeyType);

    if (!type || !CFEqual(type, kSecAttrKeyTypeRSA) || SecKeyGetBlockSize(key.value) < 256 ||
        SecKeyGetBlockSize(key.value) > 2048) {
        invalid();
    }

    Ref<CFDataRef> public_key(SecKeyCopyExternalRepresentation(key.value, nullptr));
    const auto public_bytes = bytes(public_key.value);
    auto der = [](unsigned tag, const std::string &value) {
        std::string output(1, char(tag));

        if (value.size() < 128) {
            output += char(value.size());
        } else {
            std::string length;
            auto size = value.size();

            while (size) {
                length.insert(length.begin(), char(size));
                size >>= 8;
            }

            output += char(128 | length.size());
            output += length;
        }

        return output + value;
    };
    const std::string algorithm("\x30\x0d\x06\x09\x2a\x86\x48\x86\xf7\x0d\x01\x01\x01\x05\x00", 15);

    return der(0x30, algorithm + der(0x03, std::string(1, '\0') + public_bytes));
}
} // namespace voyemu::os
