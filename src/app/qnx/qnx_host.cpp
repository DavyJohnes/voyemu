#include "qnx_host.hpp"
#include "check.hpp"
#include "error.hpp"
#include "little_endian.hpp"
#include "process.hpp"
#include "qnx_host_internal.hpp"
#include "qnx_persistent_storage.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <fstream>
#include <set>
namespace voyemu {
namespace {
#include "qnx_host_catalog.inc"

constexpr Check need{"QNX_HOST"};
} // namespace

const Json &qnx_host_catalog() {
    static const Json value = Json::parse(std::begin(qnx_host_json), std::end(qnx_host_json));

    return value;
}

std::vector<std::string> qnx_host_elf_needed(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    const auto read = [&](std::uint64_t at, unsigned size) {
        return le_read(bytes, at, size, need, "ELF extent");
    };

    need(bytes.size() > 64 &&
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

    need(strtab && strsz, "ELF string table missing");
    const auto table = offset(strtab);

    for (const auto value : needed) {
        need(value < strsz, "ELF string extent");
        const auto at = table + value;
        const auto end = bytes.find('\0', at);
        need(end != std::string::npos && end < table + strsz, "ELF string terminator");
        names.push_back(bytes.substr(at, end - at));
    }

    return names;
}

bool qnx_host_managed_windows_bindings(const Json &value) {
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

QnxHostBindingFile read_qnx_host_binding_file(const std::filesystem::path &file) {
    QnxHostBindingFile read;

    read.identity = cache_file_identity(file);
    read.bytes = read_private_json_bytes(file, 128 * 1024);
    read.pin = sha256_bytes(read.bytes);
    need(cache_file_identity(file) == read.identity && sha256_file(file) == read.pin,
         "host binding changed while reading");
    read.document = Json::parse(read.bytes);

    return read;
}

Json install_qnx_host(const std::filesystem::path &root, bool offline, std::uint64_t reserve) {
    need(root.is_absolute() && std::filesystem::canonical(root) == root,
         "canonical project root required");

    return install_qnx_host_engine(root, offline, reserve);
}
} // namespace voyemu
