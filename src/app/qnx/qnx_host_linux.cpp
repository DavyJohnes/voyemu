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
#include <cstring>
#include <fstream>
#include <set>
#include <thread>
namespace voyemu {
namespace {
constexpr Check require{"QNX_HOST"};

void text(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path);

    file << value;
    require(bool(file), "write failed: " + path.string());
}

std::string quoted(const std::string &value) {
    std::string result = "'";

    for (const char c : value) {
        result += c == '\'' ? std::string("'\\''") : std::string(1, c);
    }

    return result + "'";
}

void executable(const std::filesystem::path &path) {
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_all |
                                         std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec,
                                 std::filesystem::perm_options::replace);
}
} // namespace

std::vector<std::string> qnx_host_elf_needed(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    const auto read = [&](std::uint64_t at, unsigned size) {
        require(at <= bytes.size() && bytes.size() - at >= size, "ELF extent");
        std::uint64_t value = 0;
        std::memcpy(&value, bytes.data() + at, size);

        return value;
    };

    require(bytes.size() > 64 &&
                    bytes.compare(0, 4,
                                  "\x7f"
                                  "ELF") == 0 &&
                    bytes[4] == 2 && bytes[5] == 1 && read(18, 2) == 62,
            "x86-64 ELF executable required");
    const auto phoff = read(32, 8), phentsize = read(54, 2), phnum = read(56, 2);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> dynamic;
    std::uint64_t strtab = 0, strsz = 0;
    std::vector<std::uint64_t> needed;
    const auto offset = [&](std::uint64_t address) {
        for (std::uint64_t i = 0; i < phnum; ++i) {
            const auto header = phoff + i * phentsize;

            if (read(header, 4) == 1) {
                const auto vaddr = read(header + 16, 8), filesz = read(header + 32, 8);

                if (address >= vaddr && address - vaddr < filesz) {
                    return read(header + 8, 8) + (address - vaddr);
                }
            }
        }

        throw VoyemuError("QNX_HOST", "ELF address outside the file");
    };

    for (std::uint64_t i = 0; i < phnum; ++i) {
        const auto header = phoff + i * phentsize;

        if (read(header, 4) != 2) {
            continue;
        }

        const auto start = read(header + 8, 8), size = read(header + 32, 8);

        for (std::uint64_t at = start; at + 16 <= start + size; at += 16) {
            const auto tag = read(at, 8), value = read(at + 8, 8);

            if (tag == 0) {
                break;
            }

            if (tag == 1) {
                needed.push_back(value);
            } else if (tag == 5) {
                strtab = value;
            } else if (tag == 10) {
                strsz = value;
            }
        }
    }

    std::vector<std::string> names;

    if (needed.empty()) {
        return names;
    }

    require(strtab && strsz, "ELF string table missing");
    const auto table = offset(strtab);

    for (const auto value : needed) {
        require(value < strsz, "ELF string extent");
        const auto at = table + value;
        const auto end = bytes.find('\0', at);
        require(end != std::string::npos && end < table + strsz, "ELF string terminator");
        names.push_back(bytes.substr(at, end - at));
    }

    return names;
}

