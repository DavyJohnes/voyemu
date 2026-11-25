#include "error.hpp"
#include "profile_build.hpp"
#include "profile_catalog.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char **argv) {
    if (argc != 5 && argc != 6) {
        std::cerr
                << "Usage: voyemu_avd_filesystem_inputs ROOT PROFILE ARTIFACT_RECEIPT SHA256 [x86]\n";

        return 2;
    }

    try {
        const auto root = std::filesystem::canonical(argv[1]);
        const std::string_view target = argc == 6 ? argv[5] : "";

        if (!target.empty() && target != "x86") {
            throw voyemu::VoyemuError("AVD_FILESYSTEM", "unsupported build target");
        }

        const auto receipt = voyemu::validate_profile_artifact_resume(
                root, argv[2], std::filesystem::absolute(argv[3]), argv[4], target);
        const auto profile = voyemu::authoring_profile(root, argv[2], target);
        const auto result = voyemu::read_private_json(
                std::filesystem::path(receipt.at("transport").get<std::string>()) / "result.json",
                4ULL << 20);
        const auto &artifacts = result.at("artifacts");
        voyemu::Json entries = voyemu::Json::array();

        for (auto entry : profile.at("install")) {
            if (entry.at("type") != "file") {
                throw voyemu::VoyemuError("AVD_FILESYSTEM", "ordinary file install required");
            }

            entry["source"] = artifacts.at(entry.at("artifact").get<std::string>());
            entries.push_back(std::move(entry));
        }

        const voyemu::Json plan = {
                {"profile", profile.at("id")}, {"base", receipt.at("base")},
                {"sdk", receipt.at("sdk")},    {"android", profile.at("android")},
                {"source_sha256", argv[4]},    {"install", entries}};
        std::cout << plan.dump() << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }

    return 0;
}
