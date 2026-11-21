#include "deps.hpp"
#include "deps_test_host.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "os/process.hpp"
#include "process.hpp"
#include "sdk_archive.hpp"
#include "zip_archive.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
using namespace voyemu;

static void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

// Sets an environment variable for this process; an empty value removes it.
// The leading bytes of an executable in this host's format.
static std::string native_magic() {
    if (os::executable_format == "PE") {
        return "MZ";
    }

    return os::executable_format == "Mach-O" ? std::string("\xcf\xfa\xed\xfe", 4)
                                             : std::string("\177ELF", 4);
}

static void set_environment(const char *name, const std::string &value) {
    os::set_environment_variable(name, value);
}

static void runtime_binding_checks(const std::filesystem::path &root) {
    const auto sdk = root / "runtime-sdk";
    auto put = [](const std::filesystem::path &path, const std::string &bytes) {
        ensure_private_directory(path.parent_path());
        std::ofstream(path, std::ios::binary) << bytes;
    };

    const std::string binary = native_magic() + "native runtime fixture";
    std::vector<NativeDependency> recipe;
    Json components = Json::array();

    for (const auto *target : {"platform-tools", "emulator"}) {
        const auto archive = root / (std::string(target) + ".zip"),
                   receipt = root / (std::string(target) + "-audit.json"), directory = sdk / target;
        put(archive, std::string("pinned archive fixture ") + target);
        put(directory / "source.properties", "Pkg.Revision=1\n");
        put(directory / "tool", binary);
        std::vector<const char *> names{"source.properties", "tool"};

        if (std::string_view(target) == "platform-tools") {
            put(directory / "adb.exe", binary);
            put(directory / "mke2fs.exe", binary);
            put(directory / "mke2fs.conf", "fixed formatter config");
            names.insert(names.end(), {"adb.exe", "mke2fs.exe", "mke2fs.conf"});
        }

        Json files = Json::array();
        std::uint64_t total = 0;

        for (const auto *name : names) {
            const auto file = directory / name;
            const auto size = std::filesystem::file_size(file);
            files.push_back({{"path", name}, {"bytes", size}, {"sha256", sha256_file(file)}});
            total += size;
        }

        const auto pin = sha256_file(archive);
        write_private_json(receipt, {{"schema", 1},
                                     {"kind", "audited-native-sdk-component"},
                                     {"archive_sha256", pin},
                                     {"archive_bytes", std::filesystem::file_size(archive)},
                                     {"files", files},
                                     {"bytes", total},
                                     {"copied_bytes", 0}});
        components.push_back({{"target", target},
                              {"archive", archive.string()},
                              {"receipt", receipt.string()},
                              {"receipt_sha256", sha256_file(receipt)}});
        recipe.push_back(
                {target, "1", {"https://fixture.invalid/pinned.zip", pin, 1024}, {"tool"}});
    }

    Json binding = {{"schema", 1},
                    {"host", native_dependency_host()},
                    {"sdk_root", sdk.string()},
                    {"components", components}};
    const auto valid = verify_native_runtime_dependencies(sdk, binding, recipe);
    check(valid.at("ready") == true && valid.at("files") == 7 && valid.at("copied_bytes") == 0,
          "runtime SDK audit receipt rejected");
    auto rejects_binding = [&](const Json &candidate) {
        bool rejected = false;
        try {
            verify_native_runtime_dependencies(sdk, candidate, recipe);
        } catch (...) {
            rejected = true;
        }

        check(rejected, "invalid runtime SDK binding accepted");
    };
    auto bad = binding;
    bad["host"] = "foreign";
    rejects_binding(bad);
    bad = binding;
    bad["components"][0]["receipt_sha256"] = std::string(64, '0');
    rejects_binding(bad);
    const auto tool = sdk / "emulator/tool";
    put(tool, "changed runtime");
    rejects_binding(binding);
    put(tool, binary);
    const auto alias = root / "tool-alias";
    std::filesystem::create_hard_link(tool, alias);
    rejects_binding(binding);
    std::filesystem::remove(alias);
    put(sdk / "emulator/extra", "unlisted");
    rejects_binding(binding);
    std::filesystem::remove(sdk / "emulator/extra");
    const auto archive = root / "emulator.zip";
    put(archive, "changed archive");
    rejects_binding(binding);
    put(archive, "pinned archive fixture emulator");
    check(verify_native_runtime_dependencies(sdk, binding, recipe).at("ready") == true,
          "restored runtime SDK audit changed");
    const auto platform = sdk / "platform-tools";
    std::filesystem::remove(platform / "adb.exe");
    rejects_binding(binding);
    const auto launch = verify_native_runtime_dependencies(sdk, binding, recipe,
                                                           RuntimeSdkUse::launch);
    check(launch.at("files") == 2 && launch.at("components").size() == 1,
          "ready launch still requires platform-tools");
    std::filesystem::remove_all(platform);
    check(verify_native_runtime_dependencies(sdk, binding, recipe, RuntimeSdkUse::launch)
                          .at("files") == 2,
          "missing platform-tools blocked ready launch");
    put(sdk / "emulator/extra", "unlisted");
    bool rejected = false;
    try {
        verify_native_runtime_dependencies(sdk, binding, recipe, RuntimeSdkUse::launch);
    } catch (...) {
        rejected = true;
    }

    check(rejected, "ready launch accepted an unaudited emulator file");
}

