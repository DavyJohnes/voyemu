#include "launch_audit.hpp"
#include "android_launch_adb.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;

int main(int argc, char **argv) try {
    assert(argc == 2);
    const auto root = std::filesystem::canonical(argv[1]) / ".local/build/tmp" /
                      ("launch-audit-" + random_token());
    ensure_private_directory(root);
    const auto sdk = root / "sdk", asset = root / "icon.svg", program = root / "voyemu";
    std::ofstream(sdk) << "SDK";
    std::ofstream(asset) << "SVG";
    std::ofstream(program) << "new build";
    const auto sdk_pin = Json{{"path", sdk.string()}, {"sha256", sha256_file(sdk)}},
               asset_pin = Json{{"path", asset.string()}, {"sha256", sha256_file(asset)}},
               old_program = Json{{"path", program.string()}, {"sha256", std::string(64, 'a')}};
    Json launch{{"schema", 1},
                {"executable", sdk.string()},
                {"pins", Json::array({sdk_pin, asset_pin, old_program})},
                {"frontend",
                 {{"schema", 1},
                  {"backend", "voyemu"},
                  {"executable", program.string()},
                  {"pins", Json::array({asset_pin, old_program})}}}};
    audit_launch_pins(launch);
    auto reject = [&](const Json &value) {
        bool failed = false;
        try {
            audit_launch_pins(value);
        } catch (const std::exception &error) {
            failed = std::string(error.what()).starts_with("LAUNCH_PIN:");
        }

        assert(failed);
    };
    auto legacy = launch;
    legacy["android_sdk_root"] = root.string();
#ifdef _WIN32
    const auto adb = root / "platform-tools" / "adb.exe";
#else
    const auto adb = root / "platform-tools" / "adb";
#endif
    const auto adb_pin = Json{{"path", adb.string()}, {"sha256", std::string(64, 'b')}};
    legacy["adb"] = adb.string();
    legacy["pins"].push_back(adb_pin);
    reject(legacy);
    auto ready = legacy;
    normalize_ready_android_adb(ready);
    audit_launch_pins(ready);
    assert(ready.at("adb_transport") == "direct" && !ready.contains("adb") &&
           ready.at("pins") == launch.at("pins"));
    auto unrelated = legacy;
    unrelated["adb"] = (root / "foreign-adb").string();
    normalize_ready_android_adb(unrelated);
    reject(unrelated);
    std::ofstream(asset) << "changed";
    reject(ready);
    reject(launch);
    std::ofstream(asset) << "SVG";
    audit_launch_pins(launch);
    auto custom = launch;
    custom["frontend"]["executable"] = (root / "fixture-backend").string();
    custom["frontend"]["backend"] = "custom";
    reject(custom);
    std::ofstream(sdk) << "changed";
    reject(launch);
    std::filesystem::remove_all(root);
    std::cout << "Explicit check audits SDK/assets and leaves current Voyemu version unbound\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
