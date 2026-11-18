#include "java_runtime.hpp"
#include "e2fs_helper.hpp"
#include <fstream>
#include <iostream>
#include <string_view>
using namespace voyemu;

namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// java-runtime/bin/java (755, "runtime\n") and java-runtime/release, written by
// JavaRuntimeBuild.zip.
constexpr std::string_view archive_hex =
        "504b030414000008000000002100000000000000000000000000110000006a6176612d72756e74696d652f62696e"
        "2f504b0304140000080800000021002b08d5f50a00000008000000150000006a6176612d72756e74696d652f6269"
        "6e2f6a6176612b2acd2bc9cc4de50200504b0304140000080800000021003d6a5dba180000001600000014000000"
        "6a6176612d72756e74696d652f72656c65617365f3720c738c0f730d0af6f4f7b3553232d533d03355e20200504b"
        "0102140314000008000000002100000000000000000000000000110000000000000000001000ed41000000006a61"
        "76612d72756e74696d652f62696e2f504b01021403140000080800000021002b08d5f50a00000008000000150000"
        "000000000000000000ed812f0000006a6176612d72756e74696d652f62696e2f6a617661504b0102140314000008"
        "0800000021003d6a5dba1800000016000000140000000000000000000000a4816c0000006a6176612d72756e7469"
        "6d652f72656c65617365504b05060000000003000300c4000000b60000000000";

std::string decode(std::string_view hex) {
    std::string bytes;

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        bytes += char(std::stoul(std::string(hex.substr(i, 2)), nullptr, 16));
    }

    return bytes;
}

std::string failure(const std::function<void()> &action) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
}

void missing(const DownloadPin &, const DownloadSink &) {
    throw std::runtime_error("DOWNLOAD_HTTP: successful complete HTTPS response required");
}
} // namespace

int main(int argc, char **) {
    try {
        check(argc == 1, "usage: no arguments");
        const auto bytes = decode(archive_hex);
        const auto sha = sha256_bytes(bytes);
        const std::string host = e2fs_helper_host();
        const Json pins = {
                {"assets",
                 {{host,
                   {{"url", "https://github.com/voyemu/voyemu/releases/download/java-runtime-0/"
                            "java-runtime-" +
                                    host + ".zip"},
                    {"sha256", sha}}}}}};
        const auto scratch = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                             ("voyemu-java-runtime-" + random_token());
        ensure_private_directory(scratch);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{scratch};
        int served = 0;
        const DownloadTransport serve = [&](const DownloadPin &pin, const DownloadSink &sink) {
            check(pin.sha256 == sha && pin.url.ends_with("java-runtime-" + host + ".zip"),
                  "the pinned asset of this host is requested");
            ++served;
            sink(bytes);
        };
#ifdef _WIN32
        const auto java_name = "java.exe";
#else
        const auto java_name = "java";
#endif
        // Without a recipe JDK of the developer tools the fixture checkout lacks, nothing is
        // linked.
        const auto unpublished = scratch / "unpublished";
        ensure_private_directory(unpublished);
        const auto absent = failure([&] {
            install_java_runtime(unpublished, pins, false, 0, missing);
        });
        check(absent.starts_with("JAVA_RUNTIME_ASSET_MISSING:"),
              "an unpublished asset without the recipe JDK names the missing asset");
        check(!std::filesystem::exists(unpublished / ".local/toolchains/java-runtime" / sha),
              "nothing is installed for a missing asset");
        std::cout << "PASS missing asset\n";

        const auto downloaded = scratch / "downloaded";
        ensure_private_directory(downloaded);
        const auto java = install_java_runtime(downloaded, pins, false, 0, serve);
        check(java == downloaded / ".local/toolchains/java-runtime" / sha / "bin" / java_name &&
                      served == 1,
              "the downloaded runtime is installed by its pin");
        check(sha256_file(java.parent_path().parent_path() / "release") ==
                      sha256_bytes("JAVA_VERSION=\"25.0.5\"\n"),
              "the runtime tree is extracted below its root folder");
        check(install_java_runtime(downloaded, pins, true, 0, missing) == java && served == 1,
              "an installed runtime is reused offline");
        std::cout << "PASS download\n";

        const auto offline = scratch / "offline";
        ensure_private_directory(offline);
        check(!failure([&] {
                   install_java_runtime(offline, pins, true, 0, missing);
               }).empty() &&
                      !std::filesystem::exists(offline / ".local/toolchains/java-runtime" / sha),
              "offline without a cached asset fails");
        std::cout << "PASS offline\n";

        auto tampered = pins;
        tampered["assets"][host]["sha256"] = std::string(64, 'a');
        const auto foreign = scratch / "foreign";
        ensure_private_directory(foreign);
        check(!failure([&] {
                   install_java_runtime(foreign, tampered, false, 0, serve);
               }).empty() &&
                      !std::filesystem::exists(foreign / ".local/toolchains/java-runtime" /
                                               std::string(64, 'a')),
              "a download that differs from the pin is rejected");
        std::cout << "PASS tampered download\n";

        const auto built = scratch / "built";
        ensure_private_directory(built);
        const auto recipe = [&](const std::filesystem::path &job) {
            ensure_private_directory(job / "asset");
            std::ofstream(job / "asset" / ("java-runtime-" + host + ".zip"), std::ios::binary)
                    << bytes;
        };
        check(install_java_runtime(built, pins, false, 0, missing, recipe) ==
                      built / ".local/toolchains/java-runtime" / sha / "bin" / java_name,
              "an unpublished asset is linked by the recipe and checked against the pin");
        const auto drifted = scratch / "drifted";
        ensure_private_directory(drifted);
        const auto drift = failure([&] {
            install_java_runtime(drifted, tampered, false, 0, missing, recipe);
        });
        check(drift.starts_with("JAVA_RUNTIME:") &&
                      !std::filesystem::exists(drifted / ".local/toolchains/java-runtime" /
                                               std::string(64, 'a')),
              "a recipe output that differs from the pin is rejected");
        std::cout << "PASS recipe\n";
#ifndef _WIN32
        // The recipe itself runs javac and java of the developer JDK within the bounds of
        // artifact preparation; this JDK only writes the pinned archive.
        const auto linked = scratch / "linked";
        ensure_private_directory(linked / "src");
        const auto jdk = java_developer_jdk(linked) / "bin";
        std::filesystem::create_directories(jdk);
        const auto archive = scratch / "runtime.zip";
        std::ofstream(archive, std::ios::binary) << bytes;
        std::ofstream(jdk / "javac") << "#!/bin/sh\nexit 0\n";
        std::ofstream(jdk / "java") << "#!/bin/sh\nmkdir -p \"$6\" && cp '" << archive.string()
                                    << "' \"$6/java-runtime-" << host << ".zip\"\n";

        for (const auto *name : {"javac", "java"}) {
            std::filesystem::permissions(jdk / name, std::filesystem::perms::owner_all);
        }

        check(install_java_runtime(linked, pins, false, 0, missing) ==
                      linked / ".local/toolchains/java-runtime" / sha / "bin" / java_name,
              "an unpublished asset is linked by the recipe on the developer JDK");
        std::cout << "PASS recipe tools\n";
#endif

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';

        return 1;
    }
}
