#include "media_discovery.hpp"
#include "model.hpp"
#include "process.hpp"
#include <cassert>
#include <exception>
#include <iostream>
using namespace voyemu;

static void warm_sdk_descriptor_binding() {
    const auto root = std::filesystem::current_path() / ".local/build/tmp" /
                      ("voyemu-sdk-warm-" + random_token());

    ensure_private_directory(root);
    const auto saved = root / "private";
    ensure_private_directory(saved);
    const auto log = root / "android.log", source = root / "pid_987.ini",
               target = saved / source.filename();
    const std::string descriptor =
            "grpc.port=45992\nport.serial=5566\ngrpc.token=private-fixture-token\n";
    {
        std::ofstream(source) << descriptor;
        std::ofstream(log) << "Advertising in: " << source.string() << '\n';
    }
    SdkDescriptorCache cache;
    assert(!cache.authenticated() && cache.bearer().empty());
    assert(cache.resolve(log, root, saved, 987, 5566, 45992, false) == target &&
           cache.authenticated() && cache.bearer() == "private-fixture-token");
    { std::ofstream(log, std::ios::app) << std::string(600000, 'x') << '\n'; }
    assert(std::filesystem::file_size(log) > 512 * 1024 && !sdk_discovery(log, root, 987));
    // A guest warm reboot keeps this SDK launch, but its nonce must be verified again.
    assert(!cache.resolve(log, root, saved, 987, 5566, 45992, false));
    assert(cache.resolve(log, root, saved, 987, 5566, 45992, true) == target);
    auto rejected = [&](unsigned pid, unsigned serial, unsigned grpc) {
        bool blocked = false;
        try {
            cache.resolve(log, root, saved, pid, serial, grpc, true);
        } catch (const std::exception &) {
            blocked = true;
        }

        assert(blocked);
    };
    rejected(988, 5566, 45992);
    rejected(987, 5568, 45992);
    rejected(987, 5566, 45993);
    // Replacing the original source cannot replace the already authenticated token.
    std::filesystem::rename(source, root / "original");
    {
        auto changed = descriptor;
        changed.replace(changed.find("private-fixture-token"), 21, "changed-fixture-token");
        std::ofstream(source) << changed;
    }
    rejected(987, 5566, 45992);
    assert(cache.bearer() == "private-fixture-token");
    std::filesystem::remove(source);
    std::filesystem::rename(root / "original", source);
    assert(cache.resolve(log, root, saved, 987, 5566, 45992, true) == target);
    std::filesystem::create_hard_link(source, root / "alias");
    rejected(987, 5566, 45992);
    std::filesystem::remove(root / "alias");
#ifndef _WIN32
    std::filesystem::rename(source, root / "original");
    std::filesystem::create_symlink(root / "original", source);
    assert(!cache.resolve(log, root, saved, 987, 5566, 45992, true));
    std::filesystem::remove(source);
    std::filesystem::rename(root / "original", source);
    ::chmod(target.c_str(), 0644);
    rejected(987, 5566, 45992);
    ::chmod(target.c_str(), 0600);
#endif
    std::filesystem::create_hard_link(target, saved / "alias");
    rejected(987, 5566, 45992);
    std::filesystem::remove(saved / "alias");
    // Cold SDK launches, including PID reuse, cannot adopt a private copy without discovery.
    cache.reset();
    assert(!cache.authenticated() && cache.bearer().empty());
    assert(!cache.resolve(log, root, saved, 987, 5566, 45992, true) && !cache.authenticated());
    { std::ofstream(log) << "Advertising in: " << source.string() << '\n'; }
    assert(cache.resolve(log, root, saved, 987, 5566, 45992, false) == target);
    std::filesystem::remove(target);
    rejected(987, 5566, 45992);
    assert(!std::filesystem::exists(target));
    std::filesystem::remove_all(root);
}

