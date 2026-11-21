#include "qnx_guest_build.hpp"
#include "check.hpp"
#include "download.hpp"
#include "error.hpp"
#include "logging.hpp"
#include "process.hpp"
#include "project_files.hpp"
#include "qnx_link_interface.hpp"
#include "qnx_passion.hpp"
#include "qnx_prepare.hpp"
#include "qnx_qinggan11_mcu_layout.hpp"
#include "qnx_startup_extension.hpp"
#include "quickboot.hpp"
#include "toolchain_manifest.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
namespace voyemu {
bool elf64_gnu_relro_covers_page_rounded_read(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);

    if (!in) {
        return false;
    }

    std::string bytes((std::istreambuf_iterator<char>(in)), {});
    const auto u64 = [&](std::size_t at) {
        unsigned long long v = 0;

        for (int i = 0; i < 8; ++i) {
            v |= static_cast<unsigned long long>(static_cast<unsigned char>(bytes[at + i]))
                 << (8 * i);
        }

        return v;
    };
    const auto u16 = [&](std::size_t at) {
        return static_cast<unsigned>((static_cast<unsigned char>(bytes[at])) |
                                     (static_cast<unsigned char>(bytes[at + 1]) << 8));
    };

    if (bytes.size() <= 64) {
        return false;
    }

    const auto phoff = static_cast<std::size_t>(u64(32)),
               phentsize = static_cast<std::size_t>(u16(54)),
               phnum = static_cast<std::size_t>(u16(56));

    if (phoff < 64 || phentsize < 56 || phnum > 64 || bytes.size() < phoff + phentsize * phnum) {
        return false;
    }

    const auto page = 0x1000ULL;

    for (std::size_t i = 0; i < phnum; ++i) {
        const auto at = phoff + i * phentsize;

        if (u64(at) != 0x6474e552) {
            continue;
        }

        const auto offset = u64(at + 8), vaddr = u64(at + 16), memsz = u64(at + 40);
        const auto begin = vaddr & ~(page - 1);
        const auto end = (vaddr + memsz + page - 1) & ~(page - 1);

        if (vaddr < begin || offset < vaddr - begin) {
            return false;
        }

        const auto file_begin = offset - (vaddr - begin);

        if (end - begin > bytes.size() - file_begin) {
            return false;
        }
    }

    return true;
}

namespace {
constexpr CodeCheck need{"QNX guest build check failed"};

void write(const std::filesystem::path &path, std::string_view bytes) {
    need(!std::filesystem::exists(path), "QNX_GUEST_FRESH_FILE");
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), bytes.size());
    out.close();
    need(bool(out), "QNX_GUEST_WRITE");
}

std::string read(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);

    need(bool(in), "QNX_GUEST_READ");

    return std::string((std::istreambuf_iterator<char>(in)), {});
}

bool inside(const std::filesystem::path &path, const std::filesystem::path &parent) {
    auto p = path.begin();

    for (auto at = parent.begin(); at != parent.end(); ++at, ++p) {
        if (p == path.end() || *p != *at) {
            return false;
        }
    }

    return true;
}

void elf(std::string_view bytes) {
    need(bytes.size() > 64 && bytes.size() <= 1024 * 1024 &&
                 bytes.substr(0, 6) == std::string_view("\x7f"
                                                        "ELF\x02\x01",
                                                        6) &&
                 static_cast<unsigned char>(bytes[18]) == 183 && bytes[19] == 0,
         "QNX_GUEST_OEM_ABI");
}

std::uint32_t word(std::string_view bytes, std::size_t at) {
    std::uint32_t n = 0;

    for (unsigned i = 0; i < 4; ++i) {
        n |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }

    return n;
}

void put_word(std::string &bytes, std::size_t at, std::uint64_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<char>(value >> (8 * i));
    }
}

std::string sysmgr_tool_pack(std::string_view input) {
    need(!input.empty() && input.size() <= 262144, "QNX_SYSMGR_ORIGINAL_BOUND");
    std::string out;

    for (std::size_t at = 0; at < input.size();) {
        std::size_t n = 1;

        if (input[at] == 0) {
            while (n < 128 && at + n < input.size() && input[at + n] == 0) {
                ++n;
            }

            if (n >= 3) {
                out += static_cast<char>(128 + n - 1);
                at += n;
                continue;
            }
        }

        n = 1;

        while (n < 128 && at + n < input.size() &&
               input.substr(at + n, 3) != std::string_view("\0\0\0", 3)) {
            ++n;
        }

        out += static_cast<char>(n - 1);
        out.append(input.substr(at, n));
        at += n;
    }

    return out;
}

std::string c_array(std::string_view name, std::string_view bytes) {
    std::string out = "static const unsigned char " + std::string(name) + "[]={";

    for (unsigned char b : bytes) {
        out += std::to_string(b);
        out += ',';
    }

    return out + "};\n";
}

