#include "qnx_guest_build.hpp"
#include "error.hpp"
#include "process.hpp"
#include "qnx_imagefs.hpp"
#include "qnx_link_interface.hpp"
#include "qnx_lzo.hpp"
#include "qnx_passion.hpp"
#include "qnx_prepare.hpp"
#include "toolchain_manifest.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <regex>
#include <set>
using namespace voyemu;
namespace {
void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string failure(const std::function<void()> &call) {
    try {
        call();
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
}

std::string failure_code(const std::function<void()> &call) {
    return std::string(message_code(failure(call)));
}

std::string read(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);

    check(bool(in), "test input open");

    return std::string((std::istreambuf_iterator<char>(in)), {});
}

void write(const std::filesystem::path &file, std::string_view bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);

    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(out), "test output write");
}

std::uint32_t word(std::string_view bytes, std::size_t at) {
    std::uint32_t n = 0;

    for (unsigned i = 0; i < 4; ++i) {
        n |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }

    return n;
}

std::filesystem::path pinned_zig(const std::filesystem::path &root) {
#ifdef _WIN32
    const auto host = "windowsBuild", binary = "zig.exe";
#elif defined(__APPLE__)
    const auto host = "macosBuild", binary = "zig";
#else
    const auto host = "linuxBuild", binary = "zig";
#endif

    return root /
           embedded_toolchain_manifest()
                   .at("optionalTools")
                   .at(host)
                   .at("zig")
                   .at("directory")
                   .get<std::string>() /
           binary;
}

constexpr std::array<QnxGuestAdapter, 4> adapters = {
        QnxGuestAdapter::qinggan09, QnxGuestAdapter::qinggan11, QnxGuestAdapter::qinggan11_older,
        QnxGuestAdapter::passion};

// Links an OEM-like library with a GNU hash table that exports names, with the pinned Zig.
std::string library(const std::filesystem::path &zig, const std::filesystem::path &work,
                    const std::string &soname, const std::set<std::string> &names) {
    std::string assembly = ".text\n";

    for (const auto &name : names) {
        assembly += ".globl " + name + "\n.type " + name + ", %function\n" + name + ":\n ret\n";
    }

    write(work / (soname + ".s"), assembly);

    for (const std::vector<std::string> &args :
         {std::vector<std::string>{"clang", "-target", "aarch64-unknown-qnx7.0.0", "-c",
                                   (work / (soname + ".s")).string(), "-o",
                                   (work / (soname + ".o")).string()},
          std::vector<std::string>{"ld.lld", "-shared", "-nostdlib", "--hash-style=gnu", "-soname",
                                   soname, (work / (soname + ".o")).string(), "-o",
                                   (work / soname).string()}}) {
        ProcessSpec command;
        command.executable = zig;
        command.arguments = args;
        command.directory = work;
        const auto result = execute(command, 120000, 1 << 20);
        check(!result.timed_out && result.exit_code == 0, "OEM-like library link");
    }

    return read(work / soname);
}
} // namespace

