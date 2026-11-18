#include "profile_apk_nodes.hpp"
#include "check.hpp"
#include "error.hpp"
#include "ordinary_path.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "profile_originals.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <fstream>
#include <map>
namespace voyemu {
namespace {
constexpr Check require{"PROFILE_APK"};

void ordinary(const std::filesystem::path &p) {
    ordinary_path(p, require, PathForm::Canonical, "canonical path required",
                  "linked ancestor rejected");
}

void file(const std::filesystem::path &p, std::uint64_t bound) {
    ordinary(p);
    require(std::filesystem::is_regular_file(p) && std::filesystem::hard_link_count(p) == 1 &&
                    std::filesystem::file_size(p) <= bound,
            "ordinary bounded file required");
}

Json inventory(const std::filesystem::path &source) {
    ordinary(source);
    Json out = Json::array();
    std::uint64_t total = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(source)) {
        require(!entry.is_symlink() && (entry.is_regular_file() || entry.is_directory()),
                "ordinary source tree required");

        if (entry.is_directory()) {
            continue;
        }

        file(entry.path(), 32ULL << 20);
        const auto size = entry.file_size();
        require(out.size() < 4096 && size <= (32ULL << 20) - total, "source inventory bounds");
        total += size;
        const auto epoch = cache_file_identity(entry.path());
        const auto hash = sha256_file(entry.path());
        require(cache_file_identity(entry.path()) == epoch, "source changed during hashing");
        out.push_back({{"path", entry.path().lexically_relative(source).generic_string()},
                       {"sha256", hash},
                       {"bytes", size}});
    }

    std::sort(out.begin(), out.end(), [](const Json &a, const Json &b) {
        return a.at("path").get<std::string>() < b.at("path").get<std::string>();
    });

    return out;
}
} // namespace

