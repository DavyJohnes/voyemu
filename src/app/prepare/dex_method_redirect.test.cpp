#include "dex_method_redirect.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--apk") {
            const auto input = read_pinned_zip_entry(std::filesystem::absolute(argv[2]), argv[3],
                                                     "classes2.dex", launcher_media_source_sha256);
            const auto output = patch_launcher_media_redirect(input);

            if (output.size() != input.size()) {
                throw std::runtime_error("DEX size changed");
            }

            std::size_t changed = 0;

            for (std::size_t i = 0; i < input.size(); ++i) {
                if (input[i] != output[i]) {
                    ++changed;

                    if (i >= 32 && static_cast<unsigned char>(input[i]) != 40) {
                        throw std::runtime_error("unrelated DEX byte changed");
                    }
                }
            }

            bool refused = false;
            try {
                patch_launcher_media_redirect(output);
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("double application accepted");
            }

            auto mutated = input;
            mutated.back() ^= 1;
            refused = false;
            try {
                patch_launcher_media_redirect(mutated);
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("changed original accepted");
            }

            std::cout << "Launcher media redirect: source=" << sha256_bytes(input)
                      << " output=" << sha256_bytes(output) << " bytes=" << output.size()
                      << " changed_bytes=" << changed << " calls=4\n";

            return 0;
        }

        if (argc != 1) {
            throw std::runtime_error("usage: --apk PINNED_LAUNCHER_APK APK_SHA256");
        }

        for (const auto &input : {std::string{}, std::string("not DEX"), std::string(112, '\0')}) {
            bool refused = false;
            try {
                patch_launcher_media_redirect(input);
            } catch (...) {
                refused = true;
            }

            if (!refused) {
                throw std::runtime_error("invalid original DEX accepted");
            }
        }

        if (std::string(launcher_media_source_sha256) == launcher_media_output_sha256) {
            throw std::runtime_error("redirect pins identical");
        }

        std::cout << "registered media redirect input guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
