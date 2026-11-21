#include "deps.hpp"
#include "check.hpp"
#include "deps_metadata.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "os/platform.hpp"
#include "owner_lock.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "quickboot.hpp"
#include "sdk_archive.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
namespace voyemu {
namespace {
std::string text_file(const std::filesystem::path &file, std::size_t maximum) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) ||
        std::filesystem::hard_link_count(file) != 1 || std::filesystem::file_size(file) > maximum) {
        throw VoyemuError("DEPS_FILE", "ordinary bounded metadata required");
    }

    std::ifstream stream(file, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(stream)), {});

    if (!stream.eof() && stream.fail()) {
        throw VoyemuError("DEPS_READ", "metadata read failed");
    }

    if (text.size() > maximum) {
        throw VoyemuError("DEPS_BOUND", "metadata grew");
    }

    return text;
}

std::map<std::string, std::string> properties(const std::string &text) {
    auto trim = [](std::string value) {
        const auto first = value.find_first_not_of(" \t\r");

        if (first == std::string::npos) {
            return std::string{};
        }

        return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
    };
    std::map<std::string, std::string> result;
    std::istringstream stream(text);
    std::string line;

    while (std::getline(stream, line)) {
        line = trim(line);

        if (line.empty() || line.starts_with('#')) {
            continue;
        }

        const auto at = line.find('=');

        if (at == std::string::npos) {
            continue;
        }

        const auto key = trim(line.substr(0, at)), value = trim(line.substr(at + 1));

        if (!result.emplace(key, value).second) {
            throw VoyemuError("DEPS_META", "duplicate metadata property");
        }
    }

    return result;
}

void verify_component(const std::filesystem::path &root, const NativeDependency &component,
                      bool prepare_permissions = true) {
    const auto values = properties(text_file(root / "source.properties", 65536));

    if (!values.contains("Pkg.Revision") || values.at("Pkg.Revision") != component.revision) {
        throw VoyemuError("DEPS_VERSION",
                          "component revision differs from pinned metadata: " + component.target);
    }

    for (const auto &relative : component.executables) {
        const auto file = root / relative;

        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) ||
            std::filesystem::hard_link_count(file) != 1 || std::filesystem::file_size(file) < 4) {
            throw VoyemuError("DEPS_EXECUTABLE", "native binary missing: " + relative);
        }

        std::ifstream stream(file, std::ios::binary);
        unsigned char magic[4]{};
        stream.read(reinterpret_cast<char *>(magic), 4);
        const bool pe = magic[0] == 'M' && magic[1] == 'Z';
        const bool macho =
                (magic[0] == 0xcf && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe) ||
                (magic[0] == 0xce && magic[1] == 0xfa && magic[2] == 0xed && magic[3] == 0xfe) ||
                (magic[0] == 0xca && magic[1] == 0xfe && magic[2] == 0xba && magic[3] == 0xbe) ||
                (magic[0] == 0xca && magic[1] == 0xfe && magic[2] == 0xba && magic[3] == 0xbf);
        const bool elf = magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F';
        const auto format = pe ? "PE" : macho ? "Mach-O" : elf ? "ELF" : "";

        if (format != os::executable_format) {
            throw VoyemuError("DEPS_EXECUTABLE",
                              std::string(os::executable_format) + " executable required");
        }

        if (prepare_permissions) {
            std::filesystem::permissions(file,
                                         std::filesystem::perms::owner_exec |
                                                 std::filesystem::perms::group_exec |
                                                 std::filesystem::perms::others_exec,
                                         std::filesystem::perm_options::add);
        }
    }
}

// Android SDK Manager may materialize this metadata while discovering shared tools.
// Accept only the independently observed platform-tools 36.0.0 descriptor; payload pins stay exact.

void verify_generated_platform_tools_metadata(const std::filesystem::path &root,
                                              const std::filesystem::path &file) {
    const auto epoch = cache_file_identity(file);
    const auto text = text_file(file, 1432);

    verify_owner_controlled_metadata(file);
    const auto values = properties(text_file(root / "platform-tools/source.properties", 65536));

    if (!values.contains("Pkg.Revision") || values.at("Pkg.Revision") != "36.0.0" ||
        text.size() != 1432 ||
        sha256_bytes(text) != "4d622f5247b6f9c2f9fb3e75e8d5e15c0fd876b2c312ebab03303eea4b417333") {
        throw VoyemuError("DEPS_METADATA", "unrecognized generated platform-tools metadata");
    }

    if (cache_file_identity(file) != epoch) {
        throw VoyemuError("DEPS_CHANGED", "generated SDK metadata changed during validation");
    }
}

