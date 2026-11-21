#include "qnx_host.hpp"
#include "check.hpp"
#include "error.hpp"
#include "host_payload.hpp"
#include "host_refresh.hpp"
#include "process.hpp"
#include "qnx_host_prebuilt.hpp"
#include "qnx_host_windows_cache.hpp"
#include "qnx_persistent_storage.hpp"
#include "quickboot.hpp"
#ifndef _WIN32
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <array>
#include <fstream>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <msi.h>
#include <msiquery.h>
#include <setupapi.h>
#endif
namespace voyemu {
namespace {
#include "qnx_host_catalog.inc"

constexpr Check need{"QNX_HOST"};

// Strict parser for a host binding section: the Windows section passes the same schema,
// engine/ANGLE role and mechanics checks the macOS factory form applies in its own terms.
bool managed_windows_bindings(const Json &value) {
    if (!value.is_object() || value.size() != 6 || !value.contains("schema") ||
        !value.at("schema").is_number_integer() || value.at("schema") != 1) {
        return false;
    }

    for (const char *key : {"qnx_qemu", "graphics", "angle", "pins", "qnx_file_backend"}) {
        if (!value.contains(key)) {
            return false;
        }
    }

    for (const char *key : {"qnx_qemu", "graphics", "angle"}) {
        if (!value.at(key).is_string()) {
            return false;
        }
    }

    if (!value.at("pins").is_array() || value.at("pins").size() != 4) {
        return false;
    }

    // Interpret Windows separators independently of the reader host, retaining the original JSON
    // bytes.
    const auto lexical = [](std::string path) {
        std::replace(path.begin(), path.end(), '/', '\\');

        return path;
    };
    const auto qemu = lexical(value.at("qnx_qemu").get<std::string>()),
               graphics = lexical(value.at("graphics").get<std::string>()),
               angle = lexical(value.at("angle").get<std::string>());

    if (qemu.substr(qemu.find_last_of('\\') + 1) != "qemu-system-aarch64.exe") {
        return false;
    }

    const auto prefix = angle + (angle.empty() || angle.back() == '\\' ? "" : "\\");
    const std::set<std::string> expected{qemu, graphics, prefix + "libEGL.dll",
                                         prefix + "libGLESv2.dll"};
    std::set<std::string> seen;
    std::string engine;

    for (const auto &pin : value.at("pins")) {
        if (!pin.is_object() || pin.size() != 2 || !pin.contains("path") ||
            !pin.contains("sha256") || !pin.at("path").is_string() ||
            !pin.at("sha256").is_string()) {
            return false;
        }

        const auto path = lexical(pin.at("path").get<std::string>()),
                   digest = pin.at("sha256").get<std::string>();

        if (!expected.contains(path) || !seen.insert(path).second || !sha256_text(digest)) {
            return false;
        }

        if (path == qemu) {
            engine = digest;
        }
    }

    return seen == expected && !engine.empty() &&
           qnx_file_backend_supported(value.at("qnx_file_backend"), engine);
}

#ifdef _WIN32
struct MsiHandle {
    MSIHANDLE value = 0;

