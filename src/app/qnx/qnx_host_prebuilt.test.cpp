#include "qnx_host_prebuilt.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "qnx_host.hpp"
#include "qnx_host_windows_cache.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <regex>
using namespace voyemu;
namespace {
void check(bool value, const std::string &label) {
    if (!value) {
        throw std::runtime_error("check failed: " + label);
    }
}

std::string failure(const std::function<void()> &action) {
    try {
        action();
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
}

std::string text(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);

    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void retained_asset(const std::filesystem::path &root, const std::filesystem::path &asset,
                    const Json &pin) {
    const auto downloads = root / ".local/toolchains/downloads/qnx-host";

    ensure_private_directory(downloads);
    std::filesystem::copy_file(asset, downloads / (pin.at("sha256").get<std::string>() + ".blob"));
}

Json asset_pin(const std::filesystem::path &asset) {
    return {{"url", "https://example.invalid/qnx-host.zip"}, {"sha256", sha256_file(asset)}};
}

void source_info_tests(const std::filesystem::path &scratch) {
    const auto payload = scratch / "payload";

    ensure_private_directory(payload);
    write_private_json(payload / "engine", {{"engine", true}});
    const auto identity = cache_file_identity(payload / "engine");

    for (const std::string host : {"macos-arm64", "linux-x64", "windows-x64"}) {
        const auto asset = scratch / (host + ".zip");
        write_qnx_host_asset(payload, asset, false, host);
        const auto pin = asset_pin(asset);
        const auto digest = pin.at("sha256").get<std::string>();
        const auto info = read_pinned_zip_entry(asset, digest, "SOURCE-INFO.txt", 4096);
        check(info.find("Corresponding sources: qnx-host-" + host + "-sources.tar\n") !=
                              std::string::npos &&
                      info.find("Checksum: qnx-host-" + host + "-sources.tar.sha256\n") !=
                              std::string::npos &&
                      info.find("same release.") != std::string::npos &&
                      info.find("Build instructions,") != std::string::npos,
              host + " ZIP names the source companion and its build instructions");
        retained_asset(scratch, asset, pin);
        const auto destination = scratch / host;
        extract_prebuilt_qnx_host(scratch, pin, destination, true, 0, host);
        check(text(destination / "engine") == text(payload / "engine") &&
                      !std::filesystem::exists(destination / "SOURCE-INFO.txt") &&
                      !std::filesystem::exists(payload / "SOURCE-INFO.txt") &&
                      cache_file_identity(payload / "engine") == identity,
              host + " source information remains ZIP metadata and leaves payload unchanged");
    }

    const auto original = scratch / "macos-arm64.zip";
    const auto original_sha = sha256_file(original);
    auto bad = qnx_host_source_info("macos-arm64");
    bad[0] = '!';

    for (const auto &[name, info] :
         std::map<std::string, std::string>{{"missing", {}},
                                            {"bad", bad},
                                            {"wrong-host", qnx_host_source_info("linux-x64")}}) {
        const auto asset = scratch / (name + ".zip");
        std::vector<ZipReplacement> replacements;
        std::vector<std::string> removals;

        if (info.empty()) {
            removals.push_back("SOURCE-INFO.txt");
        } else {
            replacements.push_back({"SOURCE-INFO.txt",
                                    sha256_bytes(qnx_host_source_info("macos-arm64")), info,
                                    sha256_bytes(info)});
        }

        rewrite_pinned_zip(original, original_sha, replacements, removals, {}, asset, 0);
        const auto pin = asset_pin(asset);
        retained_asset(scratch, asset, pin);
        const auto destination = scratch / name;
        check(!failure([&] {
                   extract_prebuilt_qnx_host(scratch, pin, destination, true, 0, "macos-arm64");
               }).empty() &&
                      !std::filesystem::exists(destination),
              name + " source information rejected before destination creation");
    }

    check(!failure([&] {
               write_qnx_host_asset(payload, scratch / "unknown.zip", false, "unknown");
           }).empty() &&
                  !std::filesystem::exists(scratch / "unknown.zip"),
          "unknown companion host rejected");
    write_private_json(payload / "SOURCE-INFO.txt", {{"unexpected", true}});
    check(!failure([&] {
               write_qnx_host_asset(payload, scratch / "reserved.zip", false, "macos-arm64");
           }).empty() &&
                  !std::filesystem::exists(scratch / "reserved.zip"),
          "input cannot replace generated source information");
}

void installed_pin_tests(const std::filesystem::path &scratch) {
    // Both production POSIX paths use this installer. Each fixture is an actual pinned ZIP with
    // a valid recipe receipt; the second archive changes the engine without changing the recipe.
    for (const auto &[host, section] :
         std::map<std::string, std::string>{{"macos", "macos_arm64"}, {"linux", "linux_x64"}}) {
        const auto asset_host = host == "macos" ? "macos-arm64" : "linux-x64";
        const auto root = scratch / host;
        ensure_private_directory(root);
        const auto &component = qnx_host_catalog().at(section);
        const auto component_pin = sha256_bytes(component.dump());
        const auto payload = root / "payload";
        ensure_private_directory(payload);
        const auto engine = payload / "qemu-system-aarch64";
        const auto write_payload = [&](const std::string &bytes) {
            {
                std::ofstream out(engine, std::ios::binary);
                out << bytes;
            }
            write_private_json(payload / "qnx-host.json",
                               {{"schema", 1},
                                {"recipe", component},
                                {"component_sha256", component_pin},
                                {"executable_sha256", sha256_file(engine)}});
        };
        write_payload("first engine\n");
        write_qnx_host_asset(payload, root / "first.zip", true, asset_host);
        const auto first_pin = asset_pin(root / "first.zip");
        retained_asset(root, root / "first.zip", first_pin);
        const auto first = install_prebuilt_qnx_host(root, component, first_pin, host, true, 0);
        check(first.at("cached") == false &&
                      text(first.at("executable").get<std::string>()) == "first engine\n",
              host + " first actual asset installed");
        check(install_prebuilt_qnx_host(root, component, first_pin, host, true, 0).at("cached") ==
                      true,
              host + " current asset cache reused");
        write_payload("second engine\n");
        write_qnx_host_asset(payload, root / "second.zip", true, asset_host);
        const auto second_pin = asset_pin(root / "second.zip");
        check(first_pin != second_pin && failure([&] {
                                             install_prebuilt_qnx_host(root, component, second_pin,
                                                                       host, true, 0);
                                         }).starts_with("DOWNLOAD_OFFLINE:"),
              host + " old valid install cannot satisfy a new missing asset");
        retained_asset(root, root / "second.zip", second_pin);
        const auto second = install_prebuilt_qnx_host(root, component, second_pin, host, true, 0);
        const std::filesystem::path installed = second.at("directory").get<std::string>();
        const auto receipt = read_private_json(installed / "qnx-host.json");
        check(second.at("cached") == false && first.at("directory") != second.at("directory") &&
                      text(second.at("executable").get<std::string>()) == "second engine\n" &&
                      text(first.at("executable").get<std::string>()) == "first engine\n" &&
                      receipt.at("archive_sha256") == second_pin.at("sha256"),
              host + " changed pin installs actual new bytes without removing old install");
        check(install_prebuilt_qnx_host(root, component, second_pin, host, true, 0).at("cached") ==
                      true,
              host + " new asset cache reused");
        auto collision = receipt;
        auto digest = second_pin.at("sha256").get<std::string>();
        digest.back() = digest.back() == '0' ? '1' : '0';
        collision["archive_sha256"] = digest;
        write_private_json(installed / "qnx-host.json", collision);
        check(!failure([&] {
                   install_prebuilt_qnx_host(root, component, second_pin, host, true, 0);
               }).empty(),
              host + " complete archive SHA checked beyond directory prefix");
        auto malformed = receipt;
        malformed.erase("archive_sha256");
        write_private_json(installed / "qnx-host.json", malformed);
        check(!failure([&] {
                   install_prebuilt_qnx_host(root, component, second_pin, host, true, 0);
               }).empty(),
              host + " incomplete installed archive receipt rejected");
        write_private_json(installed / "qnx-host.json", receipt);
        {
            std::ofstream out(installed / "qemu-system-aarch64", std::ios::app);
            out << "tampered";
        }
        check(!failure([&] {
                   install_prebuilt_qnx_host(root, component, second_pin, host, true, 0);
               }).empty(),
              host + " changed installed executable rejected");
        {
            std::ofstream out(installed / "qemu-system-aarch64");
            out << "second engine\n";
        }
        std::filesystem::create_hard_link(installed / "qemu-system-aarch64", root / "alias");
        check(!failure([&] {
                   install_prebuilt_qnx_host(root, component, second_pin, host, true, 0);
               }).empty(),
              host + " aliased installed executable rejected");
        std::filesystem::remove(root / "alias");
        const auto moved = root / "moved";
        std::filesystem::rename(installed, moved);
        std::filesystem::create_directory_symlink(moved, installed);
        check(!failure([&] {
                   install_prebuilt_qnx_host(root, component, second_pin, host, true, 0);
               }).empty(),
              host + " linked installed directory rejected");
        std::filesystem::remove(installed);
        std::filesystem::rename(moved, installed);
        // A source-origin installation remains independent of any published-asset blob.
        const auto source = root / ".local/toolchains" /
                            ("qnx-host-" + host + "-11.1.2-" + component_pin.substr(0, 12));
        std::filesystem::copy(payload, source, std::filesystem::copy_options::recursive);
        std::filesystem::remove_all(root / ".local/toolchains/downloads");

        // Only the host of the recipe builds it.
        if (os::dependency_host == (host == "macos" ? "darwin/arm64" : "linux/x64")) {
            const auto built = install_qnx_host_engine(root, true, 0, true);
            check(built.at("cached") == true && built.at("directory") == source.string(),
                  "source-origin " + host + " cache needs no published archive");
        }
    }

    // Windows pins the runtime closure in its recipe. Two valid ZIP representations of the same
    // closure must still install under the selected archive pin, regardless of the retained cache.
    const auto root = scratch / "windows";
    const auto payload = root / ".local/toolchains/downloads/qnx-host/windows-file-backend-11.1.2";
    ensure_private_directory(payload);
    Json files = Json::object();

    for (const auto &name : {"qemu-system-aarch64.exe", "zlib1.dll"}) {
        write_private_json(payload / name, {{"fixture", name}});
        files[name] = {{"sha256", sha256_file(payload / name)},
                       {"bytes", std::filesystem::file_size(payload / name)}};
    }

    const Json component{
            {"version", "11.1.2"},
            {"verified_runtime",
             {{"files", files},
              {"qnx_file_backend",
               {{"schema", 1},
                {"version", "11.1.2"},
                {"engine_sha256", files.at("qemu-system-aarch64.exe").at("sha256")}}}}}};
    const auto legacy = install_cached_windows_qnx_host(root, component, 0);
    write_qnx_host_asset(payload, root / "first.zip", false, "windows-x64");
    const auto first_pin = asset_pin(root / "first.zip");
    retained_asset(root, root / "first.zip", first_pin);
    const auto first = install_cached_windows_qnx_host(root, component, 0, first_pin, true);
    auto archive = text(root / "first.zip");
    archive[10] = 1; // Valid local-header DOS time; runtime contents and recipe stay unchanged.
    {
        std::ofstream out(root / "second.zip", std::ios::binary);
        out << archive;
    }
    const auto second_pin = asset_pin(root / "second.zip");
    check(first_pin != second_pin && failure([&] {
                                         install_cached_windows_qnx_host(root, component, 0,
                                                                         second_pin, true);
                                     }).starts_with("DOWNLOAD_OFFLINE:"),
          "old valid Windows install and retained cache cannot bypass the new archive");
    retained_asset(root, root / "second.zip", second_pin);
    const auto second = install_cached_windows_qnx_host(root, component, 0, second_pin, true);
    const std::filesystem::path installed = second.at("directory").get<std::string>();
    const auto receipt = read_private_json(installed / "qnx-host.json");
    check(first.at("directory") != second.at("directory") &&
                  legacy.at("directory") != second.at("directory") &&
                  second.at("cached") == false &&
                  receipt.at("archive_sha256") == second_pin.at("sha256") &&
                  sha256_file(installed / "qemu-system-aarch64.exe") ==
                          files.at("qemu-system-aarch64.exe").at("sha256").get<std::string>() &&
                  install_cached_windows_qnx_host(root, component, 0, second_pin, true)
                                  .at("cached") == true,
          "Windows installs and reuses the actual selected archive with the pinned closure");
    auto collision = receipt;
    auto digest = second_pin.at("sha256").get<std::string>();
    digest.back() = digest.back() == '0' ? '1' : '0';
    collision["archive_sha256"] = digest;
    write_private_json(installed / "qnx-host.json", collision);
    check(!failure([&] {
               install_cached_windows_qnx_host(root, component, 0, second_pin, true);
           }).empty(),
          "Windows full archive SHA checked beyond directory prefix");
    write_private_json(installed / "qnx-host.json", receipt);
    auto malformed = receipt;
    malformed["archive_sha256"] = 1;
    write_private_json(installed / "qnx-host.json", malformed);
    check(!failure([&] {
               install_cached_windows_qnx_host(root, component, 0, second_pin, true);
           }).empty(),
          "Windows malformed installed archive receipt rejected");
    write_private_json(installed / "qnx-host.json", receipt);
    write_private_json(installed / "foreign.dll", {{"foreign", true}});
    check(!failure([&] {
               install_cached_windows_qnx_host(root, component, 0, second_pin, true);
           }).empty(),
          "foreign Windows installed payload remains rejected");
}
} // namespace

// Usage: qnx_host_prebuilt_test <project root>
int main(int argc, char **argv) {
    try {
        check(argc == 2, "project root argument");
        const auto root = std::filesystem::canonical(argv[1]);

        // A published engine is pinned by the release URL of its tag and asset; a pending one is
        // null.
        for (const auto &[section, host] :
             std::map<std::string, std::string>{{"macos_arm64", "macos-arm64"},
                                                {"linux_x64", "linux-x64"},
                                                {"windows_file_backend", "windows-x64"}}) {
            const auto pin = qnx_host_prebuilt_pin(qnx_host_catalog(), section);

            if (pin.is_null()) {
                continue;
            }

            const auto sha = pin.at("sha256").get<std::string>();
            check(std::regex_match(sha, std::regex("[0-9a-f]{64}")) &&
                          pin.at("url") ==
                                  "https://github.com/voyemu/voyemu-deps/releases/download/qnx-host-" +
                                          host + "-" + sha.substr(0, 8) + "/qnx-host-" + host +
                                          ".zip",
                  "release asset pin of " + section);
        }

        check(qnx_host_prebuilt_pin(Json{{"schema", 1}}, "macos_arm64").is_null() &&
                      qnx_host_prebuilt_pin(Json{{"prebuilt", Json::object()}}, "macos_arm64")
                              .is_null(),
              "missing pin is pending");
        const Json published = {{"url", "https://example.invalid/qnx-host.zip"},
                                {"sha256", std::string(64, 'a')}};
        check(qnx_host_asset_missing(nullptr, false).find("not published yet") !=
                              std::string::npos &&
                      qnx_host_asset_missing(published, false).empty() &&
                      qnx_host_asset_missing(nullptr, true).empty(),
              "a pending pin without a cached engine is unavailable");
        const auto scratch = root / ".local/build/tmp" /
                             ("qnx-host-prebuilt-test-" + random_token());
        ensure_private_directory(scratch);
        const std::map<std::string, std::string> files{
                {"licenses/qemu/COPYING", "GPL text\n"},
                {"qemu-system-aarch64", std::string("engine\0bytes", 12)}};
        const auto installed = scratch / "installed";

        for (const auto &[name, bytes] : files) {
            ensure_private_directory((installed / name).parent_path());
            std::ofstream out(installed / name, std::ios::binary);
            out << bytes;
        }

        write_private_json(installed / "qnx-host.json", Json{{"schema", 1}});
        write_qnx_host_asset(installed, scratch / "with-receipt.zip", true, "macos-arm64");
        write_qnx_host_asset(installed, scratch / "asset.zip", false, "macos-arm64");
        check(failure([&] {
                  write_qnx_host_asset(installed, scratch / "asset.zip", false, "macos-arm64");
              }).starts_with("QNX_HOST:"),
              "existing asset path rejected");
        check(text(scratch / "with-receipt.zip").size() > text(scratch / "asset.zip").size(),
              "receipt packed only on request");
        std::filesystem::rename(installed, scratch / "moved");
        write_qnx_host_asset(scratch / "moved", scratch / "again.zip", false, "macos-arm64");
        const auto asset = text(scratch / "asset.zip");
        check(text(scratch / "again.zip") == asset, "equal engines give equal assets");
        const auto sha = sha256_bytes(asset);
        const Json pin{{"url", "https://example.invalid/qnx-host.zip"}, {"sha256", sha}};
        check(qnx_host_prebuilt_pin(Json{{"prebuilt", {{"macos_arm64", pin}}}}, "macos_arm64") ==
                      pin,
              "published pin selected");
        check(failure([&] {
                  extract_prebuilt_qnx_host(scratch, pin, scratch / "missing", true, 0,
                                            "macos-arm64");
              }).starts_with("DOWNLOAD_OFFLINE:"),
              "missing asset is reported offline");
        const auto cache = scratch / ".local/toolchains/downloads/qnx-host";
        ensure_private_directory(cache);
        {
            std::ofstream out(cache / (sha + ".blob"), std::ios::binary);
            out << asset;
        }
        const auto engine = scratch / "engine";
        extract_prebuilt_qnx_host(scratch, pin, engine, true, 0, "macos-arm64");

        for (const auto &[name, bytes] : files) {
            check(text(engine / name) == bytes, "extracted " + name);
        }

        check(!std::filesystem::exists(engine / "qnx-host.json"), "receipt left out");

        check(failure([&] {
                  extract_prebuilt_qnx_host(scratch, pin, engine, true, 0, "macos-arm64");
              }).starts_with("QNX_HOST:"),
              "existing destination rejected");
        {
            std::ofstream out(cache / (sha + ".blob"), std::ios::binary);
            out << asset << "changed";
        }
        check(failure([&] {
                  extract_prebuilt_qnx_host(scratch, pin, scratch / "changed", true, 0,
                                            "macos-arm64");
              }).starts_with("DOWNLOAD_"),
              "changed asset rejected");
        check(!std::filesystem::exists(scratch / "changed"), "rejected asset extracts nothing");
        source_info_tests(scratch / "source-info");
        installed_pin_tests(scratch / "installed-pin");
        std::filesystem::remove_all(scratch);
        std::cout << "prebuilt QNX engine installation passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