Json tree_manifest(const std::filesystem::path &root, bool sdk_metadata = true) {
    Json files = Json::array();

    for (const auto &item : std::filesystem::recursive_directory_iterator(root)) {
        const auto status = item.symlink_status();

        if (std::filesystem::is_symlink(status) ||
            (!std::filesystem::is_regular_file(status) && !std::filesystem::is_directory(status))) {
            throw VoyemuError("DEPS_TREE", "ordinary installed payload required");
        }

        if (!item.is_regular_file()) {
            continue;
        }

        const auto relative = item.path().lexically_relative(root).generic_string();

        if (sdk_metadata && (relative == "native-deps.json" || relative == ".owner.json")) {
            continue;
        }

        if (sdk_metadata && relative == "platform-tools/package.xml") {
            verify_generated_platform_tools_metadata(root, item.path());
            continue;
        }

        const auto before = cache_file_identity(item.path());
        const auto hash = sha256_file(item.path());

        if (cache_file_identity(item.path()) != before) {
            throw VoyemuError("DEPS_CHANGED", "installed bytes changed during hashing");
        }

        files.push_back({{"path", relative}, {"bytes", item.file_size()}, {"sha256", hash}});
    }

    std::sort(files.begin(), files.end(), [](const Json &a, const Json &b) {
        return a.at("path").get<std::string>() < b.at("path").get<std::string>();
    });

    return files;
}
} // namespace

std::string native_dependency_host() {
    if (os::dependency_host.empty()) {
        throw VoyemuError("DEPS_HOST", "macOS, Windows x64 or Linux x64 required");
    }

    return std::string(os::dependency_host);
}

std::vector<NativeDependency> native_dependencies(std::string_view host) {
    // The NDK prebuilt directory of each pinned host; Apple Silicon runs the x86_64 NDK.
    static const std::map<std::string_view, std::string_view> ndk_hosts{
            {"darwin/arm64", "darwin-x86_64"},
            {"darwin/x64", "darwin-x86_64"},
            {"linux/x64", "linux-x86_64"},
            {"win32/x64", "windows-x86_64"}};
    const auto ndk_host = ndk_hosts.find(host);

    if (ndk_host == ndk_hosts.end()) {
        throw VoyemuError("DEPS_HOST", "pinned host required");
    }

    const bool windows = host == "win32/x64";
    const auto &manifest = embedded_toolchain_manifest();
    const auto &sdk = manifest.at("sdk"), &native = manifest.at("native");
    const auto &pins = sdk.at("archives").at(std::string(host.substr(0, host.find('/'))));
    const auto &emulator =
            pins.contains("emulatorByArch")
                    ? pins.at("emulatorByArch").at(std::string(host.substr(host.find('/') + 1)))
                    : pins.at("emulator");
    const std::string suffix = windows ? ".exe" : "";
    auto pin = [](const Json &value, const std::string &product, const Json &version) {
        return DownloadPin{value.at("url").get<std::string>(),
                           value.at("sha256").get<std::string>(), 3ULL << 30, DownloadEncoding::raw,
                           product + " " + version.get<std::string>()};
    };

    return {{"platform-tools",
             sdk.at("platformTools"),
             pin(pins.at("platformTools"), "Android SDK Platform-Tools", sdk.at("platformTools")),
             {"adb" + suffix}},
            {"emulator",
             sdk.at("emulator"),
             pin(emulator, "Android Emulator", sdk.at("emulator")),
             {"emulator" + suffix}},
            {"build-tools/" + sdk.at("buildTools").get<std::string>(),
             sdk.at("buildTools"),
             pin(pins.at("buildTools"), "Android SDK Build-Tools", sdk.at("buildTools")),
             {"aapt2" + suffix, "zipalign" + suffix}},
            {"ndk/" + native.at("ndk").get<std::string>(),
             native.at("ndk"),
             pin(pins.at("ndk"), "Android NDK", native.at("ndk")),
             {"toolchains/llvm/prebuilt/" + std::string(ndk_host->second) + "/bin/clang" + suffix}},
            {"cmake/" + native.at("cmake").get<std::string>(),
             native.at("cmake"),
             pin(pins.at("cmake"), "CMake", native.at("cmake")),
             {"bin/cmake" + suffix, "bin/ninja" + suffix}}};
}

