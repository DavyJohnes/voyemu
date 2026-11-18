#pragma once
#include "model.hpp"
#include <map>
#include <span>
#include <stdexcept>
namespace voyemu {
inline bool windows_android_renderer_policy(const Json &specification, bool required = false) {
    if (!specification.contains("rendererPolicy")) {
        if (required) {
            throw VoyemuError("ANDROID_RENDERER_POLICY",
                              "prepared Windows renderer policy required");
        }

        return false;
    }

    const auto &policy = specification.at("rendererPolicy");

    if (!policy.is_string() || policy != "windows-angle-36.3.10") {
        throw VoyemuError("ANDROID_RENDERER_POLICY", "unknown prepared Windows renderer policy");
    }

    if (specification.contains("args")) {
        const auto &args = specification.at("args");

        if (!args.is_array()) {
            throw VoyemuError("ANDROID_RENDERER_POLICY", "argument array required");
        }

        for (const auto &argument : args) {
            if (!argument.is_string()) {
                throw VoyemuError("ANDROID_RENDERER_POLICY", "string argument required");
            }

            const auto value = argument.get<std::string>();

            if (value == "-gpu" || value.starts_with("-gpu=") || value == "-feature" ||
                value.starts_with("-feature=")) {
                throw VoyemuError("ANDROID_RENDERER_POLICY",
                                  "renderer argument override forbidden");
            }
        }
    }

    return true;
}

struct WindowsRendererPin {
    std::string path, sha256;
    std::uint64_t bytes;
};
struct WindowsRendererRecipe {
    std::string sdk_archive_sha256;
    std::vector<WindowsRendererPin> files;
};
const WindowsRendererRecipe &windows_android_renderer_recipe();
struct WindowsAndroidRenderer {
    std::filesystem::path launcher_directory;
    std::map<std::string, std::string> environment;
    std::string gpu_mode;
    Json features, receipt;
};
// Fresh session-private overlay only. No SDK files are rewritten or copied.
WindowsAndroidRenderer prepare_windows_android_renderer(
        const std::filesystem::path &sdk, const std::filesystem::path &private_directory,
        std::string_view inherited_path = {},
        const WindowsRendererRecipe &recipe = windows_android_renderer_recipe());
// Runtime layout only; dependency integrity is audited by explicit preparation/check.
WindowsAndroidRenderer
prepare_windows_android_renderer_layout(const std::filesystem::path &sdk,
                                        const std::filesystem::path &private_directory,
                                        std::string_view inherited_path = {});
} // namespace voyemu