// The bytes objcopy -O binary --only-section writes: the named sections from the lowest to the
// highest address, with zero gaps.
std::string section_image(std::string_view bytes, std::initializer_list<std::string_view> names) {
    std::vector<QnxElfSection> chosen;

    for (const auto &section : qnx_elf_sections(bytes)) {
        if (std::find(names.begin(), names.end(), section.name) != names.end()) {
            need(section.type == 1, "QNX_GUEST_SECTION");
            chosen.push_back(section);
        }
    }

    need(chosen.size() == names.size(), "QNX_GUEST_SECTION");
    std::uint64_t begin = UINT64_MAX, end = 0;

    for (const auto &section : chosen) {
        begin = std::min(begin, section.address);
        end = std::max(end, section.address + section.size);
    }

    need(end > begin && end - begin <= 65536, "QNX_GUEST_SECTION");
    std::string image(end - begin, '\0');

    for (const auto &section : chosen) {
        image.replace(section.address - begin, section.size,
                      bytes.substr(section.offset, section.size));
    }

    return image;
}

// The reserved section of the Free A9 front module: magic, the baseline, original and packed
// lengths at 8, 12 and 16, the capacity at 20 and the bytes from 32.
constexpr std::string_view oem_magic = "VOYEMUOD";
constexpr std::size_t oem_header = 32;

QnxElfSection oem_section(std::string_view front) {
    std::optional<QnxElfSection> found;

    for (const auto &section : qnx_elf_sections(front)) {
        if (section.name == ".voyemu_oem") {
            need(!found, "QNX_GUEST_OEM_SECTION");
            found = section;
        }
    }

    if (!found) {
        throw VoyemuError("QNX_GUEST_OEM_SECTION", "OEM section differs");
    }

    const auto section = *found;
    need(section.type == 1 && (section.flags & 2) && section.size == qnx_qinggan09_oem_capacity &&
                 front.substr(section.offset, oem_magic.size()) == oem_magic &&
                 word(front, section.offset + 20) == qnx_qinggan09_oem_capacity,
         "QNX_GUEST_OEM_SECTION");

    return section;
}

const Json &module_pins(QnxGuestAdapter adapter) {
    return embedded_toolchain_manifest().at("qnxGuestModules").at(qnx_guest_adapter_name(adapter));
}

std::string module_digest(QnxGuestAdapter adapter, const Json &pins) {
    std::string lines;

    for (const auto &name : qnx_guest_module_files(adapter)) {
        lines += pins.at("files").at(name).at("sha256").get<std::string>() + "  " + name + "\n";
    }

    return sha256_bytes(lines);
}
} // namespace

std::string qnx_guest_adapter_name(QnxGuestAdapter adapter) {
    return std::string(qnx_adapter(adapter).name);
}

std::vector<std::string> qnx_guest_module_files(QnxGuestAdapter adapter) {
    switch (qnx_adapter(adapter).platform) {
    case QnxGuestPlatform::qinggan09:
        return {"front.so",     "observer.so",      "virtual_wfd.so",
                "readstub.bin", "qnx-ipl-virt.bin", "qnx-ifs2-restore.bin"};
    case QnxGuestPlatform::qinggan11:
        return {"front.so",         "observer.so",          "virtual_wfd.so",       "readstub.bin",
                "qnx-ipl-virt.bin", "qnx-ifs2-restore.bin", "qnx-pl011-console.bin"};
    case QnxGuestPlatform::passion:
        return {"front.so",
                "bounded.so",
                "ram64.so",
                "virtual_wfd.so",
                "readstub.bin",
                "qnx-ipl-virt.bin",
                "qnx-ifs2-restore.bin",
                "qnx-pl011-console.bin",
                "qnx-passion-ifs2-copy.bin"};
    }

    throw VoyemuError("QNX_GUEST_ADAPTER", "unknown guest adapter");
}

std::string qnx_guest_modules_digest(std::string_view hardware_adapter) {
    const auto adapter = qnx_adapter(hardware_adapter).adapter;

    return module_digest(adapter, module_pins(adapter));
}