std::vector<NativeDependency> native_runtime_dependencies(std::string_view host) {
    auto components = native_dependencies(host);

    components.resize(2);

    return components;
}

Json verify_native_runtime_dependencies(const std::filesystem::path &sdk, const Json &binding,
                                        const std::vector<NativeDependency> &supplied,
                                        RuntimeSdkUse use) {
    auto need = [](bool value, const char *reason) {
        if (!value) {
            throw VoyemuError("DEPS_RUNTIME", reason);
        }
    };
    auto digest = [](const Json &value) {
        return value.is_string() && sha256_text(value.get_ref<const std::string &>());
    };
    auto ordinary = [&](const std::filesystem::path &path) {
        need(path.is_absolute() && path.lexically_normal() == path &&
                     std::filesystem::canonical(path) == path,
             "canonical runtime input required");

        for (const auto &at : os::ancestors(path)) {
            need(!std::filesystem::is_symlink(std::filesystem::symlink_status(at)),
                 "linked runtime path rejected");
            need(!os::reparse_point(at), "reparse runtime path rejected");
        }
    };

    ordinary(sdk);
    need(std::filesystem::is_directory(sdk), "ordinary SDK directory required");
    need(binding.is_object() && binding.size() == 4 && binding.at("schema") == 1 &&
                 binding.at("host") == native_dependency_host() &&
                 binding.at("sdk_root") == sdk.string() && binding.at("components").is_array() &&
                 binding.at("components").size() == 2,
         "exact runtime SDK binding required");
    const auto expected = supplied.empty() ? native_runtime_dependencies(native_dependency_host())
                                           : supplied;
    need(expected.size() == 2, "two runtime components required");
    std::map<std::string, NativeDependency> recipes;

    for (const auto &component : expected) {
        need((component.target == "emulator" || component.target == "platform-tools") &&
                     recipes.emplace(component.target, component).second,
             "runtime component recipe differs");
    }

    Json pins = Json::object(), receipts = Json::array();
    std::set<std::string> components;
    std::uint64_t total = 0;

    for (const auto &item : binding.at("components")) {
        need(item.is_object() && item.size() == 4, "exact component binding required");
        const auto target = item.at("target").get<std::string>();
        need(recipes.contains(target) && components.insert(target).second,
             "unknown or duplicate runtime component");

        if (target == "platform-tools" && use == RuntimeSdkUse::launch) {
            continue;
        }

        const auto &recipe = recipes.at(target);
        const std::filesystem::path archive = item.at("archive").get<std::string>(),
                                    receipt = item.at("receipt").get<std::string>();
        // Archives are immutable public inputs, may live outside the runtime root.
        need(archive.is_absolute() && std::filesystem::canonical(archive) == archive &&
                     std::filesystem::is_regular_file(std::filesystem::symlink_status(archive)) &&
                     std::filesystem::hard_link_count(archive) == 1 &&
                     std::filesystem::file_size(archive) <= (3ULL << 30),
             "ordinary bounded pinned archive required");
        const auto archive_identity = cache_file_identity(archive);
        need(sha256_file(archive) == recipe.pin.sha256, "archive differs from source pin");
        ordinary(receipt);
        need(std::filesystem::is_regular_file(receipt) &&
                     std::filesystem::hard_link_count(receipt) == 1 &&
                     std::filesystem::file_size(receipt) <= (2ULL << 20) &&
                     digest(item.at("receipt_sha256")),
             "bounded pinned preparation receipt required");
        const auto receipt_identity = cache_file_identity(receipt);
        need(sha256_file(receipt) == item.at("receipt_sha256").get<std::string>(),
             "preparation receipt pin differs");
        const auto audit = read_private_json(receipt, 2 << 20);
        need(audit.is_object() && audit.size() == 7 && audit.at("schema") == 1 &&
                     audit.at("kind") == "audited-native-sdk-component" &&
                     audit.at("archive_sha256") == recipe.pin.sha256 &&
                     audit.at("archive_bytes") == std::filesystem::file_size(archive) &&
                     audit.at("copied_bytes") == 0 && audit.at("files").is_array() &&
                     !audit.at("files").empty() && audit.at("files").size() <= 10000,
             "preparation audit identity differs");
        const auto directory = sdk / target;
        ordinary(directory);
        std::map<std::string, Json> identities;
        std::set<std::string> audited_paths;
        std::uint64_t component_bytes = 0, audited_bytes = 0;

        for (const auto &entry : audit.at("files")) {
            need(entry.is_object() && entry.size() == 3 && digest(entry.at("sha256")) &&
                         entry.at("bytes").is_number_unsigned(),
                 "exact audited file pin required");
            const auto relative = std::filesystem::path(entry.at("path").get<std::string>());
            need(!relative.empty() && !relative.is_absolute() &&
                         relative == relative.lexically_normal(),
                 "relative audited file required");

            for (const auto &part : relative) {
                need(part != ".." && part != ".", "foreign audited file rejected");
            }

            const auto key = relative.generic_string();
            need(audited_paths.insert(key).second, "duplicate audited file");
            const auto size = entry.at("bytes").get<std::uint64_t>();
            need(size <= (512ULL << 20) && size <= (8ULL << 30) - audited_bytes,
                 "audited file extent differs");
            audited_bytes += size;
            const auto file = std::filesystem::path(directory / relative).make_preferred();
            ordinary(file);
            need(std::filesystem::is_regular_file(file) &&
                         std::filesystem::hard_link_count(file) == 1 &&
                         std::filesystem::file_size(file) == size,
                 "audited file extent or alias differs");
            const auto identity = cache_file_identity(file);
            need(sha256_file(file) == entry.at("sha256").get<std::string>() &&
                         cache_file_identity(file) == identity,
                 "audited runtime bytes changed");
            identities.emplace(key, identity);
            pins[file.string()] = entry.at("sha256");
            component_bytes += size;
        }

        std::size_t count = 0;

        for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
            ordinary(entry.path());
            need(entry.is_regular_file() || entry.is_directory(),
                 "ordinary installed tree required");

            if (!entry.is_regular_file()) {
                continue;
            }

            const auto key = entry.path().lexically_relative(directory).generic_string();
            need(identities.contains(key) &&
                         cache_file_identity(entry.path()) == identities.at(key),
                 "extra or changed runtime file");
            ++count;
        }

        need(count == identities.size(), "audited runtime inventory differs");
        verify_component(directory, recipe, false);
        need(audit.at("bytes") == audited_bytes &&
                     cache_file_identity(receipt) == receipt_identity &&
                     sha256_file(receipt) == item.at("receipt_sha256").get<std::string>() &&
                     cache_file_identity(archive) == archive_identity &&
                     sha256_file(archive) == recipe.pin.sha256,
             "runtime audit changed during validation");
        total += component_bytes;
        receipts.push_back({{"target", target},
                            {"archive_sha256", recipe.pin.sha256},
                            {"receipt_sha256", item.at("receipt_sha256")}});
    }

    return {{"schema", 1},
            {"ready", true},
            {"host", native_dependency_host()},
            {"sdk_root", sdk.string()},
            {"scope", "native runtime SDK dependencies"},
            {"files", pins.size()},
            {"bytes", total},
            {"pins", pins},
            {"components", receipts},
            {"copied_bytes", 0}};
}

