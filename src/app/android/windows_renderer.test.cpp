#include "windows_renderer.hpp"
#include "process.hpp"
#include "toolchain_manifest.hpp"
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
using namespace voyemu;
namespace {
template <class F> void rejects(F action) {
    bool rejected = false;

    try {
        action();
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
}

void put(const std::filesystem::path &path, std::string_view text) {
    std::ofstream file(path, std::ios::binary);

    file << text;
}

void unlink(const std::filesystem::path &root) {
    for (const auto *name : {"lib", "qemu", "resources", "lib64/qt", "lib64/gles_swiftshader"}) {
        std::filesystem::remove(root / name);
    }
}
} // namespace

int main(int argc, char **argv) try {
    assert(!windows_android_renderer_policy(Json::object()));
    rejects([] {
        windows_android_renderer_policy(Json::object(), true);
    });
    rejects([] {
        windows_android_renderer_policy({{"rendererPolicy", false}});
    });
    rejects([] {
        windows_android_renderer_policy({{"rendererPolicy", "windows-angle-tampered"}});
    });
    const Json policy = {{"rendererPolicy", "windows-angle-36.3.10"}};
    assert(windows_android_renderer_policy(policy) &&
           windows_android_renderer_policy(policy, true));

    for (const auto *argument : {"-gpu", "-gpu=host", "-feature", "-feature=-HostComposition"}) {
        auto overridden = policy;
        overridden["args"] = Json::array({argument});
        bool rejected = false;
        try {
            windows_android_renderer_policy(overridden);
        } catch (const std::exception &error) {
            rejected = std::string_view(error.what()).starts_with("ANDROID_RENDERER_POLICY:");
        }

        assert(rejected);
    }

    assert(!windows_android_renderer_policy({{"args", Json::array({"-gpu", "host"})}}));
    auto valid = policy;
    valid["args"] = Json::array({"-no-metrics"});
    assert(windows_android_renderer_policy(valid));
    // The recipe takes the renderer files of the pinned Windows Emulator archive from
    // toolchain.json; a file without a pinned size is rejected.
    const auto &pinned = windows_android_renderer_recipe();
    assert(embedded_toolchain_manifest()
                           .at("sdk")
                           .at("archives")
                           .at("win32")
                           .at("emulator")
                           .at("sha256") == pinned.sdk_archive_sha256 &&
           pinned.files.size() == 7);

    for (const auto &pin : pinned.files) {
        assert(pin.sha256.size() == 64 &&
               pin.sha256.find_first_not_of("0123456789abcdef") == std::string::npos &&
               pin.bytes > 0 && !pin.path.starts_with("emulator/"));
    }

    rejects([] {
        native_runtime_file_bytes("win32/x64", "emulator/bin64/e2fsck.exe");
    });
    assert(argc == 2 || argc == 3);
    ensure_private_directory(std::filesystem::path(argv[1]));
    const auto base = std::filesystem::canonical(argv[1]) / ("renderer-fixture-" + random_token());
    ensure_private_directory(base);
    const auto sdk = base / "sdk";

    for (const auto *name : {"lib", "qemu", "resources", "lib64/qt", "lib64/gles_angle"}) {
        ensure_private_directory(sdk / "emulator" / name);
    }

    const auto source = sdk / "emulator/lib64/gles_angle/fixture.dll";
    put(source, "unchanged SDK fixture");
    WindowsRendererRecipe recipe{std::string(64, 'a'),
                                 {{"lib64/gles_angle/fixture.dll", sha256_file(source),
                                   std::filesystem::file_size(source)}}};
    const auto session = base / "session";
    ensure_private_directory(session);
    const auto result = prepare_windows_android_renderer(sdk, session, "caller-path", recipe);
    assert(result.gpu_mode == "swiftshader_indirect" &&
           result.features.at("HostComposition") == "on" &&
           result.features.at("GLESDynamicVersion") == "on");
    assert(result.receipt.at("copied_sdk_bytes") == 0);
    assert(result.environment.at("ANGLE_DEFAULT_PLATFORM") == "vulkan");
    assert(result.environment.at("PATH").starts_with(
                   (sdk / "emulator/lib64/gles_angle").make_preferred().string() + ";") &&
           result.environment.at("PATH").ends_with(";caller-path"));
    assert(std::filesystem::equivalent(
            result.launcher_directory / "lib64/gles_swiftshader/fixture.dll", source));
    assert(sha256_file(source) == recipe.files[0].sha256);
    assert(read_private_json(result.launcher_directory / "renderer.json") == result.receipt);
    rejects([&] {
        prepare_windows_android_renderer(sdk, session, "", recipe);
    });
    auto failure = base / "failure";
    ensure_private_directory(failure);
    put(source, std::string(recipe.files[0].bytes, 'x'));
    rejects([&] {
        prepare_windows_android_renderer(sdk, failure, "", recipe);
    });
    assert(!std::filesystem::exists(failure / "android-renderer"));
    put(source, "unchanged SDK fixture");
    auto bad = recipe;
    bad.files[0].path = "../outside";
    rejects([&] {
        prepare_windows_android_renderer(sdk, failure, "", bad);
    });
    const auto alias = source.parent_path() / "alias";
    std::filesystem::create_hard_link(source, alias);
    rejects([&] {
        prepare_windows_android_renderer(sdk, failure, "", recipe);
    });
    std::filesystem::remove(alias);
    std::filesystem::rename(source, alias);
    std::error_code link_error;
    std::filesystem::create_symlink(alias, source, link_error);

    if (!link_error) {
        rejects([&] {
            prepare_windows_android_renderer(sdk, failure, "", recipe);
        });
        std::filesystem::remove(source);
    } else {
        std::cout << "SKIP renderer file-symlink fixture: host privilege unavailable\n";
    }

    std::filesystem::rename(alias, source);

    if (argc == 3) {
        const auto actual = base / "actual-session";
        ensure_private_directory(actual);
        const auto real = prepare_windows_android_renderer(std::filesystem::canonical(argv[2]),
                                                           actual);
        assert(real.receipt.at("pins").size() == 7);
        std::cout << real.receipt.dump() << '\n';
        unlink(real.launcher_directory);
    }

    unlink(result.launcher_directory);
    std::filesystem::remove_all(base);
    std::cout
            << "Renderer: pinned inputs, five private links, no SDK copies, exact PATH/features, changed/hardlinked/foreign inputs and stale overlay refusal passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
