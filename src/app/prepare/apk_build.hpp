#pragma once
#include "apk_dex.hpp"
#include "apk_identity.hpp"
#include "dex_instruction_patch.hpp"
#include "model.hpp"
#include "zip_archive.hpp"
#include <functional>
namespace voyemu {
struct ApkDexLiteralReplacement {
    std::string entry;
    DexLiteralPatch recipe;
};
enum class ApkRegisteredDexPatchKind {
    navi_msaa,
    launcher_media_redirect
};
struct ApkRegisteredDexPatch {
    std::string entry, source_sha256, output_sha256;
    ApkRegisteredDexPatchKind kind;
};
struct ApkRemoval {
    std::string name, sha256;
};
struct ApkBuildRequest {
    std::filesystem::path source, zipalign;
    std::string source_sha256, zipalign_sha256, base_certificate_sha256;
    std::vector<ApkDexPin> original;
    std::vector<ApkDexTemplate> templates;
    // DEX edits run through semantic fixed-width instruction guards, never arbitrary bytes.
    std::vector<ApkDexLiteralReplacement> literal_patches;
    std::vector<ApkRegisteredDexPatch> registered_patches;
    // Binary payload pins are checked by the ZIP rewriter; provenance is recorded below.
    std::vector<ZipAddition> additions;
    std::vector<ZipReplacement> replacements;
    // Registered ARM32 library removal only; never arbitrary APK entry deletion.
    std::vector<ApkRemoval> removals;
    Json binary_provenance = Json::object();
    ApkSigningIdentity identity;
    Json template_provenance = Json::object();
    std::function<void()> prepublication_guard;
};
struct ApkBuildDraft {
    std::filesystem::path apk;
    std::string sha256;
};
// Publishes app.apk and receipt.json together into a new private directory.
// Inputs are preserved; no Java, download, install or existing destination replacement.
ApkBuildDraft publish_native_apk_draft(const ApkBuildRequest &,
                                       const std::filesystem::path &destination,
                                       std::uint64_t reserve_bytes = 12ULL << 30);
} // namespace voyemu