Json build_qnx_guest_recipe(const std::filesystem::path &root, const std::filesystem::path &zig,
                            const std::filesystem::path &output, bool offline,
                            QnxGuestAdapter adapter, const Json &initial_native_state) {
    const auto &spec = qnx_adapter(adapter);
    const auto name = std::string(spec.name);
    const bool qinggan11 = spec.platform == QnxGuestPlatform::qinggan11,
               passion = spec.platform == QnxGuestPlatform::passion,
               qinggan09 = spec.platform == QnxGuestPlatform::qinggan09;

    need(initial_native_state.is_object() && (qinggan11 || initial_native_state.empty()),
         "QNX_GUEST_INITIAL_STATE_ADAPTER");

    if (qinggan11) {
        validate_qnx_qinggan11_initial_state(initial_native_state);
    }

    need(root.is_absolute() && std::filesystem::canonical(root) == root && output.is_absolute() &&
                 output.lexically_normal() == output && inside(output, root / ".local/build") &&
                 output != root / ".local/build" && !std::filesystem::exists(output),
         "QNX_GUEST_PATH");
    need(zig.is_absolute() && std::filesystem::is_regular_file(zig) &&
                 !std::filesystem::is_symlink(zig),
         "QNX_GUEST_TOOL");
    const auto zig_epoch = cache_file_identity(zig);
    need(std::filesystem::create_directory(output), "QNX_GUEST_FRESH_OUTPUT");
    ensure_private_directory(output);
    const auto work = output / "work";
    ensure_private_directory(work);
    ensure_private_directory(work / "src");
    const auto source = root / "src/guest/qnx";
    const auto definition = read_private_json(source / "component.json", 128 * 1024);
    need(definition.at("schema") == 1 && definition.at("sources").is_array() &&
                 definition.at("sources").size() <= 128 && definition.at("headers").size() == 3,
         "QNX_GUEST_SOURCE_MANIFEST");
    Json sources = Json::object();
    std::set<std::string> source_names;
    auto copy_source = [&](const std::string &file_name) {
        const std::filesystem::path relative = file_name;
        need(!relative.is_absolute() && relative.filename() == relative &&
                     source_names.insert(file_name).second,
             "QNX_GUEST_SOURCE_NAME");
        const auto file = source / relative;
        need(!std::filesystem::is_symlink(file) && std::filesystem::is_regular_file(file) &&
                     std::filesystem::file_size(file) <= 1024 * 1024,
             "QNX_GUEST_SOURCE_BOUNDS");
        const auto pin = sha256_file(file);
        std::filesystem::copy_file(file, work / "src" / relative);
        need(sha256_file(work / "src" / relative) == pin, "QNX_GUEST_SOURCE_CHANGED");
        sources[file_name] = pin;
    };

    for (const auto &file_name : definition.at("sources")) {
        copy_source(file_name.get<std::string>());
    }

    if (qinggan11 || passion) {
        copy_source("qnx-pl011-console.S");
    }

    // The HAB channel core is shared with the multimedia libuhab of the same platform.
    const auto common = root / "profiles/common/components/hab-socket/src/hab_socket.c";

    if (passion) {
        copy_source("qnx-passion-ifs2-copy.S");
        need(!std::filesystem::is_symlink(common) && std::filesystem::is_regular_file(common) &&
                     std::filesystem::file_size(common) <= 1024 * 1024 &&
                     source_names.insert("hab_socket.c").second,
             "QNX_GUEST_SOURCE_BOUNDS");
        const auto pin = sha256_file(common);
        std::filesystem::copy_file(common, work / "src/hab_socket.c");
        need(sha256_file(work / "src/hab_socket.c") == pin, "QNX_GUEST_SOURCE_CHANGED");
        sources["hab_socket.c"] = pin;
    }

    if (qinggan11) {
        for (const auto *file_name :
             {"native_mcu_qinggan11_entry.c", "native_mcu_qinggan11.c", "native_mcu_qinggan11.h"}) {
            copy_source(file_name);
        }

        const auto layout_file = std::string(spec.mcu_layout_file);
        copy_source(layout_file);
        write(work / "src/native_mcu_qinggan11_layout.inc",
              qnx_qinggan11_mcu_layout(read_private_json(work / "src" / layout_file, 512 * 1024),
                                       initial_native_state));
    }

    ensure_private_directory(work / "include/WF");
    ensure_private_directory(work / "include/KHR");

    for (auto at = definition.at("headers").begin(); at != definition.at("headers").end(); ++at) {
        need(at.key() == "WF/wfd.h" || at.key() == "WF/wfdplatform.h" ||
                     at.key() == "KHR/khrplatform.h",
             "QNX_WFD_HEADER_MANIFEST");
        const DownloadPin pin{at.value().at("url").get<std::string>(),
                              at.value().at("sha256").get<std::string>(), 128 * 1024};
        const auto blob = download_pinned(pin, root / ".local/toolchains/downloads/sources",
                                          offline);
        std::filesystem::copy_file(blob.file, work / "include" / at.key());
    }

    const auto resource = (zig.parent_path() / "lib").string();
    auto run = [&](const char *tool, std::vector<std::string> args) {
        ProcessSpec command;
        command.executable = zig;
        command.arguments = {tool};

        // The bundled clang finds its builtin headers under the lib directory of Zig.
        if (std::string_view(tool) == "clang") {
            command.arguments.insert(command.arguments.end(), {"-resource-dir", resource});
        }

        command.arguments.insert(command.arguments.end(), args.begin(), args.end());
        command.directory = work / "src";
        command.environment = {
                {"TMPDIR", work.string()}, {"TEMP", work.string()}, {"TMP", work.string()}};
        const auto result = execute(command, 120000, 1024 * 1024);
        need(!result.timed_out && result.exit_code == 0,
             ("QNX_GUEST_TOOL: " + result.output).c_str());
    };
    const auto file = [&](const std::string &file_name) {
        return (work / file_name).string();
    };
    const std::vector<std::string> guest = {
            "-target", "aarch64-unknown-qnx7.0.0", "-nostdlibinc", "-std=c11", "-fPIC", "-Wall",
            "-Werror"};
    auto compile = [&](std::vector<std::string> args) {
        args.insert(args.begin(), guest.begin(), guest.end());
        run("clang", std::move(args));
    };
    // The guest libraries carry only their sonames: imports stay undefined in each module, and the
    // QNX runtime linker resolves them in libc.so.4 and libscreen.so.1 of the guest.
    write(work / "soname.s", "");
    run("clang",
        {"-target", "aarch64-unknown-qnx7.0.0", "-c", file("soname.s"), "-o", file("soname.o")});

    for (const auto *library : {"libc.so.4", "libscreen.so.1"}) {
        run("ld.lld", {"-shared", "-nostdlib", "-soname", library, file("soname.o"), "-o",
                       file(std::string(library) + ".link.so")});
    }

    auto link_module = [&](const std::string &soname, std::vector<std::string> options,
                           std::vector<std::string> objects, const std::string &result,
                           bool screen) {
        std::vector<std::string> args = {"-shared", "-nostdlib", "-z", "now"};
        args.insert(args.end(), options.begin(), options.end());
        args.insert(args.end(), {"-soname", soname});
        args.insert(args.end(), objects.begin(), objects.end());
        args.push_back(file("libc.so.4.link.so"));

        if (screen) {
            args.push_back(file("libscreen.so.1.link.so"));
        }

        args.insert(args.end(), {"-o", file(result)});
        run("ld.lld", std::move(args));
    };

    if (qinggan09) {
        compile({"-Oz", "-Wextra", "-c", "sysmgr_wait_compat.c", "-o",
                 file("sysmgr_wait_compat.o")});
        link_module("libvoyemu-sysmgr-wait.so", {"-Bsymbolic"}, {file("sysmgr_wait_compat.o")},
                    "sysmgr-wait.so", false);
        const auto launcher = read(work / "src/sysmgr_tool_launcher.sh"),
                   shim = read(work / "sysmgr-wait.so");
        need(launcher.starts_with("#!/bin/sh\n") && launcher.size() <= 4096 &&
                     launcher.find('\0') == std::string::npos,
             "QNX_SYSMGR_SCRIPT");
        elf(shim);
        need(shim.size() <= 16384, "QNX_SYSMGR_ASSET_BOUND");
        write(work / "src/sysmgr_tool_assets.h",
              c_array("qst_launcher", launcher) + c_array("qst_shim", shim));
    }

    std::map<std::string, std::string> modules;
    run("clang", {"-target", "aarch64-linux-gnu", "-c",
                  passion ? "readstub-passion.S" : "readstub.S", "-o", file("readstub.o")});
    auto stub = section_image(read(work / "readstub.o"), {".text"});

    if (passion) {
        need(stub.size() == 176, "QNX_PASSION_STUB_SOURCE_PARITY");
    } else {
        need(stub.size() == 140 && stub.substr(0x3c, 4) == std::string_view("\0\0\0\x94", 4),
             "QNX_STUB_BRANCH_SITE");
        const std::uint32_t branch =
                0x94000000U |
                (static_cast<std::uint32_t>((0xc490 - 0x14188 - 0x3c) / 4) & 0x3ffffffU);

        for (unsigned i = 0; i < 4; ++i) {
            stub[0x3c + i] = static_cast<char>(branch >> (i * 8));
        }
    }

    modules["readstub.bin"] = stub;
    auto blob = [&](const std::string &input, std::vector<std::string> definitions,
                    std::vector<std::string> link,
                    std::initializer_list<std::string_view> sections) {
        const auto object = file(input + ".o"), image = file(input + ".elf");
        std::vector<std::string> args = {"-target", "aarch64-linux-gnu", "-c", input + ".S", "-o",
                                         object};
        args.insert(args.end(), definitions.begin(), definitions.end());
        run("clang", std::move(args));
        link.insert(link.begin(), {"-nostdlib", "--no-undefined"});
        link.insert(link.end(), {object, "-o", image});
        run("ld.lld", std::move(link));

        return section_image(read(image), sections);
    };

    if (qinggan11 || passion) {
        const auto console = blob("qnx-pl011-console", {},
                                  {"-e", "qnx_pl011_tx", "-Ttext=0x857017cc"}, {".text"});
        need(console.size() == 112, "QNX_CONSOLE_SOURCE_PARITY");
        modules["qnx-pl011-console.bin"] = console;
    }

    if (passion) {
        const auto copy = blob("qnx-passion-ifs2-copy", {},
                               {"-e", "qnx_passion_ifs2_copy", "-Ttext=0x8a000100"}, {".text"});
        need(copy.size() == 120, "QNX_PASSION_COPY_SOURCE_PARITY");
        modules["qnx-passion-ifs2-copy.bin"] = copy;
    }

    const bool queries = qinggan11 || passion;
    std::vector<std::string> ipl_definitions, restore_definitions;

    if (queries) {
        ipl_definitions = {"-DQNX_STARTUP_ENTRY=0x85701000", "-DQNX_STARTUP_STATUS=0x8575f000"};
        restore_definitions = {"-DQNX_IFS2_END=0xbfffffff", "-DQNX_ADD_ASINFO=0x85706170",
                               "-DQNX_IFS2_LOADER=0x85703870"};
    }

    if (passion) {
        ipl_definitions.push_back("-DQNX_IPL_RAM_GIB=6");
    }

    const auto ipl = blob("qnx-ipl-virt", ipl_definitions, {"-e", "_start", "-Ttext=0x40200000"},
                          {".text"});
    modules["qnx-ipl-virt.bin"] = ipl;
    const auto prior = blob("qnx-ifs2-restore", restore_definitions,
                            {"-e", "_start", "-Ttext=0x8a000000"}, {".text"});
    modules["qnx-ifs2-restore.bin"] = prior;

    if (queries) {
        run("clang", {"-target", "aarch64-linux-gnu", "-c", "qnx-boot-storage.S", "-o",
                      file("qnx-boot-storage.o")});
        run("ld.lld", {"-nostdlib", "--no-undefined", "-e", "_start", "-Ttext=0x8a000000",
                       "--section-start=.boot_storage=0x8a000080", file("qnx-ifs2-restore.o"),
                       file("qnx-boot-storage.o"), "-o", file("qnx-ifs2-restore.extended.elf")});
        const auto bundle = section_image(read(work / "qnx-ifs2-restore.extended.elf"),
                                          {".text", ".boot_storage"});
        need(prior.size() == 115 && bundle.size() == 256 && bundle.substr(0, 115) == prior &&
                     bundle.substr(115, 13) == std::string(13, 0),
             "QNX_BOOT_QUERY_PREFIX_PARITY");
        auto wide = [&](std::size_t at) {
            return std::uint64_t(word(bundle, at)) | (std::uint64_t(word(bundle, at + 4)) << 32);
        };
        need(wide(0xe8) == 0x857632e8 && wide(0xf0) == 0x85710310 && wide(0xf8) == 0x85710258,
             "QNX_BOOT_QUERY_LITERAL_LAYOUT");
        modules["qnx-ifs2-restore.bin"] = bundle;
    }

    // Guest objects see only the compiler and staged headers on every host: -nostdlibinc keeps the
    // host /usr/include, whose stdint.h khrplatform.h would reach, out of the QNX target.
    compile({"-O2", "-DKHR_BOOLEAN_FALSE=KHRONOS_FALSE", "-DKHR_BOOLEAN_TRUE=KHRONOS_TRUE", "-I",
             (work / "include").string(), "-c", "virtual_wfd.c", "-o", file("virtual_wfd.o")});
    link_module("libopenwfd_qnx.so", {}, {file("virtual_wfd.o")}, "virtual_wfd.so", false);
    modules["virtual_wfd.so"] = read(work / "virtual_wfd.so");
    need(modules.at("virtual_wfd.so").size() <= (passion ? 97848 : 97855), "QNX_WFD_SLOT_CAPACITY");

    if (passion) {
        compile({"-Oz", "-Wextra", "-c", "passion_ram64.c", "-o", file("ram64.o")});
        // QNX ldqnx relro loader requires a full-page read of the PT_GNU_RELRO segment; a tail
        // relro shorter than its page makes preload fatal ("Could not preload library").
        link_module("libvoyemu-passion-ram64.so", {"-z", "norelro", "-Bsymbolic"},
                    {file("ram64.o")}, "ram64.so", false);
        need(elf64_gnu_relro_covers_page_rounded_read(work / "ram64.so"),
             "QNX_PASSION_RAM64_RELRO_READABLE");
        modules["ram64.so"] = read(work / "ram64.so");
        need(modules.at("ram64.so").size() <= 5728, "QNX_PASSION_RAM64_SLOT");
    }

    for (const auto &[input, module] : std::array<std::pair<std::string, std::string>, 2>{
                 {{qinggan11 ? "native_mcu_qinggan11_entry.c"
                   : passion ? "passion_front_entry.c"
                             : "generated.c",
                   "front"},
                  {passion ? "bounded_wait_entry.c" : "mcd_observer.c",
                   passion ? "bounded" : "observer"}}}) {
        std::vector<std::string> args = {"-Oz", "-c", input, "-o", file(module + ".o")};

        if (module == "front") {
            if (passion) {
                args.push_back("-DQGFX_PASSION_ROOT_IMAGEFS=1");
            }

            if (qinggan09) {
                args.insert(args.end(),
                            {"-DQGFX_QINGGAN09=1",
                             "-DVOYEMU_OEM_CAPACITY=" + std::to_string(qnx_qinggan09_oem_capacity),
                             "-DVOYEMU_SYSMGR_TOOL_ASSETS=1"});
            }

            // The Kanzi Gauge observer serves the A9 and A11 clusters; Passion has no Kanzi Gauge.
            if (!passion) {
                args.push_back("-DQGFX_KANZI_UI=1");
            }

            if (!spec.guest_define.empty()) {
                args.emplace_back(spec.guest_define);
            }
        }

        compile(std::move(args));
        link_module(module == "front" ? "libvoyemu-gl-client.so"
                    : passion         ? "libvoyemu-bounded.so"
                                      : "libvoyemu-mcd-observer.so",
                    {"-Bsymbolic"}, {file(module + ".o")}, module + ".so", module == "front");
        modules[module + ".so"] = read(work / (module + ".so"));
        elf(modules.at(module + ".so"));
    }

    const auto &front = modules.at("front.so"),
               &observer = modules.at(passion ? "bounded.so" : "observer.so");

    if (qinggan09) {
        oem_section(front);
    }

    need(front.size() + observer.size() <= 360000 && observer.size() <= (passion ? 21313 : 16928),
         "QNX_GUEST_SLOT_CAPACITY");

    for (auto at = sources.begin(); at != sources.end(); ++at) {
        need(sha256_file(at.key() == "hab_socket.c" ? common : source / at.key()) ==
                     at.value().get<std::string>(),
             "QNX_GUEST_SOURCE_CHANGED");
    }

    need(cache_file_identity(zig) == zig_epoch, "QNX_GUEST_TOOL_CHANGED");
    Json files = Json::object();

    for (const auto &module : qnx_guest_module_files(adapter)) {
        write(output / module, modules.at(module));
        files[module] = sha256_bytes(modules.at(module));
    }

    std::filesystem::remove_all(work);

    return {{"schema", 1},
            {"hardware_adapter", name},
            {"initial_native_state", initial_native_state},
            {"files", files}};
}

