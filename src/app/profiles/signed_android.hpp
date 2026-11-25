#pragma once
#include "model.hpp"
#include <map>

namespace voyemu {
struct SignedAndroidInput {
    std::filesystem::path release, public_key;
    std::string spki_sha256;
};

std::filesystem::path signed_android_tools(const std::filesystem::path &root);

// Registers the exact public PEM only after verifying its independently supplied DER SPKI pin.
void trust_signed_android_key(const std::filesystem::path &root, const std::filesystem::path &key,
                              const std::string &spki_sha256);

// Verifies the host release and returns the expected immutable SDK files and boot digest.
Json verify_signed_android_input(const std::filesystem::path &root, const SignedAndroidInput &input,
                                 const Json &profile);
// Read-only check of the accepted release's signature and immutable portable boot roles.
std::map<std::string, std::string>
checked_signed_android_boot_pins(const std::filesystem::path &root,
                                 const std::filesystem::path &seed, const Json &definition,
                                 const Json &manifest);
} // namespace voyemu