Json install_qnx_host_linux(const std::filesystem::path &root, bool offline, std::uint64_t reserve,
                            bool from_source) {
#if !defined(__linux__) || !defined(__x86_64__)
    (void)root;
    (void)offline;
    (void)reserve;
    (void)from_source;
    throw VoyemuError("QNX_HOST", "pinned Linux engine requires Linux x64");
#else
    const auto &component = qnx_host_catalog().at("linux_x64");
    const auto prebuilt = qnx_host_prebuilt_pin(qnx_host_catalog(), "linux_x64");

    if (!from_source && !prebuilt.is_null()) {
        return install_prebuilt_qnx_host(root, component, prebuilt, "linux", offline, reserve);
    }

    const auto pin = sha256_bytes(component.dump());
    const auto parent = root / ".local/toolchains",
               target = parent / ("qnx-host-linux-11.1.2-" + pin.substr(0, 12)),
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
               pkgconf = download(component.at("pkgconf")),
               zig = download(component.at("compiler").at("linux_x64"));
    std::map<std::string, std::filesystem::path> archives;

    for (const auto &[name, dep] : component.at("dependencies").items()) {
        archives[name] = download(dep);
    }

    std::vector<std::filesystem::path> wheels;

    for (const auto &wheel : component.at("python_wheels")) {
        wheels.push_back(download(wheel));
    }

    const std::filesystem::path python = component.at("python").get<std::string>(),
                                make = component.at("make").get<std::string>();
    require(std::filesystem::is_regular_file(python) && std::filesystem::is_regular_file(make),
            "host Python 3 and make required to build the QNX engine");
    const auto ninja = root / ".local/toolchains/native-sdk/cmake" /
                       embedded_toolchain_manifest().at("native").at("cmake").get<std::string>() /
                       "bin/ninja";
    require(std::filesystem::is_regular_file(ninja), "run native SDK deps before QNX preparation");
    require(std::filesystem::space(root).available >= reserve + (3ULL << 30),
            "insufficient build disk reserve");
    const auto token = random_token();
    const auto stage = root / ".local/build/tmp" / ("qnx-host-" + token);
    ensure_private_directory(stage);
    // Preserve bounded diagnostics on failure; expanded trees are always reclaimed.
    QnxHostBuildStage cleanup{stage, root / ".local/build" / ("qnx-host-" + token)};
    const auto tools = stage / "tools", sysroot = stage / "root", prefix = sysroot / "usr",
               zigcache = stage / "zig-cache";
    ensure_private_directory(tools);
    ensure_private_directory(zigcache);
    std::map<std::string, std::string> env{
            {"PATH", tools.string() + ":" + ninja.parent_path().string() + ":/usr/bin:/bin"},
            {"TMPDIR", stage.string()},
            {"CC", (tools / "cc").string()},
            {"CXX", (tools / "c++").string()},
            {"AR", (tools / "ar").string()},
            {"RANLIB", (tools / "ranlib").string()},
            {"CFLAGS", "-O2"},
            {"CXXFLAGS", "-O2"},
            {"CPPFLAGS", ""},
            {"LDFLAGS", ""},
            {"PKG_CONFIG_PATH", ""},
            {"PKG_CONFIG_LIBDIR", (prefix / "lib/pkgconfig").string()},
            // Dependencies install under the fixed prefix /usr into the stage, so no stage path is
            // compiled into them; pkgconf maps their paths back into the stage.
            {"PKG_CONFIG_SYSROOT_DIR", sysroot.string()},
            {"PYTHONPATH", (stage / "python").string()},
            {"PYTHONHASHSEED", "0"},
            {"PYTHONDONTWRITEBYTECODE", "1"},
            {"PIP_NO_INDEX", "1"},
            {"SOURCE_DATE_EPOCH", "0"},
            {"ZIG_GLOBAL_CACHE_DIR", zigcache.string()},
            {"ZIG_LOCAL_CACHE_DIR", zigcache.string()}};
    unsigned step = 0;
    auto run = [&](const std::filesystem::path &program, std::vector<std::string> args,
                   const std::filesystem::path &cwd, int timeout = 300000) {
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

        const auto response = execute(spec, timeout, 1024 * 1024);
        text(spec.output, response.output);
        require(!response.timed_out && response.exit_code == 0,
                "build step " + std::to_string(step) +
                        " failed; diagnostics: " + cleanup.logs().string());

        return response.output;
    };
    // The shell runs one build step so that configure, make and the compiler wrappers see the
    // same environment.
    auto shell = [&](const std::string &script, const std::filesystem::path &cwd,
                     int timeout = 300000) {
        return run("/bin/sh", {"-euc", script}, cwd, timeout);
    };
    run(python, {"-c", "import sys, venv; assert sys.version_info >= (3, 9)"}, stage);
    run("/usr/bin/tar", {"-xf", zig.string(), "-C", stage.string()}, stage);
    const auto compiler = stage / "zig-x86_64-linux-0.15.2/zig";
    const auto triple = component.at("compiler").at("target").get<std::string>();
    // The prefix map keeps the build directory out of every object.
    auto flags = "-target " + triple + " -ffile-prefix-map=" + stage.string() + "=.";

    for (const auto &flag : component.at("compiler").at("cflags")) {
        flags += " " + flag.get<std::string>();
    }

    for (const auto &[name, command] : std::map<std::string, std::string>{
                 {"cc", "cc " + flags}, {"c++", "c++ " + flags}, {"ranlib", "ranlib"}}) {
        text(tools / name,
             "#!/bin/sh\nexec " + quoted(compiler.string()) + " " + command + " \"$@\"\n");
        executable(tools / name);
    }

    // Meson links internal libraries of GLib as thin archives, which the Zig linker cannot read.
    text(tools / "ar", "#!/bin/sh\nmode=$(printf %s \"$1\" | tr -d T)\nshift\nexec " +
                               quoted(compiler.string()) + " ar \"$mode\" \"$@\"\n");
    executable(tools / "ar");

    ensure_private_directory(stage / "python");

    for (const auto &wheel : wheels) {
        run(python, {"-m", "zipfile", "-e", wheel.string(), (stage / "python").string()}, stage);
    }

    run("/usr/bin/tar", {"-xf", source.string(), "-C", stage.string()}, stage);
    const auto sources = stage / "qemu-11.1.2", build = stage / "build";
    // GLib builds with the Meson that QEMU bundles.
    run(python,
        {"-m", "zipfile", "-e", (sources / "python/wheels/meson-1.11.1-py3-none-any.whl").string(),
         (stage / "python").string()},
        stage);

    for (const auto &[name, file] : archives) {
        run("/usr/bin/tar", {"-xf", file.string(), "-C", stage.string()}, stage);
    }

    run("/usr/bin/tar", {"-xf", pkgconf.string(), "-C", stage.string()}, stage);
    const auto pkgsource = stage / "pkgconf-3.0.7", pkgprefix = stage / "pkgconf-bin";
    shell("./configure --prefix=" + quoted(pkgprefix.string()) +
                  " --disable-shared --enable-static\n" + quoted(make.string()) + " -j2\n" +
                  quoted(make.string()) + " install",
          pkgsource, 900000);
    env["PKG_CONFIG"] = (pkgprefix / "bin/pkgconf").string();
    const auto version = [&](const std::string &name) {
        return component.at("dependencies").at(name).at("version").get<std::string>();
    };
    const auto destdir = " DESTDIR=" + quoted(sysroot.string());
    ensure_private_directory(prefix / "lib/pkgconfig");
    ensure_private_directory(prefix / "include");
    // zlib and libfdt compile from their file lists, as the Windows recipe does for libfdt.
    std::string commands;

    for (const auto *name :
         {"adler32", "compress", "crc32", "deflate", "gzclose", "gzlib", "gzread", "gzwrite",
          "infback", "inffast", "inflate", "inftrees", "trees", "uncompr", "zutil"}) {
        commands += "cc -O2 -DHAVE_UNISTD_H -c " + std::string(name) + ".c -o " + name + ".o\n";
    }

    commands += "ar rcs " + quoted((prefix / "lib/libz.a").string()) + " *.o\ncp zlib.h zconf.h " +
                quoted((prefix / "include").string()) + "\n";
    shell(commands, stage / ("zlib-" + version("zlib")));
    text(prefix / "lib/pkgconfig/zlib.pc",
         "prefix=/usr\nlibdir=${prefix}/lib\nincludedir=${prefix}/include\nName: zlib\nDescription: pinned zlib\nVersion: " +
                 version("zlib") + "\nLibs: -L${libdir} -lz\nCflags: -I${includedir}\n");
    commands.clear();

    for (const auto *name : {"fdt", "fdt_ro", "fdt_wip", "fdt_sw", "fdt_rw", "fdt_strerror",
                             "fdt_empty_tree", "fdt_addresses", "fdt_overlay", "fdt_check"}) {
        commands += "cc -O2 -I. -c " + std::string(name) + ".c -o " + name + ".o\n";
    }

    commands += "ar rcs " + quoted((prefix / "lib/libfdt.a").string()) +
                " *.o\ncp libfdt.h libfdt_env.h fdt.h " + quoted((prefix / "include").string()) +
                "\n";
    shell(commands, stage / ("dtc-" + version("dtc")) / "libfdt");
    shell("./configure --prefix=/usr --libdir=/usr/lib --disable-shared --enable-static "
          "--disable-pcre2grep-libz --disable-pcre2grep-libbz2\n" +
                  quoted(make.string()) + " -j2 libpcre2-8.la\n" + quoted(make.string()) +
                  " install-libLTLIBRARIES install-nodist_includeHEADERS install-pkgconfigDATA" +
                  destdir,
          stage / ("pcre2-" + version("pcre2")), 900000);
    shell("./configure --prefix=/usr --libdir=/usr/lib --disable-shared --enable-static "
          "--disable-docs --disable-multi-os-directory --with-pic\n" +
                  quoted(make.string()) + " -j2\n" + quoted(make.string()) + " install" + destdir,
          stage / ("libffi-" + version("libffi")), 900000);
    const auto glib = stage / ("glib-" + version("glib")), glibbuild = stage / "glib-build";
    run(python,
        {"-m",
         "mesonbuild.mesonmain",
         "setup",
         glibbuild.string(),
         "--prefix=/usr",
         "--libdir=lib",
         "--buildtype=release",
         "--default-library=static",
         "--wrap-mode=nodownload",
         "-Dtests=false",
         "-Dintrospection=disabled",
         "-Dnls=disabled",
         "-Dlibmount=disabled",
         "-Dselinux=disabled",
         "-Dxattr=false",
         "-Dman-pages=disabled",
         "-Ddocumentation=false",
         "-Ddtrace=disabled",
         "-Dsystemtap=disabled",
         "-Dsysprof=disabled",
         "-Dlibelf=disabled",
         "-Dinstalled_tests=false",
         "-Dbsymbolic_functions=false"},
        glib, 600000);
    run(ninja, {"-C", glibbuild.string(), "-j2"}, glib, 3600000);
    run(python,
        {"-m", "mesonbuild.mesonmain", "install", "-C", glibbuild.string(), "--no-rebuild",
         "--destdir", sysroot.string()},
        glib, 600000);
    ensure_private_directory(build);
    // Zig searches its own older Linux headers before every -isystem directory and drops a -I that
    // names one, so a copy of the kernel headers that QEMU bundles comes first through -I.
    const auto kernel = stage / "kernel-headers";
    ensure_private_directory(kernel);

    for (const auto &[from, to] : std::map<std::string, std::string>{
                 {"linux", "linux"}, {"asm-generic", "asm-generic"}, {"asm-x86", "asm"}}) {
        std::filesystem::copy(sources / "linux-headers" / from, kernel / to,
                              std::filesystem::copy_options::recursive);
    }

    std::vector<std::string> configure;

    for (const auto &arg : component.at("configure")) {
        configure.push_back(arg.get<std::string>());
    }

    configure.insert(configure.end(),
                     {"--cc=" + (tools / "cc").string(), "--cxx=" + (tools / "c++").string(),
                      "--python=" + python.string(),
                      "--extra-cflags=-I" + (prefix / "include").string() + " -I" + kernel.string(),
                      "--extra-ldflags=-L" + (prefix / "lib").string() + " -s",
                      "-Dbuildtype=release"});
    run(sources / "configure", configure, build, 600000);
    run(ninja, {"-j2", "qemu-system-aarch64"}, build, 7200000);
    const auto engine = build / "qemu-system-aarch64";
    std::set<std::string> allowed;

    for (const auto &name : component.at("system_libraries")) {
        allowed.insert(name.get<std::string>());
    }

    for (const auto &name : qnx_host_elf_needed(engine)) {
        require(allowed.contains(name), "non-system runtime dependency rejected: " + name);
    }

    require(run(engine, {"--version"}, stage).find("QEMU emulator version 11.1.2") !=
                    std::string::npos,
            "engine version differs");
    require(run(engine, {"-object", "help"}, stage).find("memory-backend-file") !=
                    std::string::npos,
            "engine file backend missing");
    const auto payload = stage / "payload";
    ensure_private_directory(payload / "licenses");
    std::filesystem::copy_file(engine, payload / "qemu-system-aarch64");
    std::filesystem::copy_file(sources / "COPYING", payload / "COPYING");
    std::filesystem::copy_file(sources / "COPYING.LIB", payload / "licenses/LGPL-2.1.txt");

    for (const auto &[name, dep] : component.at("dependencies").items()) {
        for (const auto &notice : dep.at("notices")) {
            const auto file = notice.get<std::string>();
            std::filesystem::copy_file(stage / (name + "-" + version(name)) / file,
                                       payload / "licenses" / (name + "-" + file));
        }
    }

    write_private_json(payload / "qnx-host.json", {{"schema", 1},
                                                   {"component_sha256", pin},
                                                   {"executable_sha256", sha256_file(engine)},
                                                   {"system_only_runtime", true},
                                                   {"recipe", component},
                                                   {"guest_verified", false}});
    require(!std::filesystem::exists(target), "destination appeared during build");
    std::filesystem::rename(payload, target);

    return result(false);
#endif
}
} // namespace voyemu