ProfileBinaryApkNode plan_profile_binary_apk_node(const std::filesystem::path &root,
                                                  std::string_view profile, std::string_view name,
                                                  const std::filesystem::path &build,
                                                  const std::filesystem::path &compiler,
                                                  const std::string &compiler_sha256) {
    ordinary(root);
    require(valid_profile_name(profile), "safe profile name required");
    require(name == "VehicleSetting" || name == "VehicleAir", "unsupported binary profile node");
    ordinary(build);
    const auto profile_dir = root / "profiles" / std::string(profile),
               profile_file = profile_dir / "profile.json",
               patch_file = profile_dir / "patches/add-qg-runtime.json",
               receipt_file = build / "receipt.json";
    file(profile_file, 512 * 1024);
    file(patch_file, 128 * 1024);
    file(receipt_file, 128 * 1024);
    std::map<std::filesystem::path, std::pair<Json, std::string>> tracked;
    auto track = [&](const std::filesystem::path &p) {
        const auto epoch = cache_file_identity(p);
        const auto hash = sha256_file(p);
        require(cache_file_identity(p) == epoch, "input changed during hashing");
        tracked[p] = {epoch, hash};

        return hash;
    };
    file(compiler, 512ULL << 20);
    require(sha256_text(compiler_sha256) && track(compiler) == compiler_sha256,
            "native compiler pin mismatch");
    track(profile_file);
    track(patch_file);
    const auto receipt_sha = track(receipt_file);
    const auto definition = read_private_json(profile_file, 512 * 1024);
    require(definition.at("schema") == 1, "profile schema");
    const auto &declaration = definition.at("patches").at(std::string(name));
    require(declaration == Json({{"type", "apk"},
                                 {"input", "original:" + std::string(name)},
                                 {"uses", Json::array({"component:qg-runtime"})},
                                 {"steps", Json::array({"add-qg-runtime.json"})},
                                 {"sign", "platform"}}),
            "binary node declaration differs from implemented recipe");
    require(definition.at("components").at("qg-runtime") == "components/qg-runtime",
            "component source mapping changed");
    bool installed = false;

    for (const auto &item : definition.at("install")) {
        if (item.value("artifact", std::string{}) == "patched:" + std::string(name)) {
            require(item == Json({{"artifact", "patched:" + std::string(name)}, {"type", "apk"}}) &&
                            !installed,
                    "ambiguous install declaration");
            installed = true;
        }
    }

    require(installed, "node is not installed by profile");
    const auto &original = definition.at("original").at(std::string(name));
    const auto original_name = original.at("file").get<std::string>(),
               original_pin = original.at("sha256").get<std::string>();
    require(original_name == std::string(name) + ".apk" && sha256_text(original_pin),
            "original APK declaration");
    const auto source = profile_originals_directory(root, profile) / original_name;
    file(source, 1ULL << 30);
    require(track(source) == original_pin, "original APK differs from profile pin");
    const auto component = profile_dir / "components/qg-runtime";
    const auto local = inventory(component);
    auto recipe = read_private_json(component / "component.json");
    const auto shared = recipe.value("sourceFiles", Json::object());
    recipe.erase("sourceFiles");
    require(recipe == Json({{"type", "native"},
                            {"minSdk", 28},
                            {"output", "libqg_runtime.so"},
                            {"libs", Json::array({"log"})}}),
            "unsupported native component recipe");

    // The producer compiles the local tree plus each common source that sourceFiles declares.
    for (auto it = shared.begin(); it != shared.end(); ++it) {
        require(it.key().starts_with("src/") &&
                        it.value() == "voyemu/profiles/common/components/qg-runtime/" + it.key(),
                "common component source mapping required");
        const auto common = root /
                            it.value().get<std::string>().substr(std::string("voyemu/").size());
        file(common, 32ULL << 20);
        track(common);
    }

    const auto receipt = read_private_json(receipt_file);
    require(receipt.at("schema") == 1 && receipt.at("type") == "native" &&
                    receipt.at("api") == 28 && receipt.at("abi") == "arm64-v8a" &&
                    receipt.at("output") == "libqg_runtime.so" && receipt.at("language") == "c11" &&
                    receipt.at("host_java_required") == false &&
                    receipt.at("host_node_required") == false &&
                    receipt.at("host_python_required") == false,
            "native API28 producer receipt required");
    require(receipt.at("compiler_sha256") == compiler_sha256 &&
                    receipt.at("guest_sonames").empty() &&
                    receipt.at("materialized_source").empty(),
            "producer tool or dependency mismatch");
    const auto library = build / "libqg_runtime.so";
    file(library, 32ULL << 20);
    const auto library_pin = track(library);
    require(receipt.at("sha256") == library_pin &&
                    receipt.at("bytes") == std::filesystem::file_size(library),
            "native producer output mismatch");
    std::ifstream input(library, std::ios::binary);
    std::string payload((std::istreambuf_iterator<char>(input)), {});
    require(payload.size() >= 64 &&
                    payload.substr(0, 4) == std::string("\x7f"
                                                        "ELF",
                                                        4) &&
                    payload[4] == 2 && payload[5] == 1 && payload[16] == 3 && payload[17] == 0 &&
                    static_cast<unsigned char>(payload[18]) == 183 && payload[19] == 0,
            "ARM64 shared ELF required");
    require(sha256_bytes(payload) == library_pin, "library changed during read");
    ProfileBinaryApkNode node;
    node.profile = profile;
    node.name = name;
    node.request.source = source;
    node.request.source_sha256 = original_pin;
    node.request.additions = {{"lib/arm64-v8a/libqg_runtime.so", std::move(payload), library_pin}};
    node.request.binary_provenance = {{"profile", profile},
                                      {"node", name},
                                      {"component", "qg-runtime"},
                                      {"component_receipt_sha256", receipt_sha},
                                      {"compiler_sha256", receipt.at("compiler_sha256")}};
    node.request.prepublication_guard = [tracked, component, local] {
        for (const auto &[path, value] : tracked) {
            require(cache_file_identity(path) == value.first && sha256_file(path) == value.second,
                    "bound input changed before publication");
        }

        require(inventory(component) == local,
                "profile component source changed before publication");
    };
    node.request.prepublication_guard();

    return node;
}

ApkBuildDraft build_profile_binary_apk_node(ProfileBinaryApkNode node,
                                            const std::filesystem::path &zipalign,
                                            const std::string &zipalign_sha256,
                                            const ApkSigningIdentity &identity,
                                            const std::string &base_certificate_sha256,
                                            const std::filesystem::path &destination,
                                            std::uint64_t reserve) {
    require(static_cast<bool>(node.request.prepublication_guard), "profile binding required");
    node.request.prepublication_guard();
    node.request.zipalign = zipalign;
    node.request.zipalign_sha256 = zipalign_sha256;
    node.request.identity = identity;
    node.request.base_certificate_sha256 = base_certificate_sha256;

    return publish_native_apk_draft(node.request, destination, reserve);
}
} // namespace voyemu