int main() try {
    warm_sdk_descriptor_binding();
    const auto os_root = std::filesystem::absolute("sdk-user"),
               home = std::filesystem::absolute("project/emulator-home");
#ifdef __APPLE__
    assert(sdk_registration_directory(os_root, home) ==
           os_root / "Library/Caches/TemporaryItems/avd/running");
#elif defined(_WIN32)
    assert(sdk_registration_directory(os_root, home) == os_root / "Temp/avd/running");
#else
    assert(sdk_registration_directory(os_root, home) == os_root / "avd/running");
#endif
    assert(sdk_registration_directory({}, home) == home / "avd/running");
    bool relative_rejected = false;
    try {
        sdk_registration_directory("relative", home);
    } catch (...) {
        relative_rejected = true;
    }

    assert(relative_rejected);
    const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                      ("voyemu-sdk-discovery-" + random_token());
    ensure_private_directory(root);
    auto log = root / "android.log", descriptor = root / "pid_123.ini";
    { std::ofstream(descriptor) << "grpc.port=45992\n"; }
    auto advertise = [&](const std::filesystem::path &path) {
        std::ofstream(log) << "INFO | Advertising in: " << path.string() << '\n';
    };
    advertise(descriptor);
    assert(sdk_discovery(log, root, 123) == descriptor);
    assert(!sdk_discovery(log, root, 124));
    advertise(root / "../pid_123.ini");
    assert(!sdk_discovery(log, root, 123));
    {
        std::ofstream(log) << std::string(600000, 'x')
                           << "\nAdvertising in: " << descriptor.string() << '\n';
    }
    assert(sdk_discovery(log, root, 123) == descriptor);
    auto other = root / "other";
    ensure_private_directory(other);
    {
        std::ofstream(other / "pid_123.ini") << "grpc.port=45992\n";
        std::ofstream(log) << "Advertising in: " << descriptor.string()
                           << "\nAdvertising in: " << (other / "pid_123.ini").string() << '\n';
    }
    assert(!sdk_discovery(log, root, 123));
    advertise(descriptor);
    std::filesystem::resize_file(descriptor, 32769);
    assert(!sdk_discovery(log, root, 123));
    std::filesystem::resize_file(descriptor, 0);
    assert(!sdk_discovery(log, root, 123));
    { std::ofstream(descriptor) << "fixture"; }
    std::filesystem::create_hard_link(descriptor, root / "alias");
    assert(!sdk_discovery(log, root, 123));
    std::filesystem::remove(root / "alias");
#ifndef _WIN32
    std::filesystem::remove(descriptor);
    std::filesystem::create_symlink(other / "pid_123.ini", descriptor);
    assert(!sdk_discovery(log, root, 123));
#endif
    auto source = root / "pid_987.ini", target = other / "pid_987.ini";
    {
        std::ofstream(source)
                << "grpc.port=45992\nport.serial=5566\ngrpc.token=private-fixture-token\n";
    }
    assert(import_sdk_descriptor(source, target, 987, 5566, 45992) == target);
    assert(std::filesystem::exists(source));
    assert(import_sdk_descriptor(source, target, 987, 5566, 45992) ==
           target); // Guest reboot keeps the same SDK process and descriptor.
    {
        std::ofstream(source)
                << "grpc.port=45992\nport.serial=5566\ngrpc.token=changed-private-fixture-token\n";
    }
    bool blocked = false;
    try {
        import_sdk_descriptor(source, target, 987, 5566, 45992);
    } catch (const std::exception &) {
        blocked = true;
    }

    assert(blocked);
    {
        std::ofstream(source)
                << "grpc.port=45992\nport.serial=5566\ngrpc.token=private-fixture-token\n";
    }
#ifndef _WIN32
    ::chmod(target.c_str(), 0644);
    blocked = false;
    try {
        import_sdk_descriptor(source, target, 987, 5566, 45992);
    } catch (const std::exception &) {
        blocked = true;
    }

    assert(blocked);
    ::chmod(target.c_str(), 0600);
#endif
    std::filesystem::create_hard_link(target, other / "descriptor-alias");
    blocked = false;
    try {
        import_sdk_descriptor(source, target, 987, 5566, 45992);
    } catch (const std::exception &) {
        blocked = true;
    }

    assert(blocked);
    std::filesystem::remove(other / "descriptor-alias");
    std::filesystem::remove(target);
    blocked = false;
    try {
        import_sdk_descriptor(source, target, 987, 5566, 45993);
    } catch (const std::exception &) {
        blocked = true;
    }

    assert(blocked && !std::filesystem::exists(target));
    { std::ofstream(source) << "grpc.port=45992\nport.serial=5566\n"; }
    assert(!import_sdk_descriptor(source, target, 987, 5566, 45992));
#ifndef _WIN32
    {
        std::ofstream(source)
                << "grpc.port=45992\nport.serial=5566\ngrpc.token=private-fixture-token\n";
    }
    assert(import_sdk_descriptor(source, target, 987, 5566, 45992));
    struct stat permissions{};
    assert(::stat(target.c_str(), &permissions) == 0 && (permissions.st_mode & 0077) == 0);
#endif
    std::filesystem::remove_all(root);
    std::cout
            << "SDK discovery retained PID/project boundary/tail extent/alias/ambiguity guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