    ~MsiHandle() {
        if (value) {
            MsiCloseHandle(value);
        }
    }
};
struct CabinetOutput {
    std::filesystem::path directory;
    std::set<std::string> selected;
    bool failed = false;
};

UINT CALLBACK cabinet(void *context, UINT notice, UINT_PTR first, UINT_PTR) {
    auto &output = *static_cast<CabinetOutput *>(context);

    if (notice == SPFILENOTIFY_FILEINCABINET) {
        auto &file = *reinterpret_cast<FILE_IN_CABINET_INFO_W *>(first);
        const std::wstring name = file.NameInCabinet;
        std::string target;

        if (name == L"_7z.exe") {
            target = "7z.exe";
        } else if (name == L"_7z.dll") {
            target = "7z.dll";
        } else if (name == L"License.txt") {
            target = "License.txt";
        } else {
            return FILEOP_SKIP;
        }

        if (file.FileSize > 8 * 1024 * 1024 || !output.selected.insert(target).second) {
            output.failed = true;

            return FILEOP_ABORT;
        }

        const auto path = (output.directory / target).wstring();

        if (path.size() >= MAX_PATH || std::filesystem::exists(output.directory / target)) {
            output.failed = true;

            return FILEOP_ABORT;
        }

        std::copy(path.begin(), path.end(), file.FullTargetName);
        file.FullTargetName[path.size()] = 0;

        return FILEOP_DOIT;
    }

    if (notice == SPFILENOTIFY_FILEEXTRACTED) {
        auto &file = *reinterpret_cast<FILEPATHS_W *>(first);

        if (file.Win32Error) {
            output.failed = true;

            return file.Win32Error;
        }
    }

    return NO_ERROR;
}

Json tree(const std::filesystem::path &root) {
    Json files = Json::array();

    for (const auto &item : std::filesystem::recursive_directory_iterator(root)) {
        need(!item.is_symlink(), "linked installed payload rejected");

        if (item.is_directory()) {
            continue;
        }

        need(item.is_regular_file() && std::filesystem::hard_link_count(item.path()) == 1,
             "ordinary installed file required");

        if (item.path().filename() == "qnx-host.json") {
            continue;
        }

        files.push_back({{"path", item.path().lexically_relative(root).generic_string()},
                         {"bytes", item.file_size()},
                         {"sha256", sha256_file(item.path())}});
    }

    std::sort(files.begin(), files.end(), [](const Json &a, const Json &b) {
        return a.at("path").get<std::string>() < b.at("path").get<std::string>();
    });

    return files;
}
#endif
} // namespace

const Json &qnx_host_catalog() {
    static const Json value = Json::parse(std::begin(qnx_host_json), std::end(qnx_host_json));

    return value;
}

Json qnx_host_binding_view(const std::filesystem::path &root, const Json &shared) {
    (void)root;
#ifdef _WIN32
    if (shared.is_object() && shared.contains("windows")) {
        return shared.at("windows");
    }
#endif

    return shared;
}

#ifdef _WIN32
void extract_qnx_unpacker(const std::filesystem::path &msi,
                          const std::filesystem::path &destination) {
    need(std::filesystem::is_regular_file(std::filesystem::symlink_status(msi)) &&
                 std::filesystem::hard_link_count(msi) == 1 &&
                 sha256_file(msi) == qnx_host_catalog()
                                             .at("windows_x64")
                                             .at("extractor")
                                             .at("sha256")
                                             .get<std::string>(),
         "pinned extractor MSI required");
    need(!std::filesystem::exists(destination) &&
                 std::filesystem::is_directory(destination.parent_path()),
         "fresh unpacker destination required");
    std::filesystem::create_directory(destination);
    const auto cab = destination / "product.cab";
    try {
        MsiHandle database, view, record;
        need(MsiOpenDatabaseW(msi.c_str(), reinterpret_cast<LPCWSTR>(0), &database.value) ==
                     ERROR_SUCCESS,
             "MSI read-only open failed");
        need(MsiDatabaseOpenViewW(database.value,
                                  L"SELECT `Data` FROM `_Streams` WHERE `Name` = 'product.cab'",
                                  &view.value) == ERROR_SUCCESS &&
                     MsiViewExecute(view.value, 0) == ERROR_SUCCESS &&
                     MsiViewFetch(view.value, &record.value) == ERROR_SUCCESS,
             "MSI cabinet stream missing");
        std::ofstream output(cab, std::ios::binary);
        need(bool(output), "cabinet output failed");
        std::array<char, 65536> bytes;
        std::uint64_t total = 0;

        for (;;) {
            DWORD count = bytes.size();
            need(MsiRecordReadStream(record.value, 1, bytes.data(), &count) == ERROR_SUCCESS,
                 "MSI stream read failed");

            if (!count) {
                break;
            }

            total += count;
            need(total <= 8ULL << 20, "extractor cabinet exceeds bound");
            output.write(bytes.data(), count);
            need(bool(output), "cabinet write failed");
        }

        output.close();
        need(total > 0 && bool(output), "empty cabinet or flush failed");
        CabinetOutput selected{destination, {}, false};
        need(SetupIterateCabinetW(cab.c_str(), 0, cabinet, &selected) != 0 && !selected.failed &&
                     selected.selected == std::set<std::string>{"7z.exe", "7z.dll", "License.txt"},
             "extractor cabinet selection failed");
        std::filesystem::remove(cab);
    } catch (...) {
        std::filesystem::remove_all(destination);
        throw;
    }
}
#endif

std::string qnx_host_unavailable(const std::filesystem::path &root) {
#ifdef _WIN32
    // An engine of the source recipe that a developer or CI run left in the cache stays usable;
    // deps never runs that recipe on Windows.
    const auto &file_backend = qnx_host_catalog().at("windows_file_backend");
    const auto cache = root / ".local/toolchains/downloads/qnx-host/windows-file-backend-11.1.2";
    const auto installed_engine =
            root / ".local/toolchains" /
            ("qnx-host-windows-11.1.2-" + qnx_host_cache_key(sha256_bytes(file_backend.dump())));

    return qnx_host_asset_missing(qnx_host_prebuilt_pin(qnx_host_catalog(), "windows_file_backend"),
                                  std::filesystem::exists(cache) ||
                                          std::filesystem::exists(installed_engine));
#else
    (void)root;

    return {};
#endif
}

Json install_qnx_host(const std::filesystem::path &root, bool offline, std::uint64_t reserve) {
    need(root.is_absolute() && std::filesystem::canonical(root) == root,
         "canonical project root required");
#ifdef _WIN32
    const auto &file_backend = qnx_host_catalog().at("windows_file_backend");
    const auto prebuilt = qnx_host_prebuilt_pin(qnx_host_catalog(), "windows_file_backend");

    if (!prebuilt.is_null()) {
        return install_cached_windows_qnx_host(root, file_backend, reserve, prebuilt, offline);
    }

    if (const auto missing = qnx_host_unavailable(root); !missing.empty()) {
        throw VoyemuError("QNX_HOST_ASSET_MISSING", missing);
    }

    return install_cached_windows_qnx_host(root, file_backend, reserve);
#elif defined(__APPLE__)
    return install_qnx_host_macos(root, offline, reserve);
#else
    return install_qnx_host_linux(root, offline, reserve);
#endif
}

#ifndef _WIN32
namespace {
// The factory binding of the engine that deps installs on a POSIX host and of the ANGLE libraries
// of the pinned Emulator.
#ifdef __APPLE__
constexpr std::string_view factory_kind = "voyemu-macos-qnx-host", factory_recipe = "macos_arm64",
                           factory_engine = "qnx-host-macos-11.1.2-", angle_suffix = ".dylib";
#else
constexpr std::string_view factory_kind = "voyemu-linux-qnx-host", factory_recipe = "linux_x64",
                           factory_engine = "qnx-host-linux-11.1.2-", angle_suffix = ".so";
#endif

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
        need(managed_windows_bindings(inherited_windows), "Windows host binding section differs");
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
        need(managed_windows_bindings(value.at("windows")), "Windows host binding section differs");
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
#endif

Json ensure_qnx_host_bindings(const std::filesystem::path &root, bool offline) {
    const auto file = qnx_host_bindings_path(root);

#ifndef _WIN32
    FactoryBindingLock factory_lock(file.string() + ".factory.lock");
#endif

    if (std::filesystem::exists(file)) {
        const auto initial_identity = cache_file_identity(file);
        const auto initial_bytes = read_private_json_bytes(file, 128 * 1024);
        const auto initial_pin = sha256_bytes(initial_bytes);
        need(cache_file_identity(file) == initial_identity && sha256_file(file) == initial_pin,
             "host binding changed while reading");
        const auto previous = Json::parse(initial_bytes);
#ifndef _WIN32
        if (managed_macos_bindings(root, previous)) {
            auto identity = initial_identity;
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
#else
        const auto section = qnx_host_binding_view(root, previous);
        need(section.is_object() && managed_windows_bindings(section),
             "Windows host binding section missing from the shared binding file");

        return section;
#endif
    }

#ifdef _WIN32
    const auto engine = install_qnx_host(root, offline);
    const auto qemu = std::filesystem::path(engine.at("executable").get<std::string>());
    const auto graphics = host_program(root),
               angle = root / ".local/toolchains/native-sdk/emulator/lib64/gles_angle";
    Json pins = Json::array();

    for (const auto &tool : {qemu, graphics, angle / "libEGL.dll", angle / "libGLESv2.dll"}) {
        need(std::filesystem::is_regular_file(std::filesystem::symlink_status(tool)) &&
                     std::filesystem::hard_link_count(tool) == 1,
             "build host worker and run deps before source build");
        pins.push_back({{"path", tool.string()}, {"sha256", sha256_file(tool)}});
    }

    Json bindings{{"schema", 1},
                  {"qnx_qemu", qemu.string()},
                  {"graphics", graphics.string()},
                  {"angle", angle.string()},
                  {"pins", pins}};

    if (engine.contains("qnx_file_backend")) {
        bindings["qnx_file_backend"] = engine.at("qnx_file_backend");
    }

    need(managed_windows_bindings(bindings), "prepared Windows host binding differs");
    ensure_private_directory(file.parent_path());
    need(!std::filesystem::exists(file), "bindings appeared during preparation");
    write_private_json(file, Json{{"schema", 1}, {"windows", bindings}});

    return bindings;
#else
    const auto bindings = macos_factory_bindings(root, install_qnx_host(root, offline));
    ensure_private_directory(file.parent_path());
    need(!std::filesystem::exists(file), "bindings appeared during preparation");
    write_private_json(file, bindings);

    return bindings;
#endif
}
} // namespace voyemu
