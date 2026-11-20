#include "android_java.hpp"
#include "os/https.hpp"
#include "process.hpp"
#include <iostream>
using namespace voyemu;

namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// A release asset that is not published yet: HTTP 404.
void missing(const DownloadPin &, const DownloadSink &) {
    throw os::HttpsFailure(os::HttpsFailure::Kind::status, 404, "fixture");
}

// A host without network.
void unreachable(const DownloadPin &, const DownloadSink &) {
    throw os::HttpsFailure(os::HttpsFailure::Kind::unreachable, 0, "fixture");
}
} // namespace

int main(int argc, char **) {
    try {
        check(argc == 1, "usage: no arguments");
        const std::string bytes = "PK guest java";
        const auto sha = sha256_bytes(bytes);
        const Json pin = {{"url",
                           "https://github.com/voyemu/voyemu/releases/download/android-java-0/"
                           "android-java.zip"},
                          {"sha256", sha}};
        const auto scratch = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                             ("voyemu-android-java-" + random_token());
        ensure_private_directory(scratch);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } cleanup{scratch};
        int served = 0;
        const DownloadTransport serve = [&](const DownloadPin &requested,
                                            const DownloadSink &sink) {
            check(requested.sha256 == sha, "the pinned asset is requested");
            ++served;
            sink(bytes);
        };

        const auto unpublished = scratch / "unpublished";
        ensure_private_directory(unpublished);
        check(install_android_java(unpublished, pin, false, 0, missing).empty(),
              "an unpublished asset leaves the guest Java to the helper JDK");
        check(install_android_java(unpublished, pin, true, 0, missing).empty(),
              "an asset that is not cached offline leaves the guest Java to the helper JDK");
        bool network = false;
        try {
            install_android_java(unpublished, pin, false, 0, unreachable);
        } catch (const std::exception &error) {
            network = std::string_view(error.what()).starts_with("DOWNLOAD_HTTP: no connection");
        }

        check(network, "a network failure is an error, not a build from source");
        std::cout << "PASS unpublished\n";

        const auto downloaded = scratch / "downloaded";
        ensure_private_directory(downloaded);
        const auto archive = install_android_java(downloaded, pin, false, 0, serve);
        check(archive == downloaded / ".local/toolchains/android-java" / sha / "android-java.zip" &&
                      sha256_file(archive) == sha && served == 1,
              "the asset is installed by its pin");
        check(install_android_java(downloaded, pin, true, 0, missing) == archive && served == 1,
              "an installed asset is reused offline");
        std::cout << "PASS download\n";

        auto tampered = pin;
        tampered["sha256"] = std::string(64, 'a');
        const auto foreign = scratch / "foreign";
        ensure_private_directory(foreign);
        bool rejected = false;

        try {
            install_android_java(foreign, tampered, false, 0, serve);
        } catch (const std::exception &) {
            rejected = true;
        }

        check(rejected && !std::filesystem::exists(foreign / ".local/toolchains/android-java" /
                                                   std::string(64, 'a') / "android-java.zip"),
              "a download that differs from the pin is rejected");
        std::cout << "PASS tampered download\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';

        return 1;
    }
}