bool native_dependency_receipt_matches(const Json &receipt, std::string_view host,
                                       const std::vector<NativeDependency> &expected) {
    if (!receipt.is_object() || receipt.value("schema", 0) != 1 || !receipt.contains("host") ||
        receipt.at("host") != host || !receipt.contains("components") ||
        !receipt.at("components").is_array() ||
        receipt.at("components").size() != expected.size()) {
        return false;
    }

    std::map<std::string, const NativeDependency *> recipes;

    for (const auto &component : expected) {
        if (!recipes.emplace(component.target, &component).second) {
            return false;
        }
    }

    std::set<std::string> seen;

    for (const auto &item : receipt.at("components")) {
        if (!item.is_object() || !item.contains("target") || !item.at("target").is_string()) {
            return false;
        }

        const auto target = item.at("target").get<std::string>();

        if (!recipes.contains(target) || !seen.insert(target).second) {
            return false;
        }

        const auto &recipe = *recipes.at(target);

        if (!item.contains("revision") || item.at("revision") != recipe.revision ||
            !item.contains("archive_sha256") || item.at("archive_sha256") != recipe.pin.sha256) {
            return false;
        }
    }

    return true;
}

Json inspect_external_ndk_inventory(const std::filesystem::path &ndk,
                                    const std::string &archive_pin, std::uint64_t archive_bytes) {
    Json files = Json::array();
    std::uint64_t total = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(ndk)) {
        const auto relative = entry.path().lexically_relative(ndk);
        const auto name = relative.generic_string();
        const auto status = entry.symlink_status();

        if (relative.empty() || relative.is_absolute() || relative != relative.lexically_normal() ||
            name.size() > 2048) {
            throw VoyemuError("DEPS_NDK_AUDIT", "unsafe inventory path");
        }

        for (const auto &part : relative) {
            if (part == "." || part == "..") {
                throw VoyemuError("DEPS_NDK_AUDIT", "unsafe inventory path");
            }
        }

        if (std::filesystem::is_directory(status)) {
            continue;
        }

        if (files.size() >= 100000) {
            throw VoyemuError("DEPS_NDK_AUDIT", "inventory exceeds bound");
        }

        if (std::filesystem::is_symlink(status)) {
            const auto link = std::filesystem::read_symlink(entry.path());

            if (link.empty() || link.is_absolute()) {
                throw VoyemuError("DEPS_NDK_AUDIT", "relative alias required");
            }

            const auto target = std::filesystem::canonical(entry.path()).lexically_relative(ndk);

            if (target.empty() || target.is_absolute() || *target.begin() == ".." ||
                !std::filesystem::is_regular_file(entry.path())) {
                throw VoyemuError("DEPS_NDK_AUDIT", "alias escapes NDK or is not a file");
            }

            files.push_back({{"path", name}, {"link", link.generic_string()}});
        } else {
            if (!std::filesystem::is_regular_file(status) ||
                std::filesystem::hard_link_count(entry.path()) != 1) {
                throw VoyemuError("DEPS_NDK_AUDIT", "ordinary unaliased file required");
            }

            const auto bytes = entry.file_size();

            if (bytes > (8ULL << 30) - total) {
                throw VoyemuError("DEPS_NDK_AUDIT", "inventory bytes exceed bound");
            }

            const auto identity = cache_file_identity(entry.path());
            const auto hash = sha256_file(entry.path());

            if (cache_file_identity(entry.path()) != identity) {
                throw VoyemuError("DEPS_NDK_AUDIT", "input changed while hashing");
            }

            files.push_back({{"path", name}, {"bytes", bytes}, {"sha256", hash}});
            total += bytes;
        }
    }

    std::sort(files.begin(), files.end(), [](const Json &a, const Json &b) {
        return a.at("path").get<std::string>() < b.at("path").get<std::string>();
    });

    return {{"schema", 1},
            {"kind", "audited-full-ndk"},
            {"archive_sha256", archive_pin},
            {"archive_bytes", archive_bytes},
            {"files", files},
            {"bytes", total},
            {"copied_bytes", 0}};
}

