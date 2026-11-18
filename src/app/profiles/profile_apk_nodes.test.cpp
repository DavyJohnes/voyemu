#include "profile_apk_nodes.hpp"
#include "native_component.hpp"
#include "process.hpp"
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
void put(const std::filesystem::path &path, const std::string &bytes) {
    ensure_private_directory(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), bytes.size());

    if (!stream) {
        throw std::runtime_error("fixture write failed");
    }
}

void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--target=aarch64-linux-android28") {
            for (int i = 1; i + 1 < argc; ++i) {
                if (std::string_view(argv[i]) == "-o") {
                    std::string elf(64, '\0');
                    elf.replace(0, 4,
                                std::string("\x7f"
                                            "ELF",
                                            4));
                    elf[4] = 2;
                    elf[5] = 1;
                    elf[16] = 3;
                    elf[18] = static_cast<char>(183);
                    put(argv[i + 1], elf);

                    return 0;
                }
            }

            throw std::runtime_error("compiler fixture output missing");
        }

        if (argc == 8 && std::string(argv[1]) == "--plan") {
            auto node = plan_profile_binary_apk_node(std::filesystem::canonical(argv[2]), argv[3],
                                                     argv[4], std::filesystem::canonical(argv[5]),
                                                     std::filesystem::canonical(argv[6]), argv[7]);
            std::cout << node.request.binary_provenance.dump() << '\n';

            return 0;
        }

        check(argc == 1, "usage: --plan ROOT PROFILE NODE NATIVE_BUILD COMPILER COMPILER_PIN");
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-profile-apk-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{root};
        const std::string profile = "free-09-test";
        const auto directory = root / "profiles" / profile,
                   component = directory / "components/qg-runtime", build = root / "build";
        ensure_private_directory(component / "src");
        const auto repository = std::filesystem::path(__FILE__)
                                        .parent_path()
                                        .parent_path()
                                        .parent_path()
                                        .parent_path();
        std::ifstream patch_input(
                repository / "profiles/free-09-chinese-20260106-6.0.3/patches/add-qg-runtime.json",
                std::ios::binary);
        const std::string patch((std::istreambuf_iterator<char>(patch_input)), {});
        check(!patch.empty(), "real implemented patch fixture missing");
        put(directory / "patches/add-qg-runtime.json", patch);
        const Json recipe = {{"type", "native"},
                             {"minSdk", 28},
                             {"output", "libqg_runtime.so"},
                             {"libs", Json::array({"log"})}};
        write_private_json(component / "component.json", recipe);
        put(component / "src/stub.c", "int stub(void){return 0;}\n");
        std::string elf(64, '\0');
        elf.replace(0, 4,
                    std::string("\x7f"
                                "ELF",
                                4));
        elf[4] = 2;
        elf[5] = 1;
        elf[16] = 3;
        elf[18] = static_cast<char>(183);
        const auto compiler = root / "clang-fixture";
        std::filesystem::copy_file(std::filesystem::canonical(argv[0]), compiler);
        const auto compiler_pin = sha256_file(compiler);
        const auto receipt = build_native_component(component, compiler, compiler_pin, build, 0);
        check(receipt.at("sha256") == sha256_bytes(elf) && !receipt.contains("sources") &&
                      !receipt.contains("source_tree_sha256"),
              "real native producer publishes its artifact without repository source pins");
        const auto original = root / ".local/profiles" / profile / "originals/VehicleAir.apk";
        put(original, "original fixture");
        Json definition = {
                {"schema", 1},
                {"components", {{"qg-runtime", "components/qg-runtime"}}},
                {"patches",
                 {{"VehicleAir",
                   {{"type", "apk"},
                    {"input", "original:VehicleAir"},
                    {"uses", Json::array({"component:qg-runtime"})},
                    {"steps", Json::array({"add-qg-runtime.json"})},
                    {"sign", "platform"}}}}},
                {"original",
                 {{"VehicleAir", {{"file", "VehicleAir.apk"}, {"sha256", sha256_file(original)}}}}},
                {"install", Json::array({{{"artifact", "patched:VehicleAir"}, {"type", "apk"}}})}};
        write_private_json(directory / "profile.json", definition);
        auto node = plan_profile_binary_apk_node(root, profile, "VehicleAir", build, compiler,
                                                 compiler_pin);
        check(node.request.templates.empty() && node.request.additions.size() == 1 &&
                      node.request.additions[0].bytes == elf,
              "binary node payload binding failed");
        check(!node.request.binary_provenance.contains("profile_sha256") &&
                      !node.request.binary_provenance.contains("patch_sha256") &&
                      !node.request.binary_provenance.contains("source_tree_sha256") &&
                      node.request.binary_provenance.at("compiler_sha256") == compiler_pin &&
                      node.request.binary_provenance.at("component_receipt_sha256") ==
                              sha256_file(build / "receipt.json"),
              "binary provenance retains artifact and tool integrity without own source pins");
        node.request.prepublication_guard();
        put(compiler, "changed compiler");
        bool compiler_refused = false;
        try {
            node.request.prepublication_guard();
        } catch (...) {
            compiler_refused = true;
        }

        check(compiler_refused, "compiler mutation accepted");
        std::filesystem::copy_file(std::filesystem::canonical(argv[0]), compiler,
                                   std::filesystem::copy_options::overwrite_existing);
        put(build / "libqg_runtime.so", elf + "changed output");
        bool output_refused = false;
        try {
            plan_profile_binary_apk_node(root, profile, "VehicleAir", build, compiler,
                                         compiler_pin);
        } catch (...) {
            output_refused = true;
        }

        check(output_refused, "native artifact mutation accepted");
        put(build / "libqg_runtime.so", elf);
        node = plan_profile_binary_apk_node(root, profile, "VehicleAir", build, compiler,
                                            compiler_pin);
        put(original, "changed");
        bool refused = false;
        try {
            node.request.prepublication_guard();
        } catch (...) {
            refused = true;
        }

        check(refused, "original mutation accepted");
        put(original, "original fixture");
        node = plan_profile_binary_apk_node(root, profile, "VehicleAir", build, compiler,
                                            compiler_pin);
        put(component / "src/stub.c", "changed source");
        refused = false;
        try {
            node.request.prepublication_guard();
        } catch (...) {
            refused = true;
        }

        check(refused, "component source mutation accepted");
        node = plan_profile_binary_apk_node(root, profile, "VehicleAir", build, compiler,
                                            compiler_pin);
        check(node.request.additions[0].bytes == elf,
              "native artifact remains usable after repository source edits");
        const auto common = root / "profiles/common/components/qg-runtime/src/shared.c";
        put(common, "int shared(void){return 1;}\n");
        auto shared_recipe = recipe;
        shared_recipe["sourceFiles"] = {
                {"src/shared.c", "voyemu/profiles/common/components/qg-runtime/src/shared.c"}};
        write_private_json(component / "component.json", shared_recipe);
        node = plan_profile_binary_apk_node(root, profile, "VehicleAir", build, compiler,
                                            compiler_pin);
        node.request.prepublication_guard();
        put(common, "int shared(void){return 2;}\n");
        bool shared_refused = false;
        try {
            node.request.prepublication_guard();
        } catch (...) {
            shared_refused = true;
        }

        check(shared_refused, "common component source mutation during operation accepted");
        refused = false;
        try {
            plan_profile_binary_apk_node(root, profile, "Launcher", build, compiler, compiler_pin);
        } catch (...) {
            refused = true;
        }

        check(refused, "DEX patch node misclassified binary-only");
        std::cout << "profile binary APK node binding and mutation guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
