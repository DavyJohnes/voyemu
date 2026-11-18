#include "resource_apk.hpp"
#include "apk_alignment.hpp"
#include "apk_base.hpp"
#include "model.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 14 &&
            (std::string(argv[1]) == "--build" || std::string(argv[1]) == "--reject-after-build")) {
            ResourceApkRequest request;
            request.source = std::filesystem::absolute(argv[2]);
            request.aapt2 = std::filesystem::absolute(argv[3]);
            request.aapt2_sha256 = argv[4];
            request.framework = std::filesystem::absolute(argv[5]);
            request.framework_sha256 = argv[6];
            request.base_certificate_sha256 = sha256_bytes(
                    pinned_apk_v2_certificate(request.framework, request.framework_sha256));
            request.identity = load_pinned_apk_identity(std::filesystem::absolute(argv[7]), argv[8],
                                                        std::filesystem::absolute(argv[9]),
                                                        argv[10], request.base_certificate_sha256);
            request.zipalign = std::filesystem::absolute(argv[11]);
            request.zipalign_sha256 = argv[12];
            const auto destination = std::filesystem::absolute(argv[13]);
            const bool reject = std::string(argv[1]) == "--reject-after-build";

            if (reject) {
                request.prepublication_guard = [] {
                    throw std::runtime_error("RESOURCE_APK_TEST: deliberate late guard");
                };
            }

            Json result;
            try {
                result = publish_resource_apk(request, destination, 0);
                check(!reject, "late guard did not run");
            } catch (const std::exception &error) {
                if (!reject ||
                    std::string(error.what()) != "RESOURCE_APK_TEST: deliberate late guard") {
                    throw;
                }

                check(!std::filesystem::exists(destination), "late guard published output");

                for (const auto &entry :
                     std::filesystem::directory_iterator(destination.parent_path())) {
                    check(!entry.path().filename().string().starts_with(
                                  "." + destination.filename().string() + ".pending-"),
                          "late guard leaked owned staging");
                }

                std::cout << "resource APK late guard cleanup passed\n";

                return 0;
            }

            verify_pinned_apk_alignment(destination / "app.apk",
                                        result.at("sha256").get<std::string>());
            check(read_private_json(destination / "receipt.json") == result,
                  "published receipt differs");
            check(result.at("unsigned_provenance").at("signed") == false &&
                          result.at("unsigned_provenance").at("dex") == false &&
                          !std::filesystem::exists(destination / "unsigned") &&
                          !std::filesystem::exists(destination / "aligned.apk"),
                  "unsigned provenance or intermediate cleanup differs");
            std::cout << result.dump() << "\n";

            return 0;
        }

        check(argc == 1,
              "usage: --build source aapt2 pin framework pin key pin pem pin zipalign pin destination");
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-resource-apk-test-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        ResourceApkRequest request;
        auto refuse = [&](const std::filesystem::path &destination, const char *expected) {
            bool rejected = false;
            try {
                publish_resource_apk(request, destination, 0);
            } catch (const std::exception &error) {
                rejected = std::string(error.what()).find(expected) != std::string::npos;
            }

            check(rejected, "resource APK preflight guard differs");
            check(!std::filesystem::exists(root / "output"), "guard created output");
        };
        refuse("relative", "RESOURCE_APK: fresh absolute");
        refuse(root / "output", "RESOURCE_APK: selected base certificate mismatch");
        request.base_certificate_sha256 = std::string(64, '0');
        request.identity.certificate_sha256 = std::string(64, '1');
        refuse(root / "output", "RESOURCE_APK: selected base certificate mismatch");
        request.identity.certificate_der = "invalid DER";
        request.identity.certificate_sha256 = sha256_bytes(request.identity.certificate_der);
        request.base_certificate_sha256 = request.identity.certificate_sha256;
        request.identity.pkcs8 = "invalid PKCS8";
        request.identity.key_sha256 = sha256_bytes(request.identity.pkcs8);
        refuse(root / "output", "APK_");
        const auto kept = root / "kept";
        ensure_private_directory(kept);
        std::ofstream(kept / "marker", std::ios::binary) << "keep";
        refuse(kept, "RESOURCE_APK: fresh absolute");
        check(sha256_file(kept / "marker") == sha256_bytes("keep"), "existing output changed");

        for (const auto &entry : std::filesystem::directory_iterator(root)) {
            check(entry.path() == kept, "failed preflight leaked staging");
        }

        std::cout << "resource APK publication preflight guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << "\n";

        return 1;
    }
}
