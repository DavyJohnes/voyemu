#include "error.hpp"
#include "library.hpp"
#include "rsa.hpp"
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace voyemu::os {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_CRYPTO", "invalid pinned RSA identity or signature");
}

// The OpenSSL 3 libcrypto functions that signing calls, declared here instead of by the OpenSSL
// headers; the types stay opaque.
struct Crypto {
    void *(*d2i_private_key)(int type, void **key, const unsigned char **input, long length);
    void *(*d2i_x509)(void **certificate, const unsigned char **input, long length);
    void *(*x509_get_pubkey)(void *certificate);
    void (*x509_free)(void *certificate);
    int (*pkey_eq)(const void *first, const void *second);
    int (*pkey_get_base_id)(const void *key);
    int (*pkey_get_bits)(const void *key);
    void (*pkey_free)(void *key);
    int (*i2d_pubkey)(const void *key, unsigned char **output);
    void *(*md_ctx_new)();
    void (*md_ctx_free)(void *context);
    const void *(*sha256)();
    int (*digest_sign_init)(void *context, void **key_context, const void *digest, void *engine,
                            void *key);
    int (*digest_sign)(void *context, unsigned char *signature, std::size_t *length,
                       const unsigned char *message, std::size_t message_length);
    int (*digest_verify_init)(void *context, void **key_context, const void *digest, void *engine,
                              void *key);
    int (*digest_verify)(void *context, const unsigned char *signature, std::size_t length,
                         const unsigned char *message, std::size_t message_length);
    void (*err_clear_error)();
};
constexpr int pkey_rsa = 6;
struct Loaded {
    explicit Loaded(const std::string &name) : library(name) {}

    Library library;
    Crypto crypto{};
};

// Linux distributions ship OpenSSL 3, so Voyemu links no cryptography of its own; a loaded library
// stays loaded for the life of the process.
const Crypto &crypto(const std::string &library) {
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<Loaded>> loaded;
    const std::lock_guard lock(mutex);

    if (const auto found = loaded.find(library); found != loaded.end()) {
        return found->second->crypto;
    }

    auto candidate = std::make_unique<Loaded>(library);

    if (!candidate->library.loaded()) {
        throw VoyemuError("APK_CRYPTO_LIBRARY",
                          library + " is required for APK signing; install the system OpenSSL 3: " +
                                  candidate->library.failure());
    }

    auto resolve = [&](auto &function, const char *name) {
        function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                candidate->library.symbol(name));

        if (!function) {
            throw VoyemuError("APK_CRYPTO_LIBRARY", library + " lacks " + name);
        }
    };
    auto &api = candidate->crypto;
    resolve(api.d2i_private_key, "d2i_PrivateKey");
    resolve(api.d2i_x509, "d2i_X509");
    resolve(api.x509_get_pubkey, "X509_get_pubkey");
    resolve(api.x509_free, "X509_free");
    resolve(api.pkey_eq, "EVP_PKEY_eq");
    resolve(api.pkey_get_base_id, "EVP_PKEY_get_base_id");
    resolve(api.pkey_get_bits, "EVP_PKEY_get_bits");
    resolve(api.pkey_free, "EVP_PKEY_free");
    resolve(api.i2d_pubkey, "i2d_PUBKEY");
    resolve(api.md_ctx_new, "EVP_MD_CTX_new");
    resolve(api.md_ctx_free, "EVP_MD_CTX_free");
    resolve(api.sha256, "EVP_sha256");
    resolve(api.digest_sign_init, "EVP_DigestSignInit");
    resolve(api.digest_sign, "EVP_DigestSign");
    resolve(api.digest_verify_init, "EVP_DigestVerifyInit");
    resolve(api.digest_verify, "EVP_DigestVerify");
    resolve(api.err_clear_error, "ERR_clear_error");

    return loaded.emplace(library, std::move(candidate)).first->second->crypto;
}

const Crypto &crypto() {
    return crypto("libcrypto.so.3");
}

const unsigned char *octets(std::string_view bytes) {
    return reinterpret_cast<const unsigned char *>(bytes.data());
}

// Owns one opaque libcrypto object.
struct Object {
    void *value;
    void (*release)(void *);

    Object(void *object, void (*free)(void *)) : value(object), release(free) {}

    ~Object() {
        if (value) {
            release(value);
        }
    }

    Object(const Object &) = delete;
    Object &operator=(const Object &) = delete;
};

// The RSA public key of 2048 to 16384 bits in the certificate.
Object certificate_key(const Crypto &api, std::string_view certificate) {
    auto *input = octets(certificate);
    Object x509(api.d2i_x509(nullptr, &input, static_cast<long>(certificate.size())),
                api.x509_free);

    if (!x509.value || input != octets(certificate) + certificate.size()) {
        invalid();
    }

    Object key(api.x509_get_pubkey(x509.value), api.pkey_free);

    if (!key.value || api.pkey_get_base_id(key.value) != pkey_rsa ||
        api.pkey_get_bits(key.value) < 2048 || api.pkey_get_bits(key.value) > 16384) {
        invalid();
    }

    return Object(std::exchange(key.value, nullptr), api.pkey_free);
}
} // namespace

void load_libcrypto(const std::string &library) {
    (void)crypto(library);
}

std::string rsa_sign_sha256(const RsaPrivateKey &rsa, std::string_view certificate,
                            std::string_view message) {
    const auto &api = crypto();
    struct Errors {
        const Crypto &api;

        ~Errors() {
            api.err_clear_error();
        }
    } errors{api};
    auto *input = octets(rsa.der);
    Object key(api.d2i_private_key(pkey_rsa, nullptr, &input, static_cast<long>(rsa.der.size())),
               api.pkey_free);

    if (!key.value || input != octets(rsa.der) + rsa.der.size()) {
        invalid();
    }

    const auto public_key = certificate_key(api, certificate);

    if (api.pkey_eq(key.value, public_key.value) != 1) {
        invalid();
    }

    Object signing(api.md_ctx_new(), api.md_ctx_free);
    std::size_t size = 0;

    if (!signing.value ||
        api.digest_sign_init(signing.value, nullptr, api.sha256(), nullptr, key.value) != 1 ||
        api.digest_sign(signing.value, nullptr, &size, octets(message), message.size()) != 1 ||
        size < 256 || size > 2048) {
        invalid();
    }

    std::string signature(size, '\0');
    Object verifying(api.md_ctx_new(), api.md_ctx_free);

    if (api.digest_sign(signing.value, reinterpret_cast<unsigned char *>(signature.data()), &size,
                        octets(message), message.size()) != 1 ||
        size != signature.size() || !verifying.value ||
        api.digest_verify_init(verifying.value, nullptr, api.sha256(), nullptr, public_key.value) !=
                1 ||
        api.digest_verify(verifying.value, octets(signature), signature.size(), octets(message),
                          message.size()) != 1) {
        invalid();
    }

    return signature;
}

std::string rsa_public_key_info(std::string_view certificate) {
    const auto &api = crypto();
    struct Errors {
        const Crypto &api;

        ~Errors() {
            api.err_clear_error();
        }
    } errors{api};
    const auto key = certificate_key(api, certificate);
    const auto size = api.i2d_pubkey(key.value, nullptr);

    if (size <= 0 || size > 4096) {
        invalid();
    }

    std::string encoded(static_cast<std::size_t>(size), '\0');
    auto *output = reinterpret_cast<unsigned char *>(encoded.data());

    if (api.i2d_pubkey(key.value, &output) != size) {
        invalid();
    }

    return encoded;
}
} // namespace voyemu::os