std::string native_ndk_tool(std::string_view name) {
    return "toolchains/llvm/prebuilt/" + std::string(os::ndk_host) + "/bin/" + std::string(name) +
           std::string(os::executable_suffix);
}

std::filesystem::path native_ndk_directory(const std::filesystem::path &root) {
    const auto version = embedded_toolchain_manifest().at("native").at("ndk").get<std::string>();
    const char *value = std::getenv("NDK_HOME");

    if (!value || !*value) {
        return root / ".local/toolchains/native-sdk/ndk" / version;
    }

    const std::filesystem::path ndk(value);

    if (!ndk.is_absolute() || ndk != ndk.lexically_normal() ||
        std::filesystem::canonical(ndk) != ndk || !std::filesystem::is_directory(ndk)) {
        throw VoyemuError("DEPS_NDK_PATH", "canonical NDK directory required");
    }

    for (const auto &at : os::ancestors(ndk)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(at))) {
            throw VoyemuError("DEPS_NDK_PATH", "linked NDK root rejected");
        }
    }

    const auto meta = properties(text_file(ndk / "source.properties", 65536));

    if (!meta.contains("Pkg.Revision") || meta.at("Pkg.Revision") != version) {
        throw VoyemuError("DEPS_VERSION", "external NDK revision differs from pin");
    }

    const auto &spec = native_ndk_archive();
    const auto archive_pin = spec.at("sha256").get<std::string>(),
               inventory_pin = spec.at("inventory_sha256").get<std::string>();
    const auto folder = root / ".local/toolchains/ndk-audits",
               receipt = folder / (archive_pin + ".json");
    Json audit;

    if (std::filesystem::exists(receipt)) {
        audit = read_private_json(receipt, 8 << 20);
    } else {
        audit = inspect_external_ndk_inventory(ndk, archive_pin,
                                               spec.at("archive_bytes").get<std::uint64_t>());

        if (sha256_bytes(audit.dump()) != inventory_pin) {
            throw VoyemuError("DEPS_NDK_AUDIT", "inventory differs from pinned upstream inventory");
        }

        ensure_private_directory(folder);
        write_private_json(receipt, audit, 8 << 20);
    }

    if (sha256_bytes(audit.dump()) != inventory_pin || audit.at("kind") != "audited-full-ndk" ||
        audit.at("archive_sha256") != archive_pin || !audit.at("files").is_array() ||
        audit.at("files").size() > 100000) {
        throw VoyemuError("DEPS_NDK_AUDIT", "invalid pinned inventory");
    }

    std::set<std::string> expected;

    for (const auto &entry : audit.at("files")) {
        const auto name = entry.at("path").get<std::string>();
        const std::filesystem::path relative(name);

        if (relative.empty() || relative.is_absolute() || relative != relative.lexically_normal() ||
            !expected.insert(name).second) {
            throw VoyemuError("DEPS_NDK_AUDIT", "invalid inventory path");
        }

        for (const auto &part : relative) {
            if (part == ".." || part == ".") {
                throw VoyemuError("DEPS_NDK_AUDIT", "invalid inventory path");
            }
        }

        const auto file = ndk / relative;

        if (entry.contains("link")) {
            if (!std::filesystem::is_symlink(std::filesystem::symlink_status(file)) ||
                std::filesystem::read_symlink(file).generic_string() !=
                        entry.at("link").get<std::string>()) {
                throw VoyemuError("DEPS_NDK_LINK", "external alias changed");
            }

            const auto target = std::filesystem::canonical(file).lexically_relative(ndk);

            if (target.empty() || target.is_absolute() || *target.begin() == "..") {
                throw VoyemuError("DEPS_NDK_LINK", "external alias escapes NDK");
            }
        } else {
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) ||
                std::filesystem::hard_link_count(file) != 1 ||
                std::filesystem::file_size(file) != entry.at("bytes").get<std::uint64_t>()) {
                throw VoyemuError("DEPS_NDK_FILE", "external NDK file missing or changed");
            }

            const auto identity = cache_file_identity(file);

            if (sha256_file(file) != entry.at("sha256").get<std::string>() ||
                cache_file_identity(file) != identity) {
                throw VoyemuError("DEPS_NDK_FILE", "external NDK bytes changed");
            }
        }
    }

    std::size_t count = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(ndk)) {
        const auto status = entry.symlink_status();

        if (std::filesystem::is_directory(status)) {
            continue;
        }

        if ((!std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)) ||
            !expected.contains(entry.path().lexically_relative(ndk).generic_string())) {
            throw VoyemuError("DEPS_NDK_TREE", "extra or unsupported external NDK file");
        }

        ++count;
    }

    if (count != expected.size()) {
        throw VoyemuError("DEPS_NDK_TREE", "external NDK inventory differs");
    }

    for (const auto &required : {std::string("build/cmake/android.toolchain.cmake"),
                                 native_ndk_tool("clang"), native_ndk_tool("ld.lld")}) {
        if (!expected.contains(required) || !std::filesystem::is_regular_file(ndk / required)) {
            throw VoyemuError("DEPS_NDK_TOOL", "required NDK tool missing");
        }
    }

    return ndk;
}