std::filesystem::path
install_qnx_guest_modules(const std::filesystem::path &root, QnxGuestAdapter adapter,
                          const Json &pins, bool offline, const DownloadTransport &transport,
                          const std::function<void(const std::filesystem::path &)> &build) {
    const auto names = qnx_guest_module_files(adapter);

    need(pins.at("files").size() == names.size(), "QNX_GUEST_MODULE_PINS");
    const auto folder = root / ".local/toolchains/qnx-guest" / module_digest(adapter, pins);
    const auto pinned = [&](const std::filesystem::path &directory) {
        for (const auto &module : names) {
            const auto file = directory / module;

            if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) ||
                std::filesystem::hard_link_count(file) != 1 ||
                sha256_file(file) != pins.at("files").at(module).at("sha256").get<std::string>()) {
                return false;
            }
        }

        return true;
    };

    if (std::filesystem::exists(std::filesystem::symlink_status(folder))) {
        if (!pinned(folder)) {
            throw VoyemuError("QNX_GUEST_MODULES",
                              "installed module differs from its pin: " + folder.string());
        }

        return folder;
    }

    ensure_private_directory(folder.parent_path());
    const auto temporary = folder.parent_path() / (".part-" + random_token());
    struct Owned {
        std::filesystem::path path;

        ~Owned() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    } owned{temporary};
    ensure_private_directory(temporary);

    try {
        for (const auto &module : names) {
            const auto &pin = pins.at("files").at(module);
            const auto blob = download_pinned({pin.at("url").get<std::string>(),
                                               pin.at("sha256").get<std::string>(), 2ULL << 20},
                                              root / ".local/toolchains/downloads/qnx-guest",
                                              offline, 0, transport)
                                      .file;
            std::filesystem::copy_file(blob, temporary / module);
        }

        logging::write(logging::Level::info, "prepare", "QNX guest modules downloaded",
                       {{"adapter", qnx_guest_adapter_name(adapter)}});
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();

        // Only assets that are not published fall back; a network failure stays an error.
        if (offline || !download_unpublished(std::current_exception())) {
            throw;
        }

        const auto job = root / ".local/build/jobs" / ("qnx-guest-" + random_token());
        const Owned job_owner{job};
        ensure_private_directory(job);

        if (build) {
            build(job);
        } else {
#ifdef _WIN32
            const auto host = "windowsBuild", binary = "zig.exe";
#elif defined(__APPLE__)
            const auto host = "macosBuild", binary = "zig";
#else
            const auto host = "linuxBuild", binary = "zig";
#endif
            const auto zig = root /
                             embedded_toolchain_manifest()
                                     .at("optionalTools")
                                     .at(host)
                                     .at("zig")
                                     .at("directory")
                                     .get<std::string>() /
                             binary;

            if (!std::filesystem::is_regular_file(zig) || !project_source_checkout(root)) {
                throw VoyemuError("QNX_GUEST_ASSET_MISSING",
                                  "the " + qnx_guest_adapter_name(adapter) +
                                          " module assets cannot be downloaded (" + message +
                                          ") and the project sources and pinned Zig of the "
                                          "developer tools that build them are not installed");
            }

            logging::write(logging::Level::info, "prepare",
                           "QNX guest modules built from the recipe",
                           {{"adapter", qnx_guest_adapter_name(adapter)}, {"download", message}});
            build_qnx_guest_recipe(root, zig, job / "modules", offline, adapter,
                                   pins.at("initialNativeState"));
        }

        for (const auto &module : names) {
            std::filesystem::copy_file(job / "modules" / module, temporary / module);
        }
    }

    if (!pinned(temporary)) {
        throw VoyemuError("QNX_GUEST_MODULES", "built or downloaded module differs from its "
                                               "pin; the recipe output must match qnxGuestModules");
    }

    std::filesystem::rename(temporary, folder);

    return folder;
}

