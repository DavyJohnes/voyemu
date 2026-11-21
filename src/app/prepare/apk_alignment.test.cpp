#include "apk_alignment.hpp"
#include "model.hpp"
#include "os/process.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
using namespace voyemu;

static void check(bool value) {
    if (!value) {
        throw std::runtime_error("alignment regression");
    }
}

static std::string fixture(const std::string &name, unsigned offset) {
    std::string result;
    auto n16 = [&](unsigned n) {
        result += char(n);
        result += char(n >> 8);
    };
    auto n32 = [&](unsigned n) {
        n16(n);
        n16(n >> 16);
    };

    check(offset >= 30 + name.size());
    n32(0x04034b50);
    n16(20);
    n16(0);
    n16(0);
    n16(0);
    n16(0);
    n32(0xcbf43926);
    n32(9);
    n32(9);
    n16(name.size());
    n16(offset - 30 - name.size());
    result += name;
    result.resize(offset, '\0');
    result += "123456789";
    const auto central = result.size();
    n32(0x02014b50);
    n16(20);
    n16(20);
    n16(0);
    n16(0);
    n16(0);
    n16(0);
    n32(0xcbf43926);
    n32(9);
    n32(9);
    n16(name.size());
    n16(0);
    n16(0);
    n16(0);
    n16(0);
    n32(0);
    n32(0);
    result += name;
    const auto central_size = result.size() - central;
    n32(0x06054b50);
    n16(0);
    n16(0);
    n16(1);
    n16(1);
    n32(central_size);
    n32(central);
    n16(0);

    return result;
}

int main(int argc, char **argv) {
    try {
        auto dir = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                   ("apk-align-" + random_token());
        ensure_private_directory(dir);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{dir};
        auto test = [&](std::string bytes, bool accepted) {
            auto file = dir / random_token();
            std::ofstream out(file, std::ios::binary);
            out.write(bytes.data(), bytes.size());
            out.close();
            bool passed = true;
            try {
                verify_pinned_apk_alignment(file, sha256_file(file));
            } catch (...) {
                passed = false;
            }

            check(passed == accepted);
        };
        test(fixture("classes.dex", 44), true);
        test(fixture("classes.dex", 43), false);
        auto invalid_padding = fixture("classes.dex", 44);
        invalid_padding[41] = 1;
        test(invalid_padding, false);
        test(fixture("lib/arm64-v8a/libown.so", 4096), true);
        test(fixture("lib/arm64-v8a/libown.so", 64), false);
        auto mismatched = fixture("classes.dex", 44);
        mismatched[14] ^= 1;
        test(mismatched, false);
        auto trailing = fixture("classes.dex", 44);
        trailing += 'x';
        test(trailing, false);
        const auto source = dir / "unaligned.apk", tool = dir / "tool",
                   destination = dir / "aligned.apk";
        const auto bytes = fixture("classes.dex", 43);
        std::ofstream output(source, std::ios::binary);
        output.write(bytes.data(), bytes.size());
        output.close();
        std::ofstream(tool) << "fixture";

        for (unsigned mode = 0; mode < 3; ++mode) {
            bool rejected = false;
            try {
                align_pinned_apk(source, mode == 0 ? std::string(64, '0') : sha256_file(source),
                                 tool, mode == 1 ? std::string(64, '0') : sha256_file(tool),
                                 destination,
                                 mode == 2 ? std::numeric_limits<std::uint64_t>::max() : 0);
            } catch (const std::exception &error) {
                const std::string message = error.what();
                rejected = message.find(mode == 0   ? "ZIP_PIN"
                                        : mode == 1 ? "APK_ALIGN_TOOL"
                                                    : "APK_ALIGN_SPACE") != std::string::npos;
            }

            check(rejected && !std::filesystem::exists(destination));
        }

        if (argc == 1) {
            // A loader override inherited by the staged tool must abort alignment on every host.
            voyemu::os::set_environment_variable("LD_LIBRARY_PATH", "voyemu-loader-override");
            const auto env_destination = dir / "env-aligned.apk";
            bool rejected_env = false;
            try {
                align_pinned_apk(source, sha256_file(source), tool, sha256_file(tool),
                                 env_destination, 0);
            } catch (const std::exception &error) {
                rejected_env = std::string(error.what()).find("APK_ALIGN_ENV") != std::string::npos;
            }

            check(rejected_env && !std::filesystem::exists(env_destination));
        }

        if (argc == 3) {
            const auto result = align_pinned_apk(
                    std::filesystem::relative(source, std::filesystem::current_path()),
                    sha256_file(source), std::filesystem::absolute(argv[1]), argv[2], destination,
                    0);
            verify_pinned_apk_alignment(destination, result);
            check(read_pinned_zip_entry(destination, result, "classes.dex",
                                        sha256_bytes("123456789")) == "123456789");
            bool rejected = false;
            try {
                align_pinned_apk(source, sha256_file(source), std::filesystem::absolute(argv[1]),
                                 argv[2], destination, 0);
            } catch (...) {
                rejected = true;
            }

            check(rejected && sha256_file(destination) == result);
        } else {
            check(argc == 1);
        }

        for (const auto &entry : std::filesystem::directory_iterator(dir)) {
            check(entry.path().filename().string().find(".align-") == std::string::npos);
        }

        std::cout << "APK alignment guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