int main(int argc, char **argv) {
    try {
        const auto passion_guest_floor = qnx_passion_source_reserve +
                                         qnx_passion_source_generated_cap;

        if (qnx_passion_guest_space_available(passion_guest_floor - 1) ||
            !qnx_passion_guest_space_available(passion_guest_floor) ||
            !qnx_passion_guest_space_available(passion_guest_floor + 1)) {
            throw std::runtime_error("Passion guest workspace inclusive disk floor");
        }

        // CTest runs from the project root, where scratch, pins and the pinned Zig live.
        const auto project = std::filesystem::canonical(std::filesystem::current_path());
        const auto guest_source =
                std::filesystem::path(__FILE__).parent_path().parent_path().parent_path() /
                "guest/qnx";
        const auto source_manifest = read_private_json(guest_source / "component.json");
        std::set<std::string> staged;

        for (const auto &name : source_manifest.at("sources")) {
            staged.insert(name.get<std::string>());
        }

        for (const auto *name : {"native_mcu_qinggan11_entry.c", "native_mcu_qinggan11.c",
                                 "native_mcu_qinggan11.h", "native_mcu_qinggan11.json"}) {
            staged.insert(name);
        }

        const std::regex local_include(R"inc(#include\s*"([^"/]+)")inc");

        for (const auto &name : staged) {
            std::ifstream in(guest_source / name);
            const std::string text((std::istreambuf_iterator<char>(in)), {});

            for (std::sregex_iterator at(text.begin(), text.end(), local_include), end; at != end;
                 ++at) {
                const auto header = (*at)[1].str();

                if (std::filesystem::is_regular_file(guest_source / header) &&
                    !staged.contains(header)) {
                    throw std::runtime_error("unstaged QNX guest dependency: " + name + " -> " +
                                             header);
                }
            }
        }

        // CI builds the four module sets into one flat release directory, <adapter>-<file>.
        if (argc == 4 && std::string_view(argv[1]) == "--recipe") {
            const auto root = std::filesystem::canonical(argv[2]),
                       output = std::filesystem::absolute(argv[3]);
            ensure_private_directory(output);

            for (const auto adapter : adapters) {
                const auto name = qnx_guest_adapter_name(adapter);
                const auto modules = output / ("." + name);
                const auto receipt = build_qnx_guest_recipe(root, pinned_zig(root), modules, false,
                                                            adapter,
                                                            embedded_toolchain_manifest()
                                                                    .at("qnxGuestModules")
                                                                    .at(name)
                                                                    .at("initialNativeState"));
                check(!receipt.contains("source_inputs"),
                      "guest recipe receipt must not pin repository sources");

                for (const auto &file : qnx_guest_module_files(adapter)) {
                    std::filesystem::rename(modules / file, output / (name + "-" + file));
                }

                std::filesystem::remove(modules);
                std::cout << receipt.dump() << '\n';
            }

            return 0;
        }

        // --run, --run-qinggan11 and --run-passion read the fetched originals of PROFILE.
        if (argc == 5 && (std::string_view(argv[1]) == "--run" ||
                          std::string_view(argv[1]) == "--run-qinggan11" ||
                          std::string_view(argv[1]) == "--run-passion")) {
            const bool qinggan11 = std::string_view(argv[1]) == "--run-qinggan11",
                       passion = std::string_view(argv[1]) == "--run-passion";
            const auto root = std::filesystem::canonical(argv[2]),
                       inputs = root / ".local/profiles" / argv[3] / "qnx";
            auto read = [](const std::filesystem::path &file) {
                std::ifstream in(file, std::ios::binary);

                if (!in) {
                    throw std::runtime_error("OEM input open");
                }

                return std::string((std::istreambuf_iterator<char>(in)), {});
            };
            const auto hyp = read(inputs / (passion ? "mifs_hyp_la.img" : "hyp_la.img")),
                       ifs = read(inputs / "ifs2_la.img");

            if (sha256_bytes(hyp) !=
                        (passion ? "1e7c487aae4f4353621f8d0301b352cc974e9037d114ff3ec2556d048512ad28"
                         : qinggan11
                                 ? "a476a1e3d3aaa51d855d2d91c412a962efb6ef1e3e63540ec739779d35ce3ce8"
                                 : "0af64f396f7de8dafe3488f21ec7e4cd989b3d02d3de0412aec020fdb1424b89") ||
                sha256_bytes(ifs) !=
                        (passion ? "1e942ab83e59ab78ff0096f474d3677ba7e83f6a00f86a1ebe9f0ae755a3e157"
                         : qinggan11
                                 ? "0e5598e15589c7816af65e2ee7257e240f62651fc142bf94129e08a1ce27c1c2"
                                 : "403269cadb0b279a8698eddf909948408caf0e2d5bd2423fd6a11e58dcde0ee0")) {
                throw std::runtime_error("OEM pins");
            }

            std::uint32_t end = 0;

            for (unsigned i = 0; i < 4; ++i) {
                end |= std::uint32_t(static_cast<unsigned char>(hyp.at(0x3024 + i))) << (i * 8);
            }

            const auto host = passion ? decode_qnx_passion_host(hyp)
                              : qinggan11
                                      ? decode_qnx_qinggan11_host(hyp, sha256_bytes(hyp))
                                      : decode_qnx_boot_stream(hyp.substr(0x5a108, end - 0x57108));
            const auto libc = read_qnx_imagefs_file(host, "lib64/libc.so.4"),
                       screen = read_qnx_imagefs_file(read_qnx_imagefs_file(ifs, "ifs_display.img"),
                                                      "lib64/libscreen.so.1");
            std::cout << build_qnx_guest_modules(
                                 root, libc, screen, std::filesystem::absolute(argv[4]), false,
                                 passion     ? QnxGuestAdapter::passion
                                 : qinggan11 ? QnxGuestAdapter::qinggan11
                                             : QnxGuestAdapter::qinggan09,
                                 qinggan11 ? Json{{"eps_calibrationFlag", 1}} : Json::object())
                                 .dump()
                      << '\n';

            return 0;
        }

        for (const auto adapter : {QnxGuestAdapter::qinggan11, QnxGuestAdapter::qinggan11_older,
                                   QnxGuestAdapter::passion, static_cast<QnxGuestAdapter>(99)}) {
            const auto error = failure_code([&] {
                build_qnx_guest_modules("relative", "", "", {}, true, adapter);
            });
            check(error == (adapter == static_cast<QnxGuestAdapter>(99) ? "QNX_GUEST_ADAPTER"
                                                                        : "QNX_GUEST_PATH"),
                  "adapter or path guard");
        }

        for (const auto adapter : {QnxGuestAdapter::qinggan09, QnxGuestAdapter::passion}) {
            check(failure_code([&] {
                      build_qnx_guest_modules("relative", "", "", {}, true, adapter,
                                              Json{{"eps_calibrationFlag", 1}});
                  }) == "QNX_GUEST_INITIAL_STATE_ADAPTER",
                  "native fixture reached a foreign adapter");
            check(failure_code([&] {
                      build_qnx_guest_recipe("relative", {}, {}, true, adapter,
                                             Json{{"eps_calibrationFlag", 1}});
                  }) == "QNX_GUEST_INITIAL_STATE_ADAPTER",
                  "native fixture reached a foreign recipe");
        }

        for (const auto &value : Json::array({-1, 4, 1.0, "1"})) {
            check(failure_code([&] {
                      build_qnx_guest_modules("relative", "", "", {}, true,
                                              QnxGuestAdapter::qinggan11,
                                              Json{{"eps_calibrationFlag", value}});
                  }) == "QNX_QINGGAN11_INITIAL_STATE",
                  "invalid native fixture reached preparation");
        }

        {
            /* The guest ldqnx preloader reads a full page per PT_GNU_RELRO segment; a file ending
             * short of the last relro page fails preload ("Could not preload library", fatal).
             * The link uses -z norelro, so no PT_GNU_RELRO is emitted; this guard is the
             * regression for both sides of the rule. */
            const auto page = 0x1000ULL, vaddr = 0x1000ULL, offset = 0x1000ULL,
                       memsz = 0xa50ULL; /* required page-rounded read: 0x2000 bytes from file
                                            offset 0x1000 */
            (void)page;
            auto elf = [&](const std::filesystem::path &target, std::size_t size) {
                std::string bytes(size, 0);
                bytes[0] = 0x7f;
                bytes[1] = 'E';
                bytes[2] = 'L';
                bytes[3] = 'F';
                bytes[4] = 2;
                bytes[5] = 1;
                const auto phoff = 64ULL, phentsize = 56ULL, phnum = 1ULL;

                for (int i = 0; i < 8; ++i) {
                    bytes[32 + i] = static_cast<char>((phoff >> (8 * i)) & 0xff);
                }

                bytes[54] = static_cast<char>(phentsize);
                bytes[55] = static_cast<char>(phentsize >> 8);
                bytes[56] = static_cast<char>(phnum);
                bytes[57] = static_cast<char>(phnum >> 8);
                const auto put = [&](std::size_t at, unsigned long long v) {
                    for (int i = 0; i < 8; ++i) {
                        bytes[at + i] = static_cast<char>((v >> (8 * i)) & 0xff);
                    }
                };
                put(phoff, 0x6474e552);
                put(phoff + 8, offset);
                put(phoff + 16, vaddr);
                put(phoff + 24, vaddr);
                put(phoff + 40, memsz);
                write(target, bytes);
            };
            const auto passing = std::filesystem::temp_directory_path() /
                                 "voyemu-ram64-relro-passing.elf",
                       failing = std::filesystem::temp_directory_path() /
                                 "voyemu-ram64-relro-failing.elf";
            elf(passing, 0x2000);
            elf(failing, 0x2000 - 1);
            check(elf64_gnu_relro_covers_page_rounded_read(passing),
                  "relro page-rounded read rejected a covering guest carrier");
            check(!elf64_gnu_relro_covers_page_rounded_read(failing),
                  "short relro tail accepted for preload");
            std::filesystem::remove(passing);
            std::filesystem::remove(failing);
        }

        check(!failure([] {
                   build_qnx_guest_modules("relative", "", "", {});
               }).empty(),
              "relative root accepted");

        // A source receipt names the module set of its hardware adapter by the digest of its pins.
        std::set<std::string> digests;

        for (const auto adapter : adapters) {
            digests.insert(qnx_guest_modules_digest(qnx_guest_adapter_name(adapter)));
        }

        check(digests.size() == adapters.size(), "module set digest per adapter");
        check(failure_code([] {
                  qnx_guest_modules_digest("sa8155-unknown");
              }) == "QNX_GUEST_ADAPTER",
              "module set digest of an unknown adapter");

        // Every adapter has a complete pinned module set whose assets name their files.
        for (const auto adapter : adapters) {
            const auto &pins = embedded_toolchain_manifest()
                                       .at("qnxGuestModules")
                                       .at(qnx_guest_adapter_name(adapter));
            check(pins.at("files").size() == qnx_guest_module_files(adapter).size(),
                  "module pin set");

            for (const auto &file : qnx_guest_module_files(adapter)) {
                const auto url = pins.at("files").at(file).at("url").get<std::string>();
                check(url.ends_with("/" + qnx_guest_adapter_name(adapter) + "-" + file),
                      "module asset name");
            }
        }

        const auto scratch = project / ".local/build/tmp" / ("qnx-guest-test-" + random_token());
        ensure_private_directory(scratch);
        const auto modules = qnx_guest_module_files(QnxGuestAdapter::qinggan09);
        Json fixture = {{"initialNativeState", Json::object()}, {"files", Json::object()}};

        for (const auto &file : modules) {
            fixture["files"][file] = {{"url", "https://example.invalid/" + file},
                                      {"sha256", sha256_bytes("module " + file)}};
        }

        const DownloadTransport missing = [](const DownloadPin &, const DownloadSink &) {
            throw std::runtime_error("DOWNLOAD_HTTP: 404");
        };
        const auto build = [&](const std::string &suffix) {
            return [&, suffix](const std::filesystem::path &job) {
                ensure_private_directory(job / "modules");

                for (const auto &file : modules) {
                    write(job / "modules" / file, "module " + file + suffix);
                }
            };
        };
        check(failure([&] {
                  install_qnx_guest_modules(scratch, QnxGuestAdapter::qinggan09, fixture, true);
              }).starts_with("DOWNLOAD_OFFLINE:"),
              "missing module download is reported offline");
        check(failure([&] {
                  install_qnx_guest_modules(scratch, QnxGuestAdapter::qinggan09, fixture, false,
                                            missing, build(" changed"));
              }).starts_with("QNX_GUEST_MODULES:"),
              "recipe output that differs from the pins rejected");
        check(!std::filesystem::exists(scratch / ".local/toolchains/qnx-guest") ||
                      std::filesystem::is_empty(scratch / ".local/toolchains/qnx-guest"),
              "rejected recipe output installs nothing");
        const auto installed = install_qnx_guest_modules(scratch, QnxGuestAdapter::qinggan09,
                                                         fixture, false, missing, build(""));
        check(installed.parent_path() == scratch / ".local/toolchains/qnx-guest" &&
                      read(installed / "front.so") == "module front.so",
              "missing release assets fall back to the recipe and its pins");
        check(install_qnx_guest_modules(scratch, QnxGuestAdapter::qinggan09, fixture, true) ==
                      installed,
              "installed module set reused offline");
        write(installed / "observer.so", "changed");
        check(failure([&] {
                  install_qnx_guest_modules(scratch, QnxGuestAdapter::qinggan09, fixture, true);
              }).starts_with("QNX_GUEST_MODULES:"),
              "changed installed module rejected");
        std::filesystem::remove_all(scratch);
        const auto zig = pinned_zig(project);

        if (!std::filesystem::is_regular_file(zig)) {
            std::cout << "QNX guest recipe skipped: the pinned Zig is not installed\n";
            std::cout << "QNX guest module guards passed\n";

            return 0;
        }

        // The recipe reproduces the pinned Free A9 set without OEM input or NDK.
        const auto work = project / ".local/build" / ("qnx-guest-recipe-test-" + random_token());
        ensure_private_directory(work);
        const auto receipt = build_qnx_guest_recipe(project, zig, work / "modules", true,
                                                    QnxGuestAdapter::qinggan09, Json::object());
        check(!receipt.contains("source_inputs"),
              "guest recipe receipt must not pin repository sources");
        const auto &pins =
                embedded_toolchain_manifest().at("qnxGuestModules").at("sa8155-qinggan09");

        for (const auto &file : modules) {
            check(receipt.at("files").at(file) == pins.at("files").at(file).at("sha256"),
                  "recipe output differs from qnxGuestModules");
        }

        const auto front = read(work / "modules/front.so"),
                   observer = read(work / "modules/observer.so");
        const auto imports = qnx_link_imports(front);
        check(imports.needed == std::vector<std::string>{"libc.so.4", "libscreen.so.1"} &&
                      !imports.strong.empty() && imports.strong.contains("screen_create_context"),
              "front imports stay undefined for the guest libraries");
        check(qnx_link_imports(observer).needed == std::vector<std::string>{"libc.so.4"},
              "observer needs only libc");
        std::set<std::string> c_names, screen_names;

        for (const auto &name : imports.strong) {
            (name.starts_with("screen_") ? screen_names : c_names).insert(name);
        }

        for (const auto &name : qnx_link_imports(observer).strong) {
            c_names.insert(name);
        }

        const auto libc = library(zig, work, "libc.so.4", c_names),
                   screen = library(zig, work, "libscreen.so.1", screen_names);
        verify_qnx_link_imports(front, {{"libc.so.4", libc}, {"libscreen.so.1", screen}});
        verify_qnx_link_imports(observer, {{"libc.so.4", libc}});
        check(failure([&] {
                  verify_qnx_link_imports(front, {{"libc.so.4", libc}});
              }).starts_with("QNX_LINK_NEEDED:"),
              "missing guest library accepted");
        auto partial = c_names;
        partial.erase(partial.begin());
        const auto short_libc = library(zig, work, "libc.so.4", partial);
        check(failure([&] {
                  verify_qnx_link_imports(observer, {{"libc.so.4", short_libc}});
              }).starts_with("QNX_LINK_IMPORT:"),
              "import absent from the OEM library accepted");
        // The OEM data lands in the reserved section as plain data; nothing else changes.
        const std::string baseline = "#!/bin/sh\nmount /\nclusterboot &\necho done\n";
        std::string original(4096, '\0');
        original.replace(0, 6,
                         std::string("\x7f"
                                     "ELF\x02\x01",
                                     6));
        original[18] = static_cast<char>(183);
        original.replace(1000, 5, "tools");
        const auto patched = qnx_qinggan09_front_oem_data(front, baseline, original);
        check(patched.size() == front.size(), "OEM data changes the module size");
        QnxElfSection section;

        for (const auto &item : qnx_elf_sections(patched)) {
            if (item.name == ".voyemu_oem") {
                section = item;
            }
        }

        check(section.size == qnx_qinggan09_oem_capacity && (section.flags & 2) != 0,
              "reserved OEM section");
        const auto at = section.offset;
        check(patched.substr(at, 8) == "VOYEMUOD" && word(patched, at + 8) == baseline.size() &&
                      word(patched, at + 12) == original.size() &&
                      word(patched, at + 20) == qnx_qinggan09_oem_capacity &&
                      patched.substr(at + 32, baseline.size()) == baseline,
              "OEM section header and baseline");
        const auto packed = patched.substr(at + 32 + baseline.size(), word(patched, at + 16));
        std::string unpacked;

        for (std::size_t in = 0; in < packed.size();) {
            const auto token = static_cast<unsigned char>(packed[in++]);
            const std::size_t n = (token & 127U) + 1;
            unpacked += token & 128U ? std::string(n, '\0') : packed.substr(in, n);
            in += token & 128U ? 0 : n;
        }

        check(unpacked == original, "packed sysmgrtool unpacks to the original");
        check(patched.substr(0, at) == front.substr(0, at) &&
                      patched.substr(at + section.size) == front.substr(at + section.size),
              "OEM data written outside its section");
        check(qnx_link_imports(patched).strong == imports.strong,
              "OEM data changed the module imports");
        check(failure_code([&] {
                  qnx_qinggan09_front_oem_data(patched, baseline, original);
              }) == "QNX_GUEST_OEM_SECTION",
              "filled OEM section accepted twice");
        check(failure([&] {
                  qnx_qinggan09_front_oem_data(
                          front, baseline,
                          std::string(original) + std::string(4, '\0') +
                                  std::string(qnx_qinggan09_oem_capacity, 'x'));
              }).starts_with("QNX_"),
              "OEM data beyond the section accepted");
        check(failure_code([&] {
                  qnx_qinggan09_front_oem_data(observer, baseline, original);
              }) == "QNX_LINK_ELF_SECTIONS" ||
                      failure_code([&] {
                          qnx_qinggan09_front_oem_data(observer, baseline, original);
                      }) == "QNX_GUEST_OEM_SECTION",
              "module without the OEM section accepted");
        std::filesystem::remove_all(work);
        std::cout << "QNX guest source build preflight guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
