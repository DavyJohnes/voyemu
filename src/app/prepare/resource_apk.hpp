#pragma once
#include "apk_identity.hpp"
#include "model.hpp"
#include <functional>
namespace voyemu {
struct ResourceApkRequest {
    std::filesystem::path source, aapt2, framework, zipalign;
    std::string aapt2_sha256, framework_sha256, zipalign_sha256;
    std::string base_certificate_sha256;
    ApkSigningIdentity identity;
    std::function<void()> prepublication_guard;
};
// Registered statusbar overlay only: native compile/link, align and platform v2 sign.
// Publishes app.apk and receipt.json together; never replaces an existing directory.
Json publish_resource_apk(const ResourceApkRequest &, const std::filesystem::path &destination,
                          std::uint64_t reserve_bytes = 20ULL << 30);
} // namespace voyemu