static void installed_metadata_checks(const std::filesystem::path &root) {
    const auto project = root / "metadata-project", sdk = project / ".local/toolchains/native-sdk";

    ensure_private_directory(project);
    auto put = [](const std::filesystem::path &file, const std::string &text) {
        ensure_private_directory(file.parent_path());
        std::ofstream(file, std::ios::binary) << text;
    };
    const std::string binary = native_magic() + "fixture";
    const auto recipe = native_dependencies(native_dependency_host());
    Json receipt = {
            {"schema", 1}, {"host", native_dependency_host()}, {"components", Json::array()}};

    for (const auto &component : recipe) {
        put(sdk / component.target / "source.properties",
            "Pkg.Revision=" + component.revision + "\n");

        for (const auto &executable : component.executables) {
            put(sdk / component.target / executable, binary);
        }

        receipt["components"].push_back({{"target", component.target},
                                         {"revision", component.revision},
                                         {"archive_sha256", component.pin.sha256}});
    }

    Json files = Json::array();

    for (const auto &entry : std::filesystem::recursive_directory_iterator(sdk)) {
        if (entry.is_regular_file()) {
            files.push_back({{"path", entry.path().lexically_relative(sdk).generic_string()},
                             {"bytes", entry.file_size()},
                             {"sha256", sha256_file(entry.path())}});
        }
    }

    std::sort(files.begin(), files.end(), [](const Json &a, const Json &b) {
        return a.at("path").get<std::string>() < b.at("path").get<std::string>();
    });
    receipt["files"] = files.size();
    receipt["tree_sha256"] = sha256_bytes(files.dump());
    write_private_json(sdk / "native-deps.json", receipt);
    check(install_native_dependencies(project, true, 0).at("ready") == true,
          "baseline installed fixture rejected");
    const std::string metadata =
            R"XML(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><ns2:repository xmlns:ns2="http://schemas.android.com/repository/android/common/02" xmlns:ns3="http://schemas.android.com/repository/android/common/01" xmlns:ns4="http://schemas.android.com/repository/android/generic/01" xmlns:ns5="http://schemas.android.com/repository/android/generic/02" xmlns:ns6="http://schemas.android.com/sdk/android/repo/addon2/01" xmlns:ns7="http://schemas.android.com/sdk/android/repo/addon2/02" xmlns:ns8="http://schemas.android.com/sdk/android/repo/addon2/03" xmlns:ns9="http://schemas.android.com/sdk/android/repo/repository2/01" xmlns:ns10="http://schemas.android.com/sdk/android/repo/repository2/02" xmlns:ns11="http://schemas.android.com/sdk/android/repo/repository2/03" xmlns:ns12="http://schemas.android.com/sdk/android/repo/sys-img2/04" xmlns:ns13="http://schemas.android.com/sdk/android/repo/sys-img2/03" xmlns:ns14="http://schemas.android.com/sdk/android/repo/sys-img2/02" xmlns:ns15="http://schemas.android.com/sdk/android/repo/sys-img2/01"><license id="license-2AAB78AE" type="text"/><localPackage path="platform-tools" obsolete="false"><type-details xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:type="ns5:genericDetailsType"/><revision><major>36</major><minor>0</minor><micro>0</micro></revision><display-name>Android SDK Platform-Tools 36</display-name><uses-license ref="license-2AAB78AE"/></localPackage></ns2:repository>)XML";
    const auto file = sdk / "platform-tools/package.xml";
    put(file, metadata);
    check(install_native_dependencies(project, true, 0).at("files") == files.size(),
          "standard SDK-manager metadata changed pinned payload identity");
    auto rejects = [&]() {
        bool failed = false;
        try {
            install_native_dependencies(project, true, 0);
        } catch (...) {
            failed = true;
        }

        check(failed, "untrusted installed SDK data accepted");
    };

    for (const auto *needle : {"platform-tools", "<major>36</major>", "repository"}) {
        auto changed = metadata;
        changed.replace(changed.find(needle), std::string(needle).size(), "untrusted");
        put(file, changed);
        rejects();
    }

    put(file, std::string(4096, 'x'));
    rejects();
    put(file, metadata);
    const auto alias = project / "metadata-alias";
    std::filesystem::create_hard_link(file, alias);
    rejects();
    std::filesystem::remove(alias);
    std::filesystem::rename(file, alias);
    std::filesystem::create_symlink(alias, file);
    rejects();
    std::filesystem::remove(file);
    std::filesystem::rename(alias, file);
    with_foreign_write(file, rejects);
    put(sdk / "platform-tools/unlisted", "extra");
    rejects();
    std::filesystem::remove(sdk / "platform-tools/unlisted");
    put(sdk / "platform-tools/package.xml.other", metadata);
    rejects();
    std::filesystem::remove(sdk / "platform-tools/package.xml.other");
    check(install_native_dependencies(project, true, 0).at("ready") == true,
          "restored generated metadata rejected");
}

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--fresh-external-ndk") {
            const auto root = std::filesystem::canonical(argv[2]);
            check(!std::filesystem::exists(root / ".local"),
                  "fresh root must start without cache, SDK or archive");
            const auto selected = native_ndk_directory(root);
            const auto &spec = native_ndk_archive();
            const auto receipt = root / ".local/toolchains/ndk-audits" /
                                 (spec.at("sha256").get<std::string>() + ".json");
            const auto audit = read_private_json(receipt, 8 << 20);
            check(sha256_bytes(audit.dump()) == spec.at("inventory_sha256").get<std::string>() &&
                          audit.at("archive_bytes") == spec.at("archive_bytes"),
                  "fresh NDK audit differs from pinned official inventory");
            check(!std::filesystem::exists(root / ".local/toolchains/downloads") &&
                          !std::filesystem::exists(root / ".local/toolchains/native-sdk"),
                  "fresh NDK lookup downloaded or installed SDK");
            std::cout << "fresh external NDK verified without ZIP, SDK or download: "
                      << selected.string() << '\n';

            return 0;
        }

        if (argc == 3 && std::string_view(argv[1]) == "--external-ndk") {
            std::cout << native_ndk_directory(std::filesystem::canonical(argv[2])).string() << '\n';

            return 0;
        }

        for (const auto *host : {"darwin/arm64", "darwin/x64", "win32/x64"}) {
            const auto components = native_dependencies(host);
            check(components.size() == 5, "five native SDK components");
            check(native_runtime_dependencies(host).size() == 2,
                  "runtime only needs emulator and ADB components");
            std::set<std::string> targets;

            for (const auto &component : components) {
                check(targets.insert(component.target).second &&
                              component.pin.url.starts_with("https://") &&
                              component.pin.sha256.size() == 64 && !component.revision.empty(),
                      "exact unique pinned dependency");
                check(!component.target.starts_with("cmdline-tools") &&
                              !component.target.starts_with("platforms"),
                      "no Java SDK manager or compile SDK");

                for (const auto &binary : component.executables) {
                    check(!binary.ends_with(".bat") && !binary.ends_with(".jar") &&
                                  binary.find("python") == std::string::npos,
                          "native executables only");
                }
            }

            check(components.at(1).pin.sha256 ==
                          (std::string_view(host) == "darwin/arm64"
                                   ? "f285a4974c3352a1c0026ae76fe53b06a02d71d0e13ebbab79a928af1f86d3a2"
                           : std::string_view(host) == "darwin/x64"
                                   ? "a01025b471a9ac0ef0fbd59febd0c1cfdeb2e889cc6fedc7ea881239bd8bb9a4"
                                   : "fbd5e54c868f66de78fa3a8b811daaa58262caddf8920271a63f77386cce21ca"),
                  "architecture-specific engine pin");
        }

        // Preparation runs e2fsck of the Emulator archive on macOS arm64 and Windows x64.
        for (const auto &[host, file] : {std::pair{"darwin/arm64", "emulator/bin64/e2fsck"},
                                         std::pair{"win32/x64", "emulator/bin64/e2fsck.exe"}}) {
            const auto pin = native_runtime_file_pin(host, file);
            check(pin.size() == 64 &&
                          pin.find_first_not_of("0123456789abcdef") == std::string::npos,
                  "pinned runtime file of the Emulator archive");
        }

        for (const auto &[host, file] :
             {std::pair{"darwin/x64", "emulator/bin64/e2fsck"},
              std::pair{"win32/x64", "emulator/bin64/e2fsck"}, std::pair{"linux", "emulator"}}) {
            bool missing = false;
            try {
                native_runtime_file_pin(host, file);
            } catch (const std::exception &e) {
                missing = std::string_view(e.what()).starts_with("DEPS_HOST:");
            }

            check(missing, "runtime file without a pin is rejected");
        }

        const auto recipes = native_dependencies(native_dependency_host());
        Json receipt = {{"schema", 1},
                        {"host", native_dependency_host()},
                        {"manifest_sha256", "unrelated toolchain metadata changed"},
                        {"components", Json::array()}};

        for (const auto &item : recipes) {
            receipt["components"].push_back({{"target", item.target},
                                             {"revision", item.revision},
                                             {"archive_sha256", item.pin.sha256}});
        }

        check(native_dependency_receipt_matches(receipt, native_dependency_host(), recipes),
              "unrelated manifest entries do not invalidate exact SDK recipes");
        auto rejects = [&](const Json &bad) {
            check(!native_dependency_receipt_matches(bad, native_dependency_host(), recipes),
                  "changed dependency identity accepted");
        };
        auto bad = receipt;
        bad["host"] = "foreign";
        rejects(bad);
        bad = receipt;
        bad["schema"] = 2;
        rejects(bad);

        for (std::size_t i = 0; i < recipes.size(); ++i) {
            for (const auto *key : {"target", "revision", "archive_sha256"}) {
                bad = receipt;
                bad["components"][i][key] = "changed";
                rejects(bad);
            }
        }

        bad = receipt;
        bad["components"].erase(0);
        rejects(bad);
        bad = receipt;
        bad["components"].push_back(bad["components"][0]);
        rejects(bad);
        bad = receipt;
        bad["components"][1] = bad["components"][0];
        rejects(bad);
        bool host = false;
        try {
            native_dependencies("freebsd/x64");
        } catch (...) {
            host = true;
        }

        check(host, "unsupported host cannot fall back");
        // This host installs its own archives and NDK binaries, never those of another host.
        const std::map<std::string_view, std::vector<std::string>> archive_names{
                {"darwin", {"darwin", "macosx"}}, {"linux", {"linux"}}, {"win32", {"win"}}};

        for (const auto &component : native_dependencies(native_dependency_host())) {
            const auto &names = archive_names.at(os::dependency_system);
            check(std::any_of(names.begin(), names.end(),
                              [&](const std::string &name) {
                                  return component.pin.url.find("/" + name) != std::string::npos ||
                                         component.pin.url.find("-" + name) != std::string::npos ||
                                         component.pin.url.find("_" + name) != std::string::npos;
                              }),
                  "archive of another host selected");
        }

        check(native_dependencies(native_dependency_host())
                              .at(3)
                              .executables.at(0)
                              .starts_with("toolchains/llvm/prebuilt/" + std::string(os::ndk_host) +
                                           "/") &&
                      native_ndk_tool("clang").starts_with("toolchains/llvm/prebuilt/" +
                                                           std::string(os::ndk_host) + "/"),
              "NDK binaries of another host selected");
        const auto root = std::filesystem::canonical(std::filesystem::path(__FILE__))
                                  .parent_path()
                                  .parent_path()
                                  .parent_path()
                                  .parent_path() /
                          ".local/build/tmp" / ("voyemu-deps-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        runtime_binding_checks(root);
        installed_metadata_checks(root);

        // Creating a symbolic link needs a privilege on Windows.
        if (!os::host_windows) {
            const auto ndk = root / "inventory-fixture";
            ensure_private_directory(ndk / "bin");
            auto put = [](const std::filesystem::path &p, const std::string &bytes) {
                std::ofstream(p, std::ios::binary) << bytes;
            };
            put(ndk / "bin/tool", "upstream");
            std::filesystem::create_symlink("tool", ndk / "bin/alias");
            put(ndk / "source.properties", "Pkg.Revision = 1\n");
            const auto expected = inspect_external_ndk_inventory(ndk, std::string(64, 'a'), 123);
            const auto pin = sha256_bytes(expected.dump());
            check(expected.at("files").size() == 3 && expected.at("archive_bytes") == 123,
                  "upstream format inventory differs");
            put(ndk / "bin/tool", "modified");
            check(sha256_bytes(
                          inspect_external_ndk_inventory(ndk, std::string(64, 'a'), 123).dump()) !=
                          pin,
                  "changed NDK bytes retained upstream pin");
            put(ndk / "bin/tool", "upstream");
            put(ndk / "extra", "bad");
            check(sha256_bytes(
                          inspect_external_ndk_inventory(ndk, std::string(64, 'a'), 123).dump()) !=
                          pin,
                  "extra NDK file retained upstream pin");
            std::filesystem::remove(ndk / "extra");
            std::filesystem::remove(ndk / "bin/alias");
            std::filesystem::create_symlink("../source.properties", ndk / "bin/alias");
            check(sha256_bytes(
                          inspect_external_ndk_inventory(ndk, std::string(64, 'a'), 123).dump()) !=
                          pin,
                  "changed alias retained upstream pin");
        }

        // Every supported host reuses an external NDK against its own archive inventory; Linux
        // installs the NDK from its pinned archive only.
        if (os::platform_name != "Linux") {
            const auto &archive = native_ndk_archive();
            check(archive.at("url").get<std::string>().ends_with(
                          os::host_windows ? "-windows.zip" : "-darwin.zip") &&
                          archive.contains("archive_bytes") && archive.contains("inventory_sha256"),
                  "host NDK archive inventory pin missing");
            const auto external = root / "external-ndk";
            ensure_private_directory(external);
            const char *previous = std::getenv("NDK_HOME");
            struct Restore {
                std::string value;

                ~Restore() {
                    set_environment("NDK_HOME", value);
                }
            } restore{previous ? previous : ""};
            set_environment("NDK_HOME", external.string());
            {
                std::ofstream out(external / "source.properties");
                out << "Pkg.Revision = 1.0.0\n";
            }
            bool wrong = false;
            try {
                native_ndk_directory(root);
            } catch (const std::exception &e) {
                wrong = std::string_view(e.what()).starts_with("DEPS_VERSION:");
            }

            check(wrong, "wrong external NDK revision accepted");
            {
                std::ofstream out(external / "source.properties");
                out << "Pkg.Revision = 29.0.14206865\n";
            }
            bool no_download = false;
            try {
                native_ndk_directory(root);
            } catch (const std::exception &e) {
                no_download = std::string_view(e.what()).starts_with("DEPS_NDK_AUDIT:");
            }

            check(no_download && !std::filesystem::exists(root / ".local/toolchains/downloads"),
                  "external NDK audit must not download");
        }

        bool offline = false;
        try {
            install_native_dependencies(root, true, 0);
        } catch (const std::exception &error) {
            offline = std::string_view(error.what()).starts_with("DOWNLOAD_OFFLINE:");
        }

        check(offline, "offline install cannot download missing payload");
        check(!std::filesystem::exists(root / ".local/toolchains/native-sdk") &&
                      os::FileLock(root / ".local/toolchains/native-sdk.lock", true).held(),
              "failed install publishes nothing and releases own lock");

        for (const auto &entry : std::filesystem::directory_iterator(root / ".local/build/tmp")) {
            check(!entry.path().filename().string().starts_with("native-sdk-"),
                  "failed install cleans owned staging");
        }

        if (argc == 2) {
            const auto installed = root / ("voyemu" + std::string(os::executable_suffix));
            ensure_private_directory(root / ".local/machines");
            std::filesystem::copy_file(std::filesystem::canonical(argv[1]), installed);
            ProcessSpec command;
            command.executable = installed;
            command.arguments = {"deps", "--offline"};
            command.directory = root;
            command.output = root / "deps.log";
            const auto reply = execute(command, 10000);
            check(reply.exit_code != 0 &&
                          reply.output.find("DOWNLOAD_OFFLINE") != std::string::npos,
                  "native CLI dispatches deps without legacy interpreter");
        }

        std::cout
                << "native dependency catalog, offline fail-closed and owned install cleanup passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
