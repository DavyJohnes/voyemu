#include "check.hpp"
#include "error.hpp"
#include "host_payload.hpp"
#include "host_refresh.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "qnx_host.hpp"
#include "qnx_host_internal.hpp"
#include "qnx_host_prebuilt.hpp"
#include "quickboot.hpp"
#include <array>
#include <fcntl.h>
#include <set>
#include <sys/file.h>
#include <unistd.h>
namespace voyemu {
namespace {
constexpr Check need{"QNX_HOST"};

// The factory binding of the engine that deps installs on a POSIX host and of the ANGLE libraries
// of the pinned Emulator.
constexpr bool macos = os::platform_name == "macOS";
constexpr std::string_view factory_kind = macos ? "voyemu-macos-qnx-host" : "voyemu-linux-qnx-host",
                           factory_recipe = macos ? "macos_arm64" : "linux_x64",
                           factory_engine = macos ? "qnx-host-macos-11.1.2-"
                                                  : "qnx-host-linux-11.1.2-",
                           angle_suffix = os::shared_library_suffix;

class FactoryBindingLock {
    int fd_ = -1;

public:
    explicit FactoryBindingLock(const std::filesystem::path &path) {
        ensure_private_directory(path.parent_path());
        fd_ = ::open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);

        if (fd_ < 0 || ::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            if (fd_ >= 0) {
                ::close(fd_);
            }

            fd_ = -1;
            throw VoyemuError("QNX_HOST", "factory binding already in preparation");
        }
    }

    ~FactoryBindingLock() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
};

void factory_file(const std::filesystem::path &file) {
    need(file.is_absolute() && std::filesystem::canonical(file) == file &&
                 std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) &&
                 std::filesystem::hard_link_count(file) == 1 &&
                 std::filesystem::file_size(file) <= (256ULL << 20),
         "ordinary bounded factory file required");
}

Json macos_factory_bindings(const std::filesystem::path &root, const Json &engine,
                            const Json &inherited_windows = Json()) {
    const auto qemu = std::filesystem::path(engine.at("executable").get<std::string>()),
               graphics = host_program(root),
               angle = root / ".local/toolchains/native-sdk/emulator/lib64/gles_angle",
               receipt = qemu.parent_path() / "qnx-host.json";
    const auto installed = read_private_json(receipt, 128 * 1024);
    const auto component = sha256_bytes(qnx_host_catalog().at(std::string(factory_recipe)).dump());

    need(installed.at("component_sha256") == component &&
                 sha256_bytes(installed.at("recipe").dump()) == component,
         "factory current recipe differs");
    Json pins = Json::array();

    for (const auto &tool : {qemu, graphics, angle / ("libEGL" + std::string(angle_suffix)),
                             angle / ("libGLESv2" + std::string(angle_suffix))}) {
        factory_file(tool);
        pins.push_back({{"path", tool.string()}, {"sha256", sha256_file(tool)}});
    }

    Json binding{{"schema", 1},
                 {"qnx_qemu", qemu.string()},
                 {"graphics", graphics.string()},
                 {"angle", angle.string()},
                 {"pins", pins},
                 {"factory",
                  {{"kind", factory_kind},
                   {"component_sha256", component},
                   {"engine_receipt_sha256", sha256_file(receipt)}}}};

    // The Windows section is inherited from the existing shared file; a macOS refresh never drops
    // it.
    if (!inherited_windows.is_null()) {
        need(qnx_host_managed_windows_bindings(inherited_windows),
             "Windows host binding section differs");
        binding["windows"] = inherited_windows;
    }

    return binding;
}