std::string qnx_qinggan09_front_oem_data(std::string_view front, std::string_view startup_baseline,
                                         std::string_view sysmgrtool_original) {
    need(!startup_baseline.empty() && startup_baseline.size() <= 65536,
         "QNX_GUEST_STARTUP_BASELINE");
    need(qnx_startup_extension(startup_baseline, startup_baseline, sha256_bytes(startup_baseline))
                 .empty(),
         "QNX_GUEST_STARTUP_ANCHOR");
    elf(sysmgrtool_original);
    const auto packed = sysmgr_tool_pack(sysmgrtool_original);
    const auto section = oem_section(front);
    need(front.substr(section.offset + 8, 12) == std::string(12, '\0') &&
                 front.substr(section.offset + 24, section.size - 24) ==
                         std::string(section.size - 24, '\0'),
         "QNX_GUEST_OEM_SECTION");
    need(oem_header + startup_baseline.size() + packed.size() <= section.size,
         "QNX_GUEST_OEM_CAPACITY");
    std::string out(front);
    put_word(out, section.offset + 8, startup_baseline.size());
    put_word(out, section.offset + 12, sysmgrtool_original.size());
    put_word(out, section.offset + 16, packed.size());
    out.replace(section.offset + oem_header, startup_baseline.size(), startup_baseline);
    out.replace(section.offset + oem_header + startup_baseline.size(), packed.size(), packed);

    return out;
}

