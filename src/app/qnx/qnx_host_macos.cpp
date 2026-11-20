#include "check.hpp"
#include "deps.hpp"
#include "download.hpp"
#include "error.hpp"
#include "owner_lock.hpp"
#include "process.hpp"
#include "qnx_host.hpp"
#include "qnx_host_build_diagnostics.hpp"
#include "qnx_host_prebuilt.hpp"
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>
namespace voyemu {
namespace {
constexpr Check require{"QNX_HOST"};

void text(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path);

    file << value;
    require(bool(file), "write failed: " + path.string());
}
} // namespace

Json install_qnx_host_macos(const std::filesystem::path &root, bool offline, std::uint64_t reserve,
                            bool from_source) {
#if !defined(__aarch64__) && !defined(__arm64__)
    (void)root;
    (void)offline;
    (void)reserve;
    (void)from_source;
    throw VoyemuError("QNX_HOST", "pinned macOS engine requires Apple Silicon");
#else
    const auto &component = qnx_host_catalog().at("macos_arm64");
    const auto prebuilt = qnx_host_prebuilt_pin(qnx_host_catalog(), "macos_arm64");

    if (!from_source && !prebuilt.is_null()) {
        return install_prebuilt_qnx_host(root, component, prebuilt, "macos", offline, reserve);
    }

    const auto pin = sha256_bytes(component.dump());
    const auto parent = root / ".local/toolchains",
               target = parent / ("qnx-host-macos-11.1.2-" + pin.substr(0, 12)),
               binary = target / "qemu-system-aarch64";
    auto result = [&](bool cached) {
        return Json{{"schema", 1},
                    {"ready", true},
                    {"cached", cached},
                    {"executable", binary.string()},
                    {"directory", target.string()},
                    {"version", component.at("version")},
                    {"guest_verified", false}};
    };

    if (std::filesystem::exists(target)) {
        require(std::filesystem::is_regular_file(std::filesystem::symlink_status(binary)) &&
                        std::filesystem::hard_link_count(binary) == 1,
                "ordinary installed engine required");
        const auto receipt = read_private_json(target / "qnx-host.json");
        require(receipt.at("component_sha256") == pin &&
                        receipt.at("executable_sha256") == sha256_file(binary),
                "installed engine differs from receipt");

        return result(true);
    }

    ensure_private_directory(parent);
    const auto lock = owner_lock(parent / "qnx-host.lock");
    require(lock->held(), "installation already owned");
    const auto cache = root / ".local/toolchains/downloads/qnx-host";
    auto download = [&](const Json &spec) {
        return download_pinned({spec.at("url").get<std::string>(),
                                spec.at("sha256").get<std::string>(), 512ULL << 20},
                               cache, offline, reserve)
                .file;
    };
    // Resolve every pinned input before allocating an expanded build tree.
    const auto source = download(component.at("source")),
               pkgconf = download(component.at("pkgconf"));
    std::map<std::string, std::filesystem::path> bottles;

    for (const auto &[name, dep] : component.at("dependencies").items()) {
        bottles[name] = download(dep.at("bottle"));
    }

    std::vector<std::filesystem::path> wheels;

    for (const auto &wheel : component.at("python_wheels")) {
        wheels.push_back(download(wheel));
    }

    const auto patch = root / component.at("patch").at("file").get<std::string>();
    require(std::filesystem::is_regular_file(patch), "source patch missing");
    const auto ninja = root / ".local/toolchains/native-sdk/cmake" /
                       embedded_toolchain_manifest().at("native").at("cmake").get<std::string>() /
                       "bin/ninja";
    require(std::filesystem::is_regular_file(ninja), "run native SDK deps before QNX preparation");
    require(std::filesystem::space(root).available >= reserve + (2ULL << 30),
            "insufficient build disk reserve");
    const auto token = random_token();
    const auto stage = root / ".local/build/tmp" / ("qnx-host-" + token);
    ensure_private_directory(stage);
    // Preserve bounded diagnostics on failure; expanded trees are always reclaimed.
    QnxHostBuildStage cleanup{stage, root / ".local/build" / ("qnx-host-" + token)};
    std::map<std::string, std::string> env{
            {"PATH", ninja.parent_path().string() + ":/usr/bin:/bin:/usr/sbin:/sbin"},
            {"TMPDIR", stage.string()},
            {"CC", "/usr/bin/clang"},
            {"CXX", "/usr/bin/clang++"},
            {"MACOSX_DEPLOYMENT_TARGET", component.at("minimum_macos").get<std::string>()},
            {"CFLAGS", ""},
            {"CXXFLAGS", ""},
            {"LDFLAGS", ""},
            {"PKG_CONFIG_PATH", ""},
            {"PYTHONPATH", (stage / "python").string()},
            // QEMU decodetree emits field assignments in Python set iteration order.
            {"PYTHONHASHSEED", "0"},
            {"PIP_NO_INDEX", "1"}};
    unsigned step = 0;
    auto run = [&](const std::filesystem::path &program, std::vector<std::string> args,
                   const std::filesystem::path &cwd, int timeout = 120000) {
        ProcessSpec spec;
        spec.executable = program;
        spec.arguments = std::move(args);
        spec.directory = cwd;
        spec.environment = env;
        spec.output = stage / (std::to_string(++step) + ".log");

        if (timeout > 300000) {
            auto child = ChildProcess::launch(spec);
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(timeout);

            while (child.alive()) {
                require(std::chrono::steady_clock::now() < deadline, "build deadline exceeded");
                require(std::filesystem::space(stage).available >= reserve,
                        "build disk reserve reached");
                require(!std::filesystem::exists(spec.output) ||
                                std::filesystem::file_size(spec.output) < (16ULL << 20),
                        "build log exceeds bound");
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            require(child.exit_code() == 0,
                    "build failed; diagnostics: " + cleanup.logs().string());

            return std::string{};
        }

        const auto response = execute(spec, timeout, 128 * 1024);
        text(spec.output, response.output);
        require(!response.timed_out && response.exit_code == 0,
                "build step " + std::to_string(step) +
                        " failed; diagnostics: " + cleanup.logs().string());

        return response.output;
    };
    run("/usr/bin/xcrun", {"--find", "clang"}, stage);
    run("/usr/bin/python3", {"-c", "import sys; assert sys.version_info >= (3,9)"}, stage);
    ensure_private_directory(stage / "python");

    for (const auto &wheel : wheels) {
        run("/usr/bin/unzip", {"-qo", wheel.string(), "-d", (stage / "python").string()}, stage);
    }

    for (const auto &[name, file] : bottles) {
        const auto output = stage / name;
        ensure_private_directory(output);
        run("/usr/bin/tar", {"-xf", file.string(), "-C", output.string()}, stage);
    }

    const auto deps = stage / "deps";
    ensure_private_directory(deps / "lib/pkgconfig");
    ensure_private_directory(deps / "include");

    for (const auto &[name, dep] : component.at("dependencies").items()) {
        const auto base = stage / name / name / dep.at("version").get<std::string>(),
                   lib = base / "lib" / dep.at("static_library").at("file").get<std::string>();
        require(sha256_file(lib) == dep.at("static_library").at("sha256").get<std::string>(),
                "static dependency differs: " + name);
        std::filesystem::copy_file(lib, deps / "lib" / lib.filename());
        std::filesystem::copy(base / "include", deps / "include",
                              std::filesystem::copy_options::recursive |
                                      std::filesystem::copy_options::overwrite_existing);

        if (name == "glib") {
            std::filesystem::copy(base / "lib/glib-2.0", deps / "lib/glib-2.0",
                                  std::filesystem::copy_options::recursive);
        }
    }

    text(deps / "lib/pkgconfig/glib-2.0.pc",
         "prefix=" + deps.string() +
                 "\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: GLib\nDescription: pinned static GLib\nVersion: 2.90.0\nLibs: -L${libdir} -lglib-2.0 -lintl -lpcre2-8 -liconv -framework CoreFoundation -framework Foundation\nCflags: -I${includedir}/glib-2.0 -I${libdir}/glib-2.0/include\n");
    env["PKG_CONFIG_LIBDIR"] = (deps / "lib/pkgconfig").string();
    run("/usr/bin/tar", {"-xf", pkgconf.string(), "-C", stage.string()}, stage);
    const auto pkgsource = stage / "pkgconf-3.0.7", pkgprefix = stage / "pkgconf-bin";
    run(pkgsource / "configure",
        {"--prefix=" + pkgprefix.string(), "--disable-shared", "--enable-static"}, pkgsource);
    run("/usr/bin/make", {"-j3"}, pkgsource);
    run("/usr/bin/make", {"install"}, pkgsource);
    env["PKG_CONFIG"] = (pkgprefix / "bin/pkgconf").string();
    run("/usr/bin/tar",
        {"-xf", source.string(), "--exclude=roms/edk2/EmulatorPkg/Unix/Host/X11IncludeHack", "-C",
         stage.string()},
        stage);
    const auto sources = stage / "qemu-11.1.2", build = stage / "build";
    ensure_private_directory(build);
    run("/usr/bin/patch", {"--batch", "-p1", "-i", patch.string()}, sources);

    for (const auto &[file, sha] : component.at("patched_sources").items()) {
        require(sha256_file(sources / file) == sha.get<std::string>(),
                "patched source differs: " + file);
    }

    // Meson consumes a relocatable dependency location rather than a developer's Homebrew path.
    const auto native = sources / "configs/meson/darwin.txt";
    require(!std::filesystem::exists(native), "unexpected upstream native configuration");
    text(native, "[properties]\nvoyemu_fdt_static_dir = '" + (deps / "lib").string() + "'\n");
    run(sources / "configure",
        {"--target-list=aarch64-softmmu", "--without-default-features", "--enable-hvf",
         "--enable-tcg", "--enable-fdt=system",
         // The prefix map and the stripped debug map keep the build directory out of the engine.
         "--extra-cflags=-I" + (deps / "include").string() +
                 " -ffile-prefix-map=" + stage.string() + "=.",
         "--extra-ldflags=-Wl,-S", "--disable-download", "--disable-werror", "--disable-docs",
         "--disable-tools", "--disable-guest-agent", "--disable-user", "--disable-debug-info",
         "-Dbuildtype=release", "--python=/usr/bin/python3"},
        build, 300000);
    run(ninja, {"-j3", "qemu-system-aarch64"}, build, 1800000);
    const auto executable = build / "qemu-system-aarch64";
    run("/usr/bin/codesign", {"--verify", "--strict", executable.string()}, stage);
    const auto dylibs = run("/usr/bin/otool", {"-L", executable.string()}, stage);
    std::istringstream lines(dylibs);
    std::string line;
    std::getline(lines, line);

    while (std::getline(lines, line)) {
        const auto first = line.find_first_not_of(" \t");
        require(first != std::string::npos && (line.substr(first).starts_with("/usr/lib/") ||
                                               line.substr(first).starts_with("/System/Library/")),
                "non-system runtime dependency rejected");
    }

    require(run(executable, {"--version"}, stage).find("QEMU emulator version 11.1.2") !=
                    std::string::npos,
            "engine version differs");
    const auto payload = stage / "payload";
    ensure_private_directory(payload);
    std::filesystem::copy_file(executable, payload / "qemu-system-aarch64");
    std::filesystem::copy_file(sources / "COPYING", payload / "COPYING");
    ensure_private_directory(payload / "licenses");

    for (const auto &[name, dep] : component.at("dependencies").items()) {
        const auto base = stage / name / name / dep.at("version").get<std::string>();

        for (const auto *notice : {"COPYING", "LICENCE.md", "LGPL-2.1-or-later.txt"}) {
            if (std::filesystem::is_regular_file(base / notice)) {
                std::filesystem::copy_file(base / notice,
                                           payload / "licenses" / (name + "-" + notice));
            }
        }
    }

    std::filesystem::copy_file(sources / "COPYING.LIB", payload / "licenses/LGPL-2.1.txt");
    write_private_json(payload / "qnx-host.json", {{"schema", 1},
                                                   {"component_sha256", pin},
                                                   {"executable_sha256", sha256_file(executable)},
                                                   {"system_only_runtime", true},
                                                   {"recipe", component},
                                                   {"guest_verified", false}});
    require(!std::filesystem::exists(target), "destination appeared during build");
    std::filesystem::rename(payload, target);

    return result(false);
#endif
}
} // namespace voyemu
