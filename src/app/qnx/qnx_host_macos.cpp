#include "check.hpp"
#include "deps.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_host.hpp"
#include "qnx_host_prebuilt.hpp"
#include "qnx_host_source.hpp"
#include <fstream>
#include <sstream>
namespace voyemu {
namespace {
constexpr Check require{"QNX_HOST"};
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

    const QnxHostSourceEngine host(root, component, "macos");

    if (host.installed()) {
        return host.result(true);
    }

    const auto lock = host.lock();
    auto download = [&](const Json &spec) {
        return host.download(spec, offline, reserve);
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
    QnxHostSourceSteps steps(root, reserve, 120000, 128 * 1024);
    const auto &stage = steps.stage();
    auto &env = steps.env;
    env = {{"PATH", ninja.parent_path().string() + ":/usr/bin:/bin:/usr/sbin:/sbin"},
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
    auto run = [&](const std::filesystem::path &program, std::vector<std::string> args,
                   const std::filesystem::path &cwd, int timeout = 120000) {
        return steps.run(program, std::move(args), cwd, timeout);
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

    qnx_host_text(
            deps / "lib/pkgconfig/glib-2.0.pc",
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
    qnx_host_text(native,
                  "[properties]\nvoyemu_fdt_static_dir = '" + (deps / "lib").string() + "'\n");
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
    return host.publish(payload, executable);
#endif
}
} // namespace voyemu
