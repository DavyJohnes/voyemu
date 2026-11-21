#include "java_runtime.hpp"
#include "e2fs_helper.hpp"
#include "os/https.hpp"
#include "os/platform.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <miniz.h>
#include <string_view>
using namespace voyemu;

namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string runtime_archive(std::string_view java_name) {
    mz_zip_archive archive{};

    check(mz_zip_writer_init_heap(&archive, 0, 0), "runtime fixture ZIP initialization");
    struct Writer {
        mz_zip_archive &archive;

        ~Writer() {
            mz_zip_writer_end(&archive);
        }
    } writer{archive};

    const std::string java_path = "java-runtime/bin/" + std::string(java_name);
    const std::string release = "JAVA_VERSION=\"25.0.5\"\n";
    const std::string runtime = "runtime\n";
    check(mz_zip_writer_add_mem(&archive, "java-runtime/bin/", nullptr, 0,
                                MZ_DEFAULT_COMPRESSION) &&
                  mz_zip_writer_add_mem(&archive, java_path.c_str(), runtime.data(), runtime.size(),
                                        MZ_DEFAULT_COMPRESSION) &&
                  mz_zip_writer_add_mem(&archive, "java-runtime/release", release.data(),
                                        release.size(), MZ_DEFAULT_COMPRESSION),
          "runtime fixture ZIP entries");

    void *data = nullptr;
    std::size_t size = 0;
    check(mz_zip_writer_finalize_heap_archive(&archive, &data, &size),
          "runtime fixture ZIP finalization");
    const std::unique_ptr<void, decltype(&mz_free)> bytes(data, mz_free);

    return std::string(static_cast<const char *>(bytes.get()), size);
}

std::string failure(const std::function<void()> &action) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
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
        const auto java_name = "java" + std::string(voyemu::os::executable_suffix);
        const auto bytes = runtime_archive(java_name);
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
        check(failure([&] {
                  install_java_runtime(unpublished, pins, false, 0, unreachable);
              }).starts_with("DOWNLOAD_HTTP: no connection"),
              "a network failure is an error, not a missing asset");
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

        // The fake developer JDK is a pair of POSIX shell scripts.
        if (!voyemu::os::host_windows) {
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
        }

        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';

        return 1;
    }
}