bool managed_macos_bindings(const std::filesystem::path &root, const Json &value) {
    if (!value.is_object() || !value.contains("factory")) {
        return false;
    }

    need(value.contains("qnx_qemu") && value.contains("graphics") && value.contains("angle") &&
                 value.contains("pins"),
         "factory binding fields missing");
    const std::filesystem::path qemu = value.at("qnx_qemu").get<std::string>(),
                                graphics = value.at("graphics").get<std::string>(),
                                angle = value.at("angle").get<std::string>();
    need(graphics == host_program(root) &&
                 angle == root / ".local/toolchains/native-sdk/emulator/lib64/gles_angle" &&
                 qemu.parent_path().parent_path() == root / ".local/toolchains" &&
                 qemu.filename() == "qemu-system-aarch64",
         "factory paths differ");
    need(value.size() == 6U + (value.contains("windows") ? 1U : 0U) && value.at("schema") == 1 &&
                 value.at("pins").is_array() && value.at("pins").size() == 4,
         "factory metadata differs");

    if (value.contains("windows")) {
        need(qnx_host_managed_windows_bindings(value.at("windows")),
             "Windows host binding section differs");
    }

    const auto receipt_file = qemu.parent_path() / "qnx-host.json";
    factory_file(receipt_file);
    const auto receipt = read_private_json(receipt_file, 128 * 1024);
    const auto component = receipt.value("component_sha256", std::string{});
    need(sha256_bytes(receipt.at("recipe").dump()) == component,
         "factory installed recipe identity differs");
    need(value.at("factory") == Json{{"kind", factory_kind},
                                     {"component_sha256", component},
                                     {"engine_receipt_sha256", sha256_file(receipt_file)}} &&
                 qemu.parent_path().filename() ==
                         std::string(factory_engine) +
                                 qnx_host_cache_key(component,
                                                    receipt.value("archive_sha256", std::string{})),
         "factory marker differs");

    factory_file(qemu);
    need(sha256_file(qemu) == receipt.at("executable_sha256").get<std::string>(),
         "factory installed executable differs");
    const std::set<std::string> expected{
            qemu.string(), graphics.string(),
            (angle / ("libEGL" + std::string(angle_suffix))).string(),
            (angle / ("libGLESv2" + std::string(angle_suffix))).string()};
    std::set<std::string> seen;

    for (const auto &pin : value.at("pins")) {
        const auto path = pin.at("path").get<std::string>(),
                   digest = pin.at("sha256").get<std::string>();
        need(pin.size() == 2 && expected.contains(path) && seen.insert(path).second &&
                     sha256_text(digest),
             "factory pin roles differ");

        if (path != graphics.string()) {
            factory_file(path);
            need(sha256_file(path) == digest, "factory nonself pin differs");
        }
    }

    need(seen == expected, "factory pin roles missing");

    return true;
}
} // namespace

Json qnx_host_binding_view(const std::filesystem::path &, const Json &shared) {
    return shared;
}

std::string qnx_host_unavailable(const std::filesystem::path &) {
    return {};
}

Json ensure_qnx_host_bindings(const std::filesystem::path &root, bool offline) {
    const auto file = qnx_host_bindings_path(root);

    FactoryBindingLock factory_lock(file.string() + ".factory.lock");

    if (std::filesystem::exists(file)) {
        const auto initial = read_qnx_host_binding_file(file);
        const auto &initial_bytes = initial.bytes, &initial_pin = initial.pin;
        const auto &previous = initial.document;

        if (managed_macos_bindings(root, previous)) {
            auto identity = initial.identity;
            const auto inherited = previous.contains("windows") ? previous.at("windows") : Json();
            const auto refreshed = macos_factory_bindings(root, install_qnx_host(root, offline),
                                                          inherited);
            need(cache_file_identity(file) == identity && sha256_file(file) == initial_pin,
                 "factory binding changed during preparation");

            if (refreshed != previous) {
                const auto saved = file.parent_path() /
                                   ("portable-host-" + initial_pin + ".previous.json");

                if (std::filesystem::exists(saved)) {
                    factory_file(saved);
                    need(sha256_file(saved) == initial_pin, "factory snapshot differs");
                } else {
                    write_private_json_bytes(saved, initial_bytes, 128 * 1024);
                    need(sha256_file(saved) == initial_pin, "factory snapshot copy differs");
                }

                const auto read = [&](const auto &target) {
                    const auto bytes = read_private_json_bytes(target, 128 * 1024);
                    need(cache_file_identity(target) == identity &&
                                 sha256_file(target) == sha256_bytes(bytes),
                         "factory binding identity/bytes changed");

                    return bytes;
                };
                const auto write = [&](const auto &target, const auto &bytes) {
                    write_private_json_bytes(target, bytes, 128 * 1024);
                    identity = cache_file_identity(target);
                };
                const auto validate = [&] {
                    need(macos_factory_bindings(root,
                                                Json{{"executable", refreshed.at("qnx_qemu")}},
                                                inherited) == refreshed,
                         "factory tools changed during publication");
                };
                publish_host_refresh(
                        std::array<HostRefreshRecord, 3>{
                                {{file, initial_bytes, refreshed.dump() + "\n"}, {}, {}}},
                        read, write, validate);
            }

            return refreshed;
        }

        return previous;
    }

    const auto bindings = macos_factory_bindings(root, install_qnx_host(root, offline));
    ensure_private_directory(file.parent_path());
    need(!std::filesystem::exists(file), "bindings appeared during preparation");
    write_private_json(file, bindings);

    return bindings;
}
} // namespace voyemu