Json build_qnx_guest_modules(const std::filesystem::path &root, std::string_view libc,
                             std::string_view screen, const std::filesystem::path &output,
                             bool offline, QnxGuestAdapter adapter,
                             const Json &initial_native_state, std::string_view startup_baseline,
                             std::string_view sysmgrtool_original) {
    const auto &spec = qnx_adapter(adapter);
    const bool qinggan11 = spec.platform == QnxGuestPlatform::qinggan11,
               passion = spec.platform == QnxGuestPlatform::passion,
               qinggan09 = spec.platform == QnxGuestPlatform::qinggan09;

    need(initial_native_state.is_object() && (qinggan11 || initial_native_state.empty()),
         "QNX_GUEST_INITIAL_STATE_ADAPTER");

    if (qinggan11) {
        validate_qnx_qinggan11_initial_state(initial_native_state);
    }

    need(root.is_absolute() && std::filesystem::canonical(root) == root && output.is_absolute() &&
                 output.lexically_normal() == output && inside(output, root / ".local/build") &&
                 output != root / ".local/build" && !std::filesystem::exists(output),
         "QNX_GUEST_PATH");
    need(qinggan09 == !startup_baseline.empty() && qinggan09 == !sysmgrtool_original.empty(),
         "QNX_GUEST_OEM_DATA_ADAPTER");
    elf(libc);
    elf(screen);
    need(std::filesystem::space(root).available >=
                 (qinggan11   ? qnx_qinggan11_disk_floor + qnx_qinggan11_guest_workspace
                  : qinggan09 ? qnx_qinggan09_disk_floor + qnx_qinggan09_guest_workspace
                              : qnx_passion_source_reserve + qnx_passion_source_generated_cap),
         "QNX_GUEST_DISK_RESERVE");
    // The pinned modules carry the initial native state they were built with.
    need(module_pins(adapter).at("initialNativeState") == initial_native_state,
         "QNX_GUEST_INITIAL_STATE_PIN");
    const auto installed = install_qnx_guest_modules(root, adapter, module_pins(adapter), offline);
    need(std::filesystem::create_directory(output), "QNX_GUEST_FRESH_OUTPUT");
    ensure_private_directory(output);
    std::map<std::string, std::string> modules;
    Json pins = Json::object();

    for (const auto &module : qnx_guest_module_files(adapter)) {
        modules[module] = read(installed / module);
        pins[module] = module_pins(adapter).at("files").at(module).at("sha256");
        need(sha256_bytes(modules.at(module)) == pins.at(module).get<std::string>(),
             "QNX_GUEST_MODULES");
    }

    const std::string observer_name = passion ? "bounded.so" : "observer.so";
    const std::vector<std::pair<std::string, std::string_view>> guest_libc = {{"libc.so.4", libc}},
                                                                guest_screen = {
                                                                        {"libc.so.4", libc},
                                                                        {"libscreen.so.1", screen}};
    verify_qnx_link_imports(modules.at("front.so"), guest_screen);
    verify_qnx_link_imports(modules.at(observer_name), guest_libc);
    verify_qnx_link_imports(modules.at("virtual_wfd.so"), guest_libc);

    if (passion) {
        verify_qnx_link_imports(modules.at("ram64.so"), guest_libc);
    }

    Json sysmgrtool_compat = nullptr;

    if (qinggan09) {
        modules.at("front.so") = qnx_qinggan09_front_oem_data(
                modules.at("front.so"), startup_baseline, sysmgrtool_original);
        sysmgrtool_compat = {{"original_sha256", sha256_bytes(sysmgrtool_original)},
                             {"original_bytes", sysmgrtool_original.size()},
                             {"packed_bytes", sysmgr_tool_pack(sysmgrtool_original).size()},
                             {"oem_section_bytes", qnx_qinggan09_oem_capacity},
                             {"launcher_kind", "fixed-oem-sh"},
                             {"argv0", "retained-oem-path"},
                             {"namespace_path", "/usr/bin/sysmgrtool"},
                             {"process_scoped", true}};
    }

    for (const auto &[module, bytes] : modules) {
        write(output / module, bytes);
    }

    const auto &front = modules.at("front.so"), &observer = modules.at(observer_name);
    need(front.size() + observer.size() <= 360000 && observer.size() <= (passion ? 21313 : 16928),
         "QNX_GUEST_SLOT_CAPACITY");
    const auto bundle = passion ? front : front + observer;
    write(output / "front-bundle.so", bundle);
    Json receipt = {
            {"schema", 1},
            {"hardware_adapter", qnx_guest_adapter_name(adapter)},
            {"source_generated", true},
            {"modules", pins},
            {"modules_digest", module_digest(adapter, module_pins(adapter))},
            {"simulated_initial_native_state",
             qinggan11 ? qnx_qinggan11_simulated_initial_state(initial_native_state,
                                                               qnx_guest_adapter_name(adapter))
                       : Json(nullptr)},
            {"mcu_projection", qinggan11 ? qnx_mcu_projection(adapter) : Json(nullptr)},
            {"oem_libc_sha256", sha256_bytes(libc)},
            {"oem_screen_sha256", sha256_bytes(screen)},
            {"startup_baseline_sha256",
             startup_baseline.empty() ? Json(nullptr) : Json(sha256_bytes(startup_baseline))},
            {"startup_baseline_bytes", startup_baseline.size()},
            {"front_sha256", sha256_bytes(front)},
            {"observer_sha256", sha256_bytes(observer)},
            {"bundle_sha256", sha256_bytes(bundle)},
            {"virtual_wfd_sha256", pins.at("virtual_wfd.so")},
            {"observer_offset", passion ? 0 : front.size()},
            {"ram64_sha256", passion ? pins.at("ram64.so") : Json(nullptr)},
            {"auxiliary_kind", passion ? "bounded-native-carrier" : "mcd-observer"},
            {"readstub_sha256", pins.at("readstub.bin")},
            {"ipl_sha256", pins.at("qnx-ipl-virt.bin")},
            {"restore_sha256", pins.at("qnx-ifs2-restore.bin")},
            {"passion_ifs2_copy_sha256", passion ? pins.at("qnx-passion-ifs2-copy.bin") : Json("")},
            {"pl011_console", qinggan11 || passion},
            {"pl011_console_sha256",
             (qinggan11 || passion) ? pins.at("qnx-pl011-console.bin") : Json("")},
            {"pl011_console_bytes", (qinggan11 || passion) ? 112 : 0},
            {"boot_storage_queries", qinggan11 || passion},
            {"restore_prefix_sha256",
             sha256_bytes(std::string_view(modules.at("qnx-ifs2-restore.bin"))
                                  .substr(0, (qinggan11 || passion) ? 115 : std::string::npos))},
            {"guest_booted", false},
            {"exported_frame_verified", false}};

    if (!sysmgrtool_compat.is_null()) {
        receipt["sysmgrtool_compat"] = sysmgrtool_compat;
    }

    write_private_json(output / "source-provenance.json", receipt);

    return receipt;
}
} // namespace voyemu
