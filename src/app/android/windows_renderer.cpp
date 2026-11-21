#include "windows_renderer.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include "toolchain_manifest.hpp"
#include <algorithm>
#include <array>
namespace voyemu {
const WindowsRendererRecipe &windows_android_renderer_recipe() {
    // The renderer files of the Windows Emulator archive, pinned in sdk.runtimeFilePins.
    static const WindowsRendererRecipe recipe = [] {
        const std::string host = "win32/x64";
        WindowsRendererRecipe result{embedded_toolchain_manifest()
                                             .at("sdk")
                                             .at("runtimeFilePins")
                                             .at("win32")
                                             .at("x64")
                                             .at("emulatorArchiveSha256")
                                             .get<std::string>(),
                                     {}};

        for (const auto *name :
             {"source.properties", "lib64/gles_angle/d3dcompiler_47.dll",
              "lib64/gles_angle/libEGL.dll", "lib64/gles_angle/libGLESv2.dll",
              "lib64/gles_angle/vk_swiftshader.dll", "lib64/gles_angle/vk_swiftshader_icd.json",
              "lib64/gles_angle/vulkan-1.dll"}) {
            const auto file = "emulator/" + std::string(name);
            result.files.push_back({name, native_runtime_file_pin(host, file),
                                    native_runtime_file_bytes(host, file)});
        }

        return result;
    }();

    return recipe;
}

namespace {
void need(bool value, const char *error) {
    if (!value) {
        throw std::runtime_error(error);
    }
}

void ordinary_directory(const std::filesystem::path &path) {
    need(path.is_absolute() && path == path.lexically_normal() &&
                 std::filesystem::is_directory(std::filesystem::symlink_status(path)) &&
                 std::filesystem::canonical(path) == path,
         "ANDROID_RENDERER_PATH: canonical ordinary directory required");
}

void link_directory(const std::filesystem::path &target, const std::filesystem::path &link) {
    std::error_code error;

    std::filesystem::create_directory_symlink(target, link, error);

    if (!error) {
        return;
    }

    // Junctions need no Developer Mode/symlink privilege on native Windows.
    if (!os::create_junction(target, link)) {
        throw VoyemuError("ANDROID_RENDERER_LINK", "directory link unavailable");
    }
}
} // namespace

static WindowsAndroidRenderer renderer_layout(const std::filesystem::path &sdk,
                                              const std::filesystem::path &private_directory,
                                              std::string_view inherited_path,
                                              const WindowsRendererRecipe &recipe, bool audit) {
    ordinary_directory(sdk);
    ordinary_directory(private_directory);
    const auto emulator = sdk / "emulator";
    ordinary_directory(emulator);
    need(recipe.sdk_archive_sha256.size() == 64 && !recipe.files.empty(),
         "ANDROID_RENDERER_PIN: pinned SDK recipe required");
    Json pins = Json::array();
    std::vector<Json> identities;

    for (const auto &pin : recipe.files) {
        const std::filesystem::path relative = pin.path;
        need(!relative.empty() && !relative.is_absolute() &&
                     relative == relative.lexically_normal(),
             "ANDROID_RENDERER_PIN: relative source required");

        for (const auto &part : relative) {
            need(part != "..", "ANDROID_RENDERER_PIN: foreign source forbidden");
        }

        const auto file = emulator / relative;
        need(std::filesystem::canonical(file) == file &&
                     std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) &&
                     std::filesystem::hard_link_count(file) == 1 &&
                     (!audit || std::filesystem::file_size(file) == pin.bytes),
             "ANDROID_RENDERER_PIN: ordinary SDK file required");
        const auto identity = cache_file_identity(file);

        if (audit) {
            need(sha256_file(file) == pin.sha256 && cache_file_identity(file) == identity,
                 "ANDROID_RENDERER_SHA: SDK renderer differs from pinned release");
        }

        identities.push_back(identity);
        pins.push_back({{"path", file.string()}, {"bytes", pin.bytes}, {"sha256", pin.sha256}});
    }

    constexpr std::array<std::pair<const char *, const char *>, 5> layout{
            {{"lib", "lib"},
             {"qemu", "qemu"},
             {"resources", "resources"},
             {"lib64/qt", "lib64/qt"},
             {"lib64/gles_swiftshader", "lib64/gles_angle"}}};

    for (const auto &[link, target] : layout) {
        (void)link;
        ordinary_directory(emulator / target);
    }

    const auto overlay = private_directory / "android-renderer";
    need(!std::filesystem::exists(overlay) && !std::filesystem::is_symlink(overlay),
         "ANDROID_RENDERER_OUTPUT: fresh private overlay required");
    ensure_private_directory(overlay / "lib64");
    // On failure remove only links we created, never recursively traverse targets.
    std::vector<std::filesystem::path> linked;
    try {
        for (const auto &[link, target] : layout) {
            link_directory(emulator / target, overlay / link);
            linked.push_back(overlay / link);
        }

        for (std::size_t i = 0; i < recipe.files.size(); ++i) {
            need(cache_file_identity(emulator / recipe.files[i].path) == identities[i],
                 "ANDROID_RENDERER_CHANGED: SDK changed during overlay creation");
        }
    } catch (...) {
        for (auto it = linked.rbegin(); it != linked.rend(); ++it) {
            std::filesystem::remove(*it);
        }

        std::filesystem::remove(overlay / "lib64");
        std::filesystem::remove(overlay);
        throw;
    }

    WindowsAndroidRenderer result;
    result.launcher_directory = overlay;
    result.gpu_mode = "swiftshader_indirect";
    result.features = {{"GLESDynamicVersion", "on"}, {"HostComposition", "on"}};
    const auto path = (emulator / "lib64/gles_angle").make_preferred().string() + ";" +
                      (emulator / "lib64").make_preferred().string() + ";" +
                      (emulator / "lib64/qt/lib").make_preferred().string() + ";" +
                      emulator.string() +
                      (inherited_path.empty() ? "" : ";" + std::string(inherited_path));
    const auto icd = (emulator / "lib64/gles_angle/vk_swiftshader_icd.json").make_preferred();
    result.environment = {{"ANDROID_EMULATOR_LAUNCHER_DIR", overlay.string()},
                          {"ANGLE_DEFAULT_PLATFORM", "vulkan"},
                          {"VK_DRIVER_FILES", icd.string()},
                          {"VK_ICD_FILENAMES", icd.string()},
                          {"PATH", path}};
    result.receipt = {{"schema", 1},
                      {"source", "pinned Windows SDK 36.3.10 build 14472402 private ANGLE layout"},
                      {"integrity_checked", audit},
                      {"sdk_archive_sha256", recipe.sdk_archive_sha256},
                      {"copied_sdk_bytes", 0},
                      {"launcher_directory", overlay.string()},
                      {"pins", pins},
                      {"gpu_mode", result.gpu_mode},
                      {"features", result.features}};
    write_private_json(overlay / "renderer.json", result.receipt);

    return result;
}

WindowsAndroidRenderer prepare_windows_android_renderer(const std::filesystem::path &sdk,
                                                        const std::filesystem::path &directory,
                                                        std::string_view inherited,
                                                        const WindowsRendererRecipe &recipe) {
    return renderer_layout(sdk, directory, inherited, recipe, true);
}

WindowsAndroidRenderer
prepare_windows_android_renderer_layout(const std::filesystem::path &sdk,
                                        const std::filesystem::path &directory,
                                        std::string_view inherited) {
    return renderer_layout(sdk, directory, inherited, windows_android_renderer_recipe(), false);
}
} // namespace voyemu
