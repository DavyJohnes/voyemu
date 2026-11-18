#include "apk_build.hpp"
#include "apk_base.hpp"
#include "dex_method_redirect.hpp"
#include "model.hpp"
#include "process.hpp"
#include "profile_binary_patches.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) {
    try {
        if (argc == 13 && std::string(argv[1]) == "--music-profile") {
            const auto patch = plan_profile_binary_patch(std::filesystem::canonical(argv[2]),
                                                         argv[3], "MusicConcate");
            ApkBuildRequest request;
            request.source = patch.source;
            request.source_sha256 = patch.source_sha256;

            for (const auto &item : patch.removals) {
                request.removals.push_back({item.entry, item.sha256});
            }

            request.additions = patch.additions;
            request.binary_provenance = patch.provenance;
            request.prepublication_guard = patch.revalidate;
            request.base_certificate_sha256 = sha256_bytes(
                    pinned_apk_v2_certificate(std::filesystem::absolute(argv[4]), argv[5]));
            request.identity = load_pinned_apk_identity(std::filesystem::absolute(argv[6]), argv[7],
                                                        std::filesystem::absolute(argv[8]), argv[9],
                                                        request.base_certificate_sha256);
            request.zipalign = std::filesystem::absolute(argv[10]);
            request.zipalign_sha256 = argv[11];
            const auto result = publish_native_apk_draft(request,
                                                         std::filesystem::absolute(argv[12]), 0);
            std::cout << Json({{"apk", result.apk.string()},
                               {"sha256", result.sha256},
                               {"scope", "Music ARM32 removals only; qg-runtime still required"}})
                                 .dump()
                      << "\n";

            return 0;
        }

        if (argc == 13 && std::string(argv[1]) == "--launcher-ethernet") {
            ApkBuildRequest request;
            request.source = std::filesystem::absolute(argv[2]);
            request.source_sha256 = argv[3];
            request.base_certificate_sha256 = sha256_bytes(
                    pinned_apk_v2_certificate(std::filesystem::absolute(argv[4]), argv[5]));
            request.identity = load_pinned_apk_identity(std::filesystem::absolute(argv[6]), argv[7],
                                                        std::filesystem::absolute(argv[8]), argv[9],
                                                        request.base_certificate_sha256);
            request.zipalign = std::filesystem::absolute(argv[10]);
            request.zipalign_sha256 = argv[11];
            request.literal_patches = {{"classes3.dex", launcher_ethernet_guard_recipe()}};
            const auto result = publish_native_apk_draft(request,
                                                         std::filesystem::absolute(argv[12]), 0);
            std::cout
                    << Json({{"apk", result.apk.string()},
                             {"sha256", result.sha256},
                             {"scope",
                              "Launcher ethernet literal only; remaining Launcher patches not applied"}})
                               .dump()
                    << "\n";

            return 0;
        }

        if (argc == 13 &&
            (std::string(argv[1]) == "--navi-msaa" || std::string(argv[1]) == "--launcher-media")) {
            ApkBuildRequest request;
            request.source = std::filesystem::absolute(argv[2]);
            request.source_sha256 = argv[3];
            request.base_certificate_sha256 = sha256_bytes(
                    pinned_apk_v2_certificate(std::filesystem::absolute(argv[4]), argv[5]));
            request.identity = load_pinned_apk_identity(std::filesystem::absolute(argv[6]), argv[7],
                                                        std::filesystem::absolute(argv[8]), argv[9],
                                                        request.base_certificate_sha256);
            request.zipalign = std::filesystem::absolute(argv[10]);
            request.zipalign_sha256 = argv[11];
            const bool navi = std::string(argv[1]) == "--navi-msaa";

            if (navi) {
                request.registered_patches = {
                        {"classes.dex",
                         "f24e35ff5987ca00155badfe6ee83d1ea5b3aad2098549997aabc76763428b0c",
                         "a43bed8ccbab8e9e980004ca38d30133381845a7f81fe97c0c7fcdce38be5efe",
                         ApkRegisteredDexPatchKind::navi_msaa}};
            } else {
                request.registered_patches = {{"classes2.dex", launcher_media_source_sha256,
                                               launcher_media_output_sha256,
                                               ApkRegisteredDexPatchKind::launcher_media_redirect}};
            }

            const auto result = publish_native_apk_draft(request,
                                                         std::filesystem::absolute(argv[12]), 0);
            std::cout
                    << Json({{"apk", result.apk.string()},
                             {"sha256", result.sha256},
                             {"scope",
                              navi ? "PateoNavi registered MSAA recipe only; remaining patches not applied"
                                   : "Launcher media redirect only; remaining patches not applied"}})
                               .dump()
                    << "\n";

            return 0;
        }

        if (argc == 15 && std::string(argv[1]) == "--inject") {
            ApkBuildRequest request;
            request.source = std::filesystem::absolute(argv[2]);
            request.source_sha256 = argv[3];
            const auto library = std::filesystem::absolute(argv[4]);
            const std::string library_pin = argv[5];

            if (std::filesystem::is_symlink(library) ||
                !std::filesystem::is_regular_file(library) ||
                std::filesystem::file_size(library) > 32 * 1024 * 1024 ||
                sha256_file(library) != library_pin) {
                throw std::runtime_error("pinned ordinary library required");
            }

            std::ifstream stream(library, std::ios::binary);
            std::string payload((std::istreambuf_iterator<char>(stream)), {});

            if (payload.size() < 20 ||
                payload.substr(0, 4) != std::string("\x7f"
                                                    "ELF",
                                                    4) ||
                static_cast<unsigned char>(payload[4]) != 2 ||
                static_cast<unsigned char>(payload[5]) != 1 ||
                static_cast<unsigned char>(payload[18]) != 183 || payload[19] != 0) {
                throw std::runtime_error("ARM64 ELF library required");
            }

            request.base_certificate_sha256 = sha256_bytes(
                    pinned_apk_v2_certificate(std::filesystem::absolute(argv[6]), argv[7]));
            request.identity = load_pinned_apk_identity(std::filesystem::absolute(argv[8]), argv[9],
                                                        std::filesystem::absolute(argv[10]),
                                                        argv[11], request.base_certificate_sha256);
            request.zipalign = std::filesystem::absolute(argv[12]);
            request.zipalign_sha256 = argv[13];
            request.additions = {
                    {"lib/arm64-v8a/" + library.filename().string(), payload, library_pin}};
            request.binary_provenance = {{"library_sha256", library_pin},
                                         {"trusted_base_sha256", argv[7]}};
            request.prepublication_guard = [&] {
                if (sha256_file(library) != library_pin) {
                    throw std::runtime_error("library changed");
                }
            };
            const auto result = publish_native_apk_draft(request,
                                                         std::filesystem::absolute(argv[14]), 0);
            std::cout << Json({{"apk", result.apk.string()}, {"sha256", result.sha256}}).dump()
                      << "\n";

            return 0;
        }

        if (argc != 1) {
            throw std::runtime_error(
                    "usage: --inject source pin library pin base pin key pin pem pin zipalign pin destination");
        }

        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-apk-build-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        ApkBuildRequest request;
        request.source = root / "absent.apk";
        request.source_sha256 = std::string(64, '0');
        auto refuse = [&](const std::filesystem::path &destination) {
            bool rejected = false;
            try {
                publish_native_apk_draft(request, destination, 0);
            } catch (const std::exception &) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("APK draft guard accepted input");
            }
        };
        refuse("relative");
        refuse(root / "empty");
        request.additions = {{"classes.dex", "bad", sha256_bytes("bad")}};
        refuse(root / "forbidden-binary");
        request.binary_provenance = {{"producer", "test-fixture"}};
        request.additions = {{"lib/arm64-v8a/libtest.so", "not ELF", sha256_bytes("not ELF")}};
        bool rejected_binary = false;
        try {
            publish_native_apk_draft(request, root / "bad-elf", 0);
        } catch (const std::exception &e) {
            rejected_binary = std::string(e.what()).starts_with("APK_DRAFT_BINARY:");
        }

        if (!rejected_binary) {
            throw std::runtime_error(
                    "binary preflight did not reject malformed ELF before identity/tool use");
        }

        std::string elf(64, '\0');
        elf.replace(0, 4,
                    std::string("\x7f"
                                "ELF",
                                4));
        elf[4] = 2;
        elf[5] = 1;
        elf[16] = 3;
        elf[18] = static_cast<char>(183);
        request.additions = {{"lib/arm64-v8a/libtest.so", elf, std::string(64, '0')}};
        rejected_binary = false;
        try {
            publish_native_apk_draft(request, root / "bad-binary-pin", 0);
        } catch (const std::exception &e) {
            rejected_binary = std::string(e.what()).starts_with("APK_DRAFT_BINARY:");
        }

        if (!rejected_binary) {
            throw std::runtime_error(
                    "binary pin mismatch was not rejected before identity/tool use");
        }

        request.additions.clear();
        request.binary_provenance = Json::array();
        refuse(root / "invalid-provenance");
        request.binary_provenance = Json::object();
        request.literal_patches = {{"assets/classes.dex", launcher_ethernet_guard_recipe()}};
        bool rejected_dex = false;
        try {
            publish_native_apk_draft(request, root / "bad-dex-entry", 0);
        } catch (const std::exception &e) {
            rejected_dex = std::string(e.what()).starts_with("APK_DRAFT_DEX:");
        }

        if (!rejected_dex) {
            throw std::runtime_error("typed DEX path guard did not reject non-original slot");
        }

        request.literal_patches.clear();
        request.registered_patches = {{"classes.dex", std::string(64, '0'), std::string(64, '1'),
                                       static_cast<ApkRegisteredDexPatchKind>(99)}};
        rejected_dex = false;
        try {
            publish_native_apk_draft(request, root / "unregistered-kind", 0);
        } catch (const std::exception &e) {
            rejected_dex = std::string(e.what()).starts_with("APK_DRAFT_DEX:");
        }

        if (!rejected_dex) {
            throw std::runtime_error("unregistered DEX algorithm was not rejected");
        }

        request.registered_patches = {{"assets/classes.dex", std::string(64, '0'),
                                       std::string(64, '1'), ApkRegisteredDexPatchKind::navi_msaa}};
        rejected_dex = false;
        try {
            publish_native_apk_draft(request, root / "registered-bad-entry", 0);
        } catch (const std::exception &e) {
            rejected_dex = std::string(e.what()).starts_with("APK_DRAFT_DEX:");
        }

        if (!rejected_dex) {
            throw std::runtime_error("registered DEX slot guard failed");
        }

        request.registered_patches = {
                {"classes.dex", "bad", std::string(64, '1'), ApkRegisteredDexPatchKind::navi_msaa}};
        rejected_dex = false;
        try {
            publish_native_apk_draft(request, root / "registered-bad-pin", 0);
        } catch (const std::exception &e) {
            rejected_dex = std::string(e.what()).starts_with("APK_DRAFT_DEX:");
        }

        if (!rejected_dex) {
            throw std::runtime_error("registered DEX pin guard failed");
        }

        request.registered_patches.clear();
        request.removals = {{"classes.dex", std::string(64, '0')}};
        request.binary_provenance = {{"producer", "test-fixture"}};
        bool rejected_removal = false;
        try {
            publish_native_apk_draft(request, root / "forbidden-removal", 0);
        } catch (const std::exception &e) {
            rejected_removal = std::string(e.what()).starts_with("APK_DRAFT_REMOVAL:");
        }

        if (!rejected_removal) {
            throw std::runtime_error("unregistered APK removal accepted");
        }

        request.removals = {{"lib/armeabi-v7a/libBugly.so", std::string(64, '0')}};
        rejected_removal = false;
        try {
            publish_native_apk_draft(request, root / "wrong-removal-pin", 0);
        } catch (const std::exception &e) {
            rejected_removal = std::string(e.what()).starts_with("APK_DRAFT_REMOVAL:");
        }

        if (!rejected_removal) {
            throw std::runtime_error("unregistered removal pin accepted");
        }

        request.removals.clear();
        request.templates = {{"not DEX", sha256_bytes("not DEX"), {"Lru/own/A;"}}};
        request.base_certificate_sha256 = std::string(64, '1');
        refuse(root / "wrong-base");
        request.identity.certificate_der = "not a certificate";
        request.identity.certificate_sha256 = sha256_bytes(request.identity.certificate_der);
        request.base_certificate_sha256 = request.identity.certificate_sha256;
        request.identity.pkcs8 = "not a key";
        request.identity.key_sha256 = sha256_bytes(request.identity.pkcs8);
        refuse(root / "bad-identity");
        const auto kept = root / "kept";
        ensure_private_directory(kept);
        std::ofstream(kept / "marker") << "keep";
        refuse(kept);

        if (sha256_file(kept / "marker") != sha256_bytes("keep")) {
            throw std::runtime_error("existing draft changed");
        }

        for (const auto &entry : std::filesystem::directory_iterator(root)) {
            if (entry.path() != kept) {
                throw std::runtime_error("failed pipeline leaked an output or staging directory");
            }
        }

        std::cout << "native APK draft preflight guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
