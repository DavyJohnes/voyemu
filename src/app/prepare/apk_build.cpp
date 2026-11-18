#include "apk_build.hpp"
#include "apk_alignment.hpp"
#include "apk_crypto.hpp"
#include "apk_sign.hpp"
#include "check.hpp"
#include "dex_method_redirect.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include <map>
#include <regex>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <stdio.h>
#endif
namespace voyemu {
namespace {
bool old_v1_signature(std::string name) {
    for (auto &c : name) {
        if (c >= 'a' && c <= 'z') {
            c -= 'a' - 'A';
        }
    }

    if (!name.starts_with("META-INF/")) {
        return false;
    }

    const auto leaf = name.substr(9);

    if (leaf.empty() || leaf.find('/') != std::string::npos) {
        return false;
    }

    return leaf == "MANIFEST.MF" || leaf.ends_with(".SF") || leaf.ends_with(".RSA") ||
           leaf.ends_with(".DSA") || leaf.ends_with(".EC");
}

void check_binary(const std::string &name, const std::string &payload, const std::string &pin) {
    if (!name.starts_with("lib/arm64-v8a/") || !name.ends_with(".so") ||
        name.substr(14).find('/') != std::string::npos) {
        throw VoyemuError("APK_DRAFT_BINARY", "ARM64 library entry required");
    }

    if (payload.size() < 20 || payload.size() > 32 * 1024 * 1024 || sha256_bytes(payload) != pin ||
        payload.substr(0, 4) != std::string("\x7f"
                                            "ELF",
                                            4) ||
        static_cast<unsigned char>(payload[4]) != 2 ||
        static_cast<unsigned char>(payload[5]) != 1 ||
        static_cast<unsigned char>(payload[16]) != 3 || payload[17] != 0 ||
        static_cast<unsigned char>(payload[18]) != 183 || payload[19] != 0) {
        throw VoyemuError("APK_DRAFT_BINARY", "pinned ARM64 shared ELF required");
    }
}
} // namespace

ApkBuildDraft publish_native_apk_draft(const ApkBuildRequest &request,
                                       const std::filesystem::path &destination,
                                       std::uint64_t reserve) {
    if (!destination.is_absolute() || destination.filename().empty() ||
        destination.filename() == "." || destination.filename() == ".." ||
        std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        throw VoyemuError("APK_DRAFT_PATH", "new absolute directory required");
    }

    if (request.templates.empty() && request.additions.empty() && request.replacements.empty() &&
        request.literal_patches.empty() && request.registered_patches.empty() &&
        request.removals.empty()) {
        throw VoyemuError("APK_DRAFT_INPUT", "DEX templates or binary patches required");
    }

    if (!request.binary_provenance.is_object() ||
        request.binary_provenance.dump().size() > 128 * 1024) {
        throw VoyemuError("APK_DRAFT_PROVENANCE", "bounded binary object required");
    }

    if ((!request.additions.empty() || !request.replacements.empty() ||
         !request.removals.empty()) &&
        request.binary_provenance.empty()) {
        throw VoyemuError("APK_DRAFT_PROVENANCE", "binary producer provenance required");
    }

    for (const auto &item : request.additions) {
        check_binary(item.name, item.bytes, item.sha256);
    }

    for (const auto &item : request.replacements) {
        check_binary(item.name, item.bytes, item.sha256);
    }

    static const std::map<std::string, std::string> registered_removals = {
            {"lib/armeabi-v7a/libBugly.so",
             "e3094e2d13dd6f65ceb9138d8562dea219718d9f0536ac19f6c9f1d17da95c20"},
            {"lib/armeabi-v7a/libpl_droidsonroids_gif.so",
             "4f8e8ef4dd9c05ca84e797522d301dcf165d5a4879456707be5787acdeaab402"},
            {"lib/armeabi-v7a/libjni_qgHandWrite.so",
             "0726317b7f88149f493ea342f1fa51e9185f1845bd823416d47690f1f099dbb3"},
            {"lib/armeabi-v7a/libjni_qgime.so",
             "4bedfd2d15fa79adb770e3864583d058e464f1cef7a3f64918d5e38fab023221"}};

    if (request.removals.size() > registered_removals.size()) {
        throw VoyemuError("APK_DRAFT_REMOVAL", "bounded registered removals required");
    }

    std::set<std::string> removal_names;
    Json removal_provenance = Json::array();

    for (const auto &item : request.removals) {
        const auto registered = registered_removals.find(item.name);

        if (registered == registered_removals.end() || registered->second != item.sha256 ||
            !removal_names.insert(item.name).second) {
            throw VoyemuError("APK_DRAFT_REMOVAL",
                              "unique registered ARM32 library and pin required");
        }

        for (const auto &addition : request.additions) {
            if (addition.name == item.name) {
                throw VoyemuError("APK_DRAFT_REMOVAL", "addition overlap");
            }
        }

        for (const auto &replacement : request.replacements) {
            if (replacement.name == item.name) {
                throw VoyemuError("APK_DRAFT_REMOVAL", "replacement overlap");
            }
        }

        read_pinned_zip_entry(request.source, request.source_sha256, item.name, item.sha256,
                              32 * 1024 * 1024);
        removal_provenance.push_back({{"entry", item.name}, {"sha256", item.sha256}});
    }

    if (!request.template_provenance.is_object() ||
        request.template_provenance.dump().size() > 128 * 1024) {
        throw VoyemuError("APK_DRAFT_PROVENANCE", "bounded object required");
    }

    if (request.literal_patches.size() > 32 ||
        request.registered_patches.size() > 32 - request.literal_patches.size()) {
        throw VoyemuError("APK_DRAFT_DEX", "bounded literal patch count required");
    }

    std::set<std::string> patched_dex;
    std::vector<ZipReplacement> replacements = request.replacements;
    Json literal_provenance = Json::array();
    std::uint64_t patched_bytes = 0;

    for (const auto &patch : request.literal_patches) {
        if (patch.entry.size() > 32 ||
            !std::regex_match(patch.entry, std::regex(R"(^classes([2-9]|[1-9][0-9]+)?\.dex$)")) ||
            !patched_dex.insert(patch.entry).second) {
            throw VoyemuError("APK_DRAFT_DEX", "unique original DEX entry required");
        }

        const auto &r = patch.recipe;
        const auto original_dex = read_pinned_zip_entry(request.source, request.source_sha256,
                                                        patch.entry, r.source_sha256,
                                                        32 * 1024 * 1024);

        if (original_dex.size() > (128ULL << 20) - patched_bytes) {
            throw VoyemuError("APK_DRAFT_DEX", "patch payload budget exceeded");
        }

        patched_bytes += original_dex.size();
        auto patched = patch_dex_literal(original_dex, r);
        replacements.push_back({patch.entry, r.source_sha256, std::move(patched), r.output_sha256});
        literal_provenance.push_back({{"entry", patch.entry},
                                      {"type", "const/16"},
                                      {"source_sha256", r.source_sha256},
                                      {"output_sha256", r.output_sha256},
                                      {"owner", r.owner},
                                      {"method", r.method},
                                      {"prototype", r.prototype},
                                      {"instructions_sha256", r.instructions_sha256},
                                      {"access", r.access},
                                      {"code_unit", r.code_unit},
                                      {"instruction_units", r.instruction_units},
                                      {"registers", r.registers},
                                      {"inputs", r.inputs},
                                      {"outputs", r.outputs},
                                      {"tries", r.tries},
                                      {"reg", r.reg},
                                      {"before", r.before},
                                      {"after", r.after}});
    }

    Json registered_provenance = Json::array();

    for (const auto &patch : request.registered_patches) {
        const char *name = nullptr;

        switch (patch.kind) {
        case ApkRegisteredDexPatchKind::navi_msaa:
            name = "navi-msaa";
            break;
        case ApkRegisteredDexPatchKind::launcher_media_redirect:
            name = "launcher-media-redirect";
            break;
        default:
            throw VoyemuError("APK_DRAFT_DEX", "unregistered patch kind");
        }

        if (patch.entry.size() > 32 ||
            !std::regex_match(patch.entry, std::regex(R"(^classes([2-9]|[1-9][0-9]+)?\.dex$)")) ||
            !patched_dex.insert(patch.entry).second) {
            throw VoyemuError("APK_DRAFT_DEX", "unique original DEX entry required");
        }

        const auto valid_pin = [](const std::string &value) {
            return sha256_text(value);
        };

        if (!valid_pin(patch.source_sha256) || !valid_pin(patch.output_sha256)) {
            throw VoyemuError("APK_DRAFT_DEX", "registered patch pins required");
        }

        const auto original_dex = read_pinned_zip_entry(request.source, request.source_sha256,
                                                        patch.entry, patch.source_sha256,
                                                        32 * 1024 * 1024);

        if (original_dex.size() > (128ULL << 20) - patched_bytes) {
            throw VoyemuError("APK_DRAFT_DEX", "patch payload budget exceeded");
        }

        patched_bytes += original_dex.size();
        auto patched = patch.kind == ApkRegisteredDexPatchKind::navi_msaa
                               ? patch_navi_msaa(original_dex)
                               : patch_launcher_media_redirect(original_dex);

        if (patched.size() != original_dex.size() || sha256_bytes(patched) != patch.output_sha256) {
            throw VoyemuError("APK_DRAFT_DEX", "registered output pin/extent mismatch");
        }

        replacements.push_back(
                {patch.entry, patch.source_sha256, std::move(patched), patch.output_sha256});
        registered_provenance.push_back({{"entry", patch.entry},
                                         {"recipe", name},
                                         {"source_sha256", patch.source_sha256},
                                         {"output_sha256", patch.output_sha256},
                                         {"fixed_width", true},
                                         {"metadata_preserved", true}});
    }

    const auto &identity = request.identity;

    if (identity.certificate_sha256 != request.base_certificate_sha256) {
        throw VoyemuError("APK_DRAFT_IDENTITY", "selected base certificate mismatch");
    }

    // Validate the pinned key/certificate pair before creating any candidate or running a tool.
    sign_apk_rsa_sha256(identity.pkcs8, identity.key_sha256, identity.certificate_der,
                        identity.certificate_sha256, "voyemu APK draft identity check");
    ensure_private_directory(destination.parent_path());
    const auto pending = destination.parent_path() /
                         ("." + destination.filename().string() + ".pending-" + random_token());

    if (!std::filesystem::create_directory(pending)) {
        throw VoyemuError("APK_DRAFT_CREATE", "exclusive stage failed");
    }

    struct Cleanup {
        std::filesystem::path path;
        bool owned = true;

        ~Cleanup() {
            if (owned) {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        }
    } cleanup{pending};
    ensure_private_directory(pending);
    const auto appended = pending / "append.apk", aligned = pending / "aligned.apk",
               signed_file = pending / "app.apk";
    ApkDexAppendResult dex;
    const auto dex_file = pending / "dex.apk";
    auto rewrite_source = request.source;
    auto rewrite_pin = request.source_sha256;

    if (!request.templates.empty()) {
        dex = append_pinned_apk_dex(request.source, request.source_sha256, request.original,
                                    request.templates, dex_file, reserve);
        rewrite_source = dex_file;
        rewrite_pin = dex.sha256;
    }

    std::vector<std::string> removals;

    for (const auto &entry : pinned_zip_inventory(rewrite_source, rewrite_pin)) {
        if (!entry.directory && old_v1_signature(entry.name)) {
            removals.push_back(entry.name);
        }
    }

    const auto v1_removals = removals;

    for (const auto &item : request.removals) {
        removals.push_back(item.name);
    }

    const auto rewritten_sha = rewrite_pinned_zip(rewrite_source, rewrite_pin, replacements,
                                                  removals, request.additions, appended, reserve);
    const auto aligned_sha = align_pinned_apk(appended, rewritten_sha, request.zipalign,
                                              request.zipalign_sha256, aligned, reserve);
    const auto signed_sha = sign_pinned_apk_v2(aligned, aligned_sha, identity.pkcs8,
                                               identity.key_sha256, identity.certificate_der,
                                               identity.certificate_sha256, signed_file, reserve);
    verify_pinned_apk_alignment(signed_file, signed_sha);

    if (sha256_file(request.source) != request.source_sha256) {
        throw VoyemuError("APK_DRAFT_SOURCE", "source changed");
    }

    if (request.prepublication_guard) {
        request.prepublication_guard();
    }

    Json entries = Json::array();

    for (std::size_t i = 0; i < dex.slots.size(); ++i) {
        entries.push_back({{"entry", dex.slots[i].entry},
                           {"sha256", request.templates[i].sha256},
                           {"class_count", dex.slots[i].inventory.classes.size()},
                           {"allowlist_sha256",
                            sha256_bytes(Json(request.templates[i].expected_classes).dump())}});
    }

    Json binaries = Json::array();

    for (const auto &item : request.additions) {
        binaries.push_back({{"entry", item.name}, {"sha256", item.sha256}, {"action", "add"}});
    }

    for (const auto &item : request.replacements) {
        binaries.push_back({{"entry", item.name},
                            {"sha256", item.sha256},
                            {"original_sha256", item.original_sha256},
                            {"action", "replace"}});
    }

    write_private_json(pending / "receipt.json",
                       {{"schema", 1},
                        {"kind", "native-apk-draft"},
                        {"source_sha256", request.source_sha256},
                        {"zipalign_sha256", request.zipalign_sha256},
                        {"certificate_sha256", identity.certificate_sha256},
                        {"signature", "APK-v2-RSA-SHA256"},
                        {"apk", "app.apk"},
                        {"sha256", signed_sha},
                        {"dex", entries},
                        {"template_provenance", request.template_provenance},
                        {"binary_provenance", request.binary_provenance},
                        {"binaries", binaries},
                        {"removed_v1_signatures", v1_removals},
                        {"removed_arm32_libraries", removal_provenance},
                        {"dex_literal_patches", literal_provenance},
                        {"dex_registered_patches", registered_provenance}});

    if (!request.templates.empty() && !std::filesystem::remove(dex_file)) {
        throw VoyemuError("APK_DRAFT_CLEANUP", "DEX intermediate removal failed");
    }

    if (!std::filesystem::remove(appended) || !std::filesystem::remove(aligned)) {
        throw VoyemuError("APK_DRAFT_CLEANUP", "intermediate removal failed");
    }

    if (!os::rename_exclusive(pending, destination)) {
        throw VoyemuError("APK_DRAFT_PUBLISH", "exclusive directory rename failed");
    }

    cleanup.owned = false;

    return {destination / "app.apk", signed_sha};
}
} // namespace voyemu
