#include "profile_patch_graph.hpp"
#include "process.hpp"
#include "profile_catalog.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>
using namespace voyemu;
namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 4 && std::string(argv[1]) == "--plan") {
            std::cout << plan_profile_patch_graph(std::filesystem::canonical(argv[2]), argv[3])
                                 .dump(2)
                      << '\n';

            return 0;
        }

        check(argc == 1, "usage: --plan ROOT PROFILE");
        const auto repository = std::filesystem::path(__FILE__)
                                        .parent_path()
                                        .parent_path()
                                        .parent_path()
                                        .parent_path();
        const std::string profile =
                embedded_profile_names(ProfilePlatform::qinggan09, ProfileBoard::sa8155).at(0);
        const auto graph = plan_profile_patch_graph(repository, profile);
        check(!graph.contains("source_pins") && !graph.contains("profile_sha256"),
              "graph persists authoring source pins");
        check(!graph.at("patches_supported").get<bool>() && !graph.at("machine_ready").get<bool>(),
              "partial graph declared ready");
        const auto current_definition =
                read_private_json(repository / "profiles" / profile / "profile.json", 1 << 20);
        check(graph.at("nodes").size() == current_definition.at("patches").size(),
              "profile nodes omitted");
        std::set<std::string> source_steps;

        for (const auto &node : current_definition.at("patches")) {
            for (const auto &step : node.at("steps")) {
                source_steps.insert(step.get<std::string>());
            }
        }

        check(source_steps.size() == 44 && source_steps.contains("systemservice-network.json"),
              "profile steps omitted");
        std::set<std::string> planned_steps;

        for (const auto &node : graph.at("nodes")) {
            for (const auto &step : node.at("steps")) {
                check(!step.contains("source_sha256"), "step persists authoring source pin");
                planned_steps.insert(step.at("name").get<std::string>());
            }
        }

        check(planned_steps == source_steps, "profile step coverage differs");

        const auto media = std::find_if(graph.at("nodes").begin(), graph.at("nodes").end(),
                                        [](const auto &node) {
                                            return node.at("name") == "QGMedia";
                                        });
        check(media != graph.at("nodes").end(), "OEM USB media node omitted");
        check(media->at("steps").size() == 3 &&
                      media->at("steps")[0].at("name") == "media-runtime.json" &&
                      media->at("steps")[1].at("name") == "api28-usb-player.json" &&
                      media->at("steps")[2].at("name") == "add-qg-runtime.json",
              "OEM USB decoder ordered steps omitted");
        const auto phone = std::find_if(graph.at("nodes").begin(), graph.at("nodes").end(),
                                        [](const auto &node) {
                                            return node.at("name") == "BluetoothPhone";
                                        });
        check(phone != graph.at("nodes").end(), "phone provider node omitted");
        check(phone->at("steps").size() == 3 &&
                      phone->at("steps")[0].at("name") == "systemservice-framework.json" &&
                      phone->at("steps")[1].at("name") == "add-qg-runtime.json" &&
                      phone->at("steps")[2].at("name") == "phone-provider.json",
              "phone provider ordered steps omitted");
        std::size_t file_supported = 0, supported_nodes = 0;
        bool vehicle_air = false, vehicle_setting = false, launcher_missing = false,
             navi_first = false;
        std::size_t cil_index = 0, hash_index = 0;

        for (std::size_t i = 0; i < graph.at("nodes").size(); ++i) {
            const auto &node = graph.at("nodes")[i];
            const auto name = node.at("name").get<std::string>();

            if (node.at("supported").get<bool>()) {
                ++supported_nodes;

                if (node.at("type") == "file") {
                    ++file_supported;
                }
            }

            if (name == "VehicleAir") {
                vehicle_air = node.at("supported");
            }

            if (name == "VehicleSetting") {
                vehicle_setting = node.at("supported");
            }

            if (name == "Launcher") {
                launcher_missing = !node.at("supported").get<bool>();
                check(node.at("steps")[1].at("dispatch") == "launcher-media-redirect" &&
                              !node.at("steps")[1].at("composable").get<bool>(),
                      "unknown preceding DEX edit silently composed");
            }

            if (name == "PateoNavi") {
                navi_first = node.at("steps")[0].at("composable");
            }

            if (name == "can-early-policy-a9") {
                cil_index = i;
            }

            if (name == "selinux-hash-a9") {
                hash_index = i;
            }
        }

        check(file_supported == 12 && supported_nodes == 14,
              "native file nodes not integrated or unsupported APK promoted");
        const auto selinux_canbus = std::find_if(
                graph.at("nodes").begin(), graph.at("nodes").end(), [](const auto &node) {
                    return node.at("name") == "can-early-policy-a9";
                });
        check(selinux_canbus != graph.at("nodes").end(), "A9 CAN early policy node omitted");
        check(vehicle_air && vehicle_setting && launcher_missing && navi_first &&
                      cil_index < hash_index,
              "dispatch coverage or dependency order invalid");
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-patch-graph-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path p;

            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove_all(p, e);
            }
        } cleanup{root};
        const auto target = root / "profiles" / profile;
        ensure_private_directory(target);
        const auto definition =
                read_private_json(repository / "profiles" / profile / "profile.json", 512 * 1024);
        write_private_json(target / "profile.json", definition);

        for (const auto &step : source_steps) {
            const auto dest = target / "patches" / step;
            ensure_private_directory(dest.parent_path());
            std::filesystem::copy_file(repository / "profiles" / profile / "patches" / step, dest);
        }

        const auto wrapper = profile + "-copy";
        const auto wrapper_directory = root / "profiles" / wrapper;
        ensure_private_directory(wrapper_directory / "patches");
        auto wrapper_definition = definition;
        wrapper_definition["id"] = wrapper;
        wrapper_definition["local"] = true;
        wrapper_definition["capture"] = {{"profile", wrapper}, {"referenceProfile", profile}};
        write_private_json(wrapper_directory / "profile.json", wrapper_definition);

        for (const auto &step : source_steps) {
            const auto destination = wrapper_directory / "patches" / step;
            ensure_private_directory(destination.parent_path());
            std::filesystem::copy_file(repository / "profiles" / profile / "patches" / step,
                                       destination);
        }

        const auto wrapper_graph = plan_profile_patch_graph(root, wrapper);
        const auto wrapper_codec =
                std::find_if(wrapper_graph.at("nodes").begin(), wrapper_graph.at("nodes").end(),
                             [](const auto &node) {
                                 return node.at("name") == "ccodec-allocation-a9";
                             });
        check(wrapper_codec != wrapper_graph.at("nodes").end() &&
                      wrapper_codec->at("supported") == true,
              "stable A9 copied local wrapper must use current codec recipe");
        auto simulator_supported = [](const Json &value) {
            for (const auto &n : value.at("nodes")) {
                if (n.at("name") == "can-simulator-contexts-a9") {
                    return n.at("supported").get<bool>();
                }
            }

            return false;
        };
        check(!simulator_supported(wrapper_graph),
              "A9 reference family must not enable an undeclared simulator recipe");
        const std::string simulator_step =
                "{\n  \"api\": 28,\n  \"calls\": [],\n  \"constants\": {\n    \"KIND\": \"can-simulator-file-contexts\"\n  },\n  \"schema\": 1\n}\n";
        {
            std::ofstream recipe(wrapper_directory / "patches/can-simulator-contexts-a9.json",
                                 std::ios::binary);
            recipe << simulator_step;
        }
        auto candidate = wrapper_definition;
        candidate["patches"].erase("can-early-file-contexts-a9");
        candidate["patches"]["can-simulator-contexts-a9"] = {
                {"type", "file"},
                {"input", "android:/system/etc/selinux/plat_file_contexts"},
                {"steps", Json::array({"can-simulator-contexts-a9.json"})}};
        write_private_json(wrapper_directory / "profile.json", candidate);
        check(simulator_supported(plan_profile_patch_graph(root, wrapper)),
              "explicit A9 simulator recipe must be supported");
        auto bad = candidate;
        bad["board"] = "nxp";
        write_private_json(wrapper_directory / "profile.json", bad);
        check(!simulator_supported(plan_profile_patch_graph(root, wrapper)),
              "A9 simulator recipe accepted on a foreign board");
        bad = candidate;
        bad["android"]["package"] = "system-images;android-29;default;arm64-v8a";
        write_private_json(wrapper_directory / "profile.json", bad);
        check(!simulator_supported(plan_profile_patch_graph(root, wrapper)), "wrong SDK accepted");
        bad = candidate;
        bad["patches"]["can-simulator-contexts-a9"]["steps"] =
                Json::array({"can-simulator-contexts-a9.json", "can-early-file-contexts-a9.json"});
        write_private_json(wrapper_directory / "profile.json", bad);
        check(!simulator_supported(plan_profile_patch_graph(root, wrapper)),
              "conflicting context nodes accepted");
        write_private_json(wrapper_directory / "profile.json", wrapper_definition);
        std::ofstream(target / "patches/add-qg-runtime.json", std::ios::app)
                << "\n// changed recipe\n";
        const auto changed = plan_profile_patch_graph(root, profile);
        check(changed == graph, "recipe source bytes became a persisted graph contract");
        auto cyclic = definition;
        cyclic["patches"]["VehicleAir"]["uses"].push_back("patched:VehicleSetting");
        cyclic["patches"]["VehicleSetting"]["uses"].push_back("patched:VehicleAir");
        write_private_json(target / "profile.json", cyclic);
        bool refused = false;
        try {
            plan_profile_patch_graph(root, profile);
        } catch (const std::exception &e) {
            refused = std::string(e.what()).find("cycle") != std::string::npos;
        }

        check(refused, "dependency cycle accepted");
        std::cout << "native profile patch graph coverage, composition and cycle guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