Json install_native_dependencies(const std::filesystem::path &root, bool offline,
                                 std::uint64_t reserve) {
    if (!root.is_absolute() || std::filesystem::canonical(root) != root) {
        throw VoyemuError("DEPS_ROOT", "ordinary canonical project root required");
    }

    const auto host = native_dependency_host();
    auto components = native_dependencies(host);
    // Native guest components are CI-built release assets, so deps installs no NDK; an existing
    // installation keeps the NDK that its receipt names.
    components.erase(std::remove_if(components.begin(), components.end(),
                                    [](const auto &c) {
                                        return c.target.starts_with("ndk/");
                                    }),
                     components.end());

    const auto manifest_pin = sha256_bytes(embedded_toolchain_manifest().dump());
    const auto sdk = root / ".local/toolchains/native-sdk";
    auto result = [&](bool cached, const Json &files) {
        return Json{{"schema", 1},
                    {"ready", true},
                    {"sdk_root", sdk.string()},
                    {"host", host},
                    {"cached", cached},
                    {"files", files.size()},
                    {"host_java_required", false},
                    {"host_node_required", false},
                    {"host_python_required", false},
                    {"scope", "native SDK dependencies"}};
    };

    if (std::filesystem::exists(sdk) || std::filesystem::is_symlink(sdk)) {
        const auto receipt = read_private_json(sdk / "native-deps.json", 8 * 1024 * 1024);
        auto installed_components = components;

        const auto names_ndk =
                receipt.contains("components") && receipt.at("components").is_array() &&
                std::any_of(receipt.at("components").begin(), receipt.at("components").end(),
                            [](const Json &c) {
                                return c.value("target", std::string{}).starts_with("ndk/");
                            });

        if (names_ndk) {
            installed_components = native_dependencies(host);
        }

        if (!native_dependency_receipt_matches(receipt, host, installed_components)) {
            throw VoyemuError("DEPS_INSTALL", "installed SDK component identity differs");
        }

        for (const auto &component : installed_components) {
            verify_component(sdk / component.target, component, false);
        }

        const auto files = tree_manifest(sdk);

        if (files.size() != receipt.at("files").get<std::size_t>() ||
            sha256_bytes(files.dump()) != receipt.at("tree_sha256").get<std::string>()) {
            throw VoyemuError("DEPS_INSTALL", "installed payload differs from receipt");
        }

        return result(true, files);
    }

    const auto parent = root / ".local/toolchains";
    ensure_private_directory(parent);
    const auto lock = owner_lock(parent / "native-sdk.lock");

    if (!lock->held()) {
        throw VoyemuError("DEPS_LOCK", "installation already owned by another process");
    }

    const auto token = random_token();
    const auto staging = root / ".local/build/tmp" / ("native-sdk-" + token);
    ensure_private_directory(staging);
    struct Stage {
        std::filesystem::path path;
        std::string token;

        ~Stage() {
            try {
                if (read_private_json(path / ".owner.json").at("token") == token) {
                    std::filesystem::remove_all(path);
                }
            } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
            }
        }
    } stage_guard{staging, token};
    write_private_json(staging / ".owner.json", {{"token", token}});
    Json installed = Json::array();

    for (const auto &component : components) {
        // A retained archive is only an input cache, never a prepared SDK/VM shortcut.
        const auto legacy = root / ".local/toolchains/downloads/sdk" /
                            (component.pin.sha256 + ".zip");
        std::filesystem::path archive;

        if (std::filesystem::exists(legacy) || std::filesystem::is_symlink(legacy)) {
            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(legacy)) ||
                std::filesystem::hard_link_count(legacy) != 1 ||
                sha256_file(legacy) != component.pin.sha256) {
                throw VoyemuError("DEPS_SHA", "retained archive differs from pin");
            }

            archive = legacy;
        } else {
            archive = download_pinned(component.pin, root / ".local/toolchains/downloads/native",
                                      offline, reserve)
                              .file;
        }

        progress::report("extract", download_names(component.pin));
        const auto extraction = extract_sdk_archive(archive, component.pin.sha256,
                                                    staging / component.target, reserve, 8ULL << 30,
                                                    !component.target.starts_with("cmake/"));
        verify_component(staging / component.target, component);
        installed.push_back({{"target", component.target},
                             {"revision", component.revision},
                             {"archive_sha256", component.pin.sha256},
                             {"extraction", extraction}});
    }

    const auto files = tree_manifest(staging);
    write_private_json(staging / "native-deps.json", {{"schema", 1},
                                                      {"host", host},
                                                      {"manifest_sha256", manifest_pin},
                                                      {"components", installed},
                                                      {"files", files.size()},
                                                      {"tree_sha256", sha256_bytes(files.dump())}});

    if (std::filesystem::exists(sdk) || std::filesystem::is_symlink(sdk)) {
        throw VoyemuError("DEPS_OUTPUT", "destination appeared during install");
    }

    std::filesystem::rename(staging, sdk);
    std::filesystem::remove(sdk / ".owner.json");

    return result(false, files);
}
} // namespace voyemu
