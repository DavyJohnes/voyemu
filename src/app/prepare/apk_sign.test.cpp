#include "apk_sign.hpp"
#include "model.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;

int main() {
    try {
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("apk-sign-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const auto source = root / "source", output = root / "output";
        std::ofstream(source) << "not APK";

        for (unsigned mode = 0; mode < 3; ++mode) {
            if (mode == 2) {
                std::ofstream(output) << "keep";
            }

            bool rejected = false;
            try {
                sign_pinned_apk_v2(source, sha256_file(source), "key", sha256_bytes("key"), "cert",
                                   sha256_bytes("cert"),
                                   mode == 0 ? std::filesystem::path("relative") : output, 0);
            } catch (...) {
                rejected = true;
            }

            if (!rejected) {
                throw std::runtime_error("invalid APK publication accepted");
            }
        }

        std::ifstream kept(output);
        std::string bytes;
        kept >> bytes;

        if (bytes != "keep") {
            throw std::runtime_error("existing output changed");
        }

        for (const auto &p : std::filesystem::directory_iterator(root)) {
            if (p.path().filename().string().find(".pending-") != std::string::npos) {
                throw std::runtime_error("staging leaked");
            }
        }

        std::cout << "APK signing publication guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
