#include "profile_step_inputs.hpp"
#include "process.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace voyemu;

void check(bool value) {
    if (!value) {
        throw std::runtime_error("profile step input contract differs");
    }
}

template <typename Work> void rejects(Work work, std::string_view code) {
    try {
        work();
    } catch (const VoyemuError &error) {
        check(error.code() == code);

        return;
    }

    throw std::runtime_error("invalid profile step input accepted");
}
} // namespace

int main() {
    try {
        const auto jobs = std::filesystem::current_path() / ".local" / "build" / "jobs";
        std::filesystem::create_directories(jobs);
        const auto root =
                std::filesystem::canonical(jobs) /
                ("voyemu-step-inputs-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
        struct Cleanup {
            std::filesystem::path root;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(root, error);
            }
        } cleanup{root};
        const auto source = root / "source";
        std::filesystem::create_directories(source / "lib");
        std::ofstream(source / "lib" / "b.so", std::ios::binary) << "bb";
        std::ofstream(source / "a.json", std::ios::binary) << "a";
        // Both binary profile steps share the checks and keep their own error code.
        constexpr ProfileStepInputs apk{Check{"PROFILE_APK"}};
        constexpr ProfileStepInputs patch{Check{"BINARY_PATCH"}};
        apk.ordinary(source);
        apk.file(source / "a.json", 1);
        const auto inventory = patch.inventory(source);
        check(inventory ==
              Json::array({{{"path", "a.json"}, {"sha256", sha256_bytes("a")}, {"bytes", 1}},
                           {{"path", "lib/b.so"}, {"sha256", sha256_bytes("bb")}, {"bytes", 2}}}));
        rejects(
                [&] {
                    apk.file(source / "lib" / "b.so", 1);
                },
                "PROFILE_APK");
        rejects(
                [&] {
                    patch.file(source / "lib", 1 << 20);
                },
                "BINARY_PATCH");
        rejects(
                [&] {
                    apk.ordinary(source / "." / "a.json");
                },
                "PROFILE_APK");
        std::filesystem::create_hard_link(source / "a.json", root / "second-name");
        rejects(
                [&] {
                    patch.file(source / "a.json", 1);
                },
                "BINARY_PATCH");
        rejects(
                [&] {
                    apk.inventory(source);
                },
                "PROFILE_APK");
        std::filesystem::remove(root / "second-name");
        std::filesystem::create_directory_symlink(source, root / "alias");
        rejects(
                [&] {
                    patch.ordinary(root / "alias" / "a.json");
                },
                "BINARY_PATCH");
        std::filesystem::create_symlink(source / "a.json", source / "link");
        rejects(
                [&] {
                    apk.inventory(source);
                },
                "PROFILE_APK");
        std::cout << "profile step path, file and inventory contracts passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
