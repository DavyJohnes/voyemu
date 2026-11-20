#include "e2fs_helper.hpp"
#include "deps.hpp"
#include "os/https.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace voyemu;
namespace {
void check(bool value, const std::string &label) {
    if (!value) {
        throw std::runtime_error("check failed: " + label);
    }
}

std::string text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);

    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &path, const std::string &bytes) {
    std::ofstream out(path, std::ios::binary);

    out << bytes;
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

// Usage: e2fs_helper_test <project root> [--recipe]
// --recipe builds both helpers of this host with the real recipe while their release asset is
// missing and checks them against the real pins; it needs the pinned Zig and CMake and the network.
int main(int argc, char **argv) {
    try {
        check(argc == 2 || (argc == 3 && std::string(argv[2]) == "--recipe"),
              "project root argument");
        const auto root = std::filesystem::canonical(argv[1]);
        const auto &helpers = embedded_toolchain_manifest().at("e2fsprogs").at("helpers");

        if (argc == 3) {
            const auto scratch = root / ".local/build/tmp" /
                                 ("e2fs-helper-recipe-" + random_token());

            for (const auto *name : {"a9-offline-labels", "api30-root-layout"}) {
                const auto helper = install_e2fs_helper(scratch, helpers, name, false, 0, missing,
                                                        [&](const std::filesystem::path &output) {
                                                            build_e2fs_helpers(root, output);
                                                        });
                check(sha256_file(helper) == helpers.at(e2fs_helper_host())
                                                     .at(name)
                                                     .at("sha256")
                                                     .get<std::string>(),
                      std::string("recipe reproduces the pinned ") + name);
            }

            std::filesystem::remove_all(scratch);
            std::cout << "e2fsprogs helper recipe reproduces the pins\n";

            return 0;
        }

        for (const auto *host : {"darwin-arm64", "linux-x64", "win32-x64"}) {
            for (const auto *name : {"a9-offline-labels", "api30-root-layout"}) {
                const auto &pin = helpers.at(host).at(name);
                check(pin.at("url").get<std::string>().ends_with(std::string("/") + name + "-" +
                                                                 host) &&
                              pin.at("sha256").get<std::string>().size() == 64,
                      std::string("release asset pin ") + name + " " + host);
            }
        }

        const auto scratch = root / ".local/build/tmp" / ("e2fs-helper-test-" + random_token());
        ensure_private_directory(scratch);
        const std::string bytes = "#!/bin/sh\necho pinned helper\n";
        write(scratch / "fixture", bytes);
        const auto sha = sha256_file(scratch / "fixture");
        const Json fixture = {{e2fs_helper_host(),
                               {{"a9-offline-labels",
                                 {{"url", "https://example.invalid/a9"}, {"sha256", sha}}}}}};
        check(failure([&] {
                  install_e2fs_helper(scratch, fixture, "a9-offline-labels", true, 0);
              }).starts_with("DOWNLOAD_OFFLINE:"),
              "missing download is reported offline");
        const auto cache = scratch / ".local/toolchains/downloads/e2fsprogs";
        ensure_private_directory(cache);
        write(cache / (sha + ".blob"), bytes);
#ifdef _WIN32
        const std::string installed = "a9-offline-labels.exe";
#else
        const std::string installed = "a9-offline-labels";
#endif
        const auto helper = install_e2fs_helper(scratch, fixture, "a9-offline-labels", true, 0);
        check(helper == scratch / ".local/toolchains/e2fs-helpers" / sha / installed &&
                      text(helper) == bytes,
              "helper installed from the verified download");
#ifndef _WIN32
        check((std::filesystem::status(helper).permissions() &
               std::filesystem::perms::owner_exec) == std::filesystem::perms::owner_exec,
              "installed helper is executable");
#endif
        check(install_e2fs_helper(scratch, fixture, "a9-offline-labels", true, 0) == helper,
              "installed helper reused");
        write(helper, bytes + "changed");
        check(failure([&] {
                  install_e2fs_helper(scratch, fixture, "a9-offline-labels", true, 0);
              }).starts_with("E2FS_HELPER:"),
              "changed installed helper rejected");
        std::filesystem::remove(helper);
        write(cache / (sha + ".blob"), bytes + "changed");
        check(failure([&] {
                  install_e2fs_helper(scratch, fixture, "a9-offline-labels", true, 0);
              }).starts_with("DOWNLOAD_"),
              "changed download rejected");
        check(!std::filesystem::exists(helper), "rejected download installs nothing");
        std::filesystem::remove(cache / (sha + ".blob"));
        const auto build = [&](const std::string &output) {
            return [&, output](const std::filesystem::path &job) {
                write(job / (std::string("a9-offline-labels-") + e2fs_helper_host()), output);
            };
        };
        check(failure([&] {
                  install_e2fs_helper(scratch, fixture, "a9-offline-labels", false, 0, missing,
                                      build(bytes + "changed"));
              }).starts_with("E2FS_HELPER:"),
              "recipe output that differs from the pin rejected");
        check(!std::filesystem::exists(helper), "rejected recipe output installs nothing");
        check(failure([&] {
                  install_e2fs_helper(
                          scratch, fixture, "a9-offline-labels", false, 0,
                          [&](const DownloadPin &, const DownloadSink &sink) {
                              sink(bytes + "changed");
                          },
                          build(bytes));
              }).starts_with("DOWNLOAD_"),
              "changed download does not fall back to the recipe");
        check(failure([&] {
                  install_e2fs_helper(scratch, fixture, "a9-offline-labels", false, 0, unreachable,
                                      build(bytes));
              }).starts_with("DOWNLOAD_HTTP: no connection") &&
                      !std::filesystem::exists(helper),
              "a network failure does not fall back to the recipe");
        check(install_e2fs_helper(scratch, fixture, "a9-offline-labels", false, 0, missing,
                                  build(bytes)) == helper &&
                      text(helper) == bytes,
              "missing release asset falls back to the recipe and its pin");
        std::filesystem::remove_all(scratch);
        std::cout << "e2fsprogs helper installation passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
