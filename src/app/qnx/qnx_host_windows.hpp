#pragma once
#include "error.hpp"
#include "model.hpp"
#include "process.hpp"
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
namespace voyemu {
// model.hpp provides read_private_json/write_private_json/ensure_private_directory; process.hpp
// provides sha256_bytes/sha256_file/random_token for these host-side validations. Builds the pinned
// Windows ARM64 QEMU host from source with the pinned MSYS/Zig closure; never changes guest images.
Json install_qnx_host_windows_source(const std::filesystem::path &, bool, std::uint64_t);

// Shared host-side validations for the source engine install: used by the Windows builder itself
// and by the focused regression tests on both supported hosts.
inline Json qnx_windows_source_payload_spec(const Json &recipe) {
    return recipe.at("verified_runtime").at("files");
}

// Maps a pinned runtime closure relative to its source inside the staged ucrt64 tree: license files
// live under share/licenses with the "licenses/" prefix (9 bytes) removed; every other runtime file
// lives in bin.
inline std::filesystem::path qnx_windows_source_file(const std::filesystem::path &msys,
                                                     const std::string &relative) {
    return std::string_view(relative).starts_with("licenses/")
                   ? msys / "ucrt64/share/licenses" / relative.substr(9)
                   : msys / "ucrt64/bin" / relative;
}

// Validates an installed source engine directory against the exact current recipe: recipe identity
// in the receipt, the complete engine/DLL/license closure with no extras, no linked payload or
// linked ancestors, ordinary single-link files, receipt hashes matching pins, the receipt engine
// hash matching the actual published engine. This checks recipe and receipt integrity, not engine
// provenance against an actor who can replace both the engine and its receipt. The engine hash is
// build-output-dependent.
inline void validate_installed_windows_source(const std::filesystem::path &directory,
                                              const std::filesystem::path &root,
                                              const Json &recipe) {
    const auto throw_invalid = [&](const std::string &message) {
        throw VoyemuError("QNX_HOST", message);
    };
    const auto pin = sha256_bytes(recipe.dump());
    const auto receipt = read_private_json(directory / "qnx-host.json", 8 << 20);

    if (receipt.value("component_sha256", std::string()) != pin ||
        receipt.value("recipe", Json()) != recipe) {
        throw_invalid("installed source recipe differs");
    }

    if (!receipt.contains("executable_sha256")) {
        throw_invalid("installed source executable receipt differs");
    }

    auto ancestor = root;

    for (const auto &part : directory.lexically_relative(root)) {
        ancestor /= part;

        if (std::filesystem::is_symlink(std::filesystem::symlink_status(ancestor))) {
            throw_invalid("linked source engine directory rejected");
        }
    }

    const auto &runtime = qnx_windows_source_payload_spec(recipe);
    const auto &files = receipt.at("files");

    if (!files.is_object() || files.size() != runtime.size()) {
        throw_invalid("installed source closure differs " + std::to_string(files.size()) + "/" +
                      std::to_string(runtime.size()));
    }

    std::set<std::string> expect;

    for (const auto &name : runtime.items()) {
        expect.insert(name.key());
    }

    if (expect.size() != runtime.size() || expect.find("qemu-system-aarch64.exe") == expect.end()) {
        throw_invalid("pinned runtime closure differs");
    }

    for (const auto &[relative, spec] : runtime.items()) {
        const std::filesystem::path name(relative);

        if (name.empty() || name.is_absolute() || name.lexically_normal() != name) {
            throw_invalid("installed source path differs: " + relative);
        }

        auto path = directory;

        for (const auto &part : name) {
            path /= part;

            if (part == "..") {
                throw_invalid("installed source path escapes");
            }

            if (std::filesystem::is_symlink(std::filesystem::symlink_status(path))) {
                throw_invalid("linked source engine payload rejected: " + relative);
            }
        }

        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) ||
            std::filesystem::hard_link_count(path) != 1) {
            throw_invalid("ordinary installed source payload required: " + relative);
        }

        if (!files.contains(relative)) {
            throw_invalid("installed source receipt file missing: " + relative);
        }

        if (files.at(relative).at("bytes").get<std::uint64_t>() !=
            std::filesystem::file_size(path)) {
            throw_invalid("installed source payload extent differs: " + relative);
        }

        if (relative == "qemu-system-aarch64.exe") {
            if (files.at(relative).at("sha256").get<std::string>() != sha256_file(path) ||
                receipt.at("executable_sha256").get<std::string>() != sha256_file(path)) {
                throw_invalid("installed source engine differs from receipt");
            }
        } else if (files.at(relative).at("sha256").get<std::string>() !=
                           spec.at("sha256").get<std::string>() ||
                   sha256_file(path) != spec.at("sha256").get<std::string>()) {
            throw_invalid("installed source payload pin differs: " + relative);
        }
    }

    std::set<std::string> remaining = expect;
    unsigned ordinary_entries = 0;

    for (const auto &entry : std::filesystem::recursive_directory_iterator(directory)) {
        if (entry.is_symlink()) {
            throw_invalid("linked source engine entry rejected");
        }

        if (entry.is_directory()) {
            continue;
        }

        const auto relative = entry.path().lexically_relative(directory).generic_string();

        if (relative == "qnx-host.json") {
            ordinary_entries++;
            continue;
        }

        if (remaining.erase(relative) != 1) {
            throw_invalid("unexpected installed source payload: " + relative);
        }

        ordinary_entries++;
    }

    if (!remaining.empty() || ordinary_entries != runtime.size() + 1) {
        throw_invalid("installed source closure inventory differs");
    }
}

// 7z decodes GNU long-name/link and PAX records. Parse its bounded technical listing instead of tar
// bytes. Return archive names for exact extraction exclusions; records use paths relative to the
// MSYS root.
inline std::vector<std::string> record_msys_archive_links(const std::string &listing,
                                                          const std::filesystem::path &records,
                                                          const std::string &prefix = "",
                                                          bool omit_unused_qemu_links = false) {
    auto fail = []() {
        throw VoyemuError("QNX_HOST", "invalid bounded MSYS archive listing");
    };

    if (listing.empty() || listing.size() > (32ULL << 20)) {
        fail();
    }

    // Windows 7z renders member and link paths with native separators. Normalize only for
    // root-relative validation/materialization; exact extraction exclusions retain the original
    // Path field below.
    auto separators = [&](std::string name) {
        std::replace(name.begin(), name.end(), '\\', '/');

        if (name.starts_with("//") || name.find(':') != std::string::npos) {
            fail();
        }

        return name;
    };
    auto clean = [&](std::string name) {
        name = separators(std::move(name));

        while (name.starts_with("./")) {
            name.erase(0, 2);
        }

        if (name.ends_with('/')) {
            name.pop_back();
        }

        if (name.empty() || name.size() > 4096 || name.front() == '/' ||
            name.find_first_of("\\:\t\r\n") != std::string::npos) {
            fail();
        }

        for (unsigned char c : name) {
            if (c < 32 || c == 127) {
                fail();
            }
        }

        const std::filesystem::path path(name);

        if (path.lexically_normal() != path) {
            fail();
        }

        for (const auto &part : path) {
            if (part == ".." || part == ".") {
                fail();
            }
        }

        return name;
    };
    auto strip = [&](const std::string &name) {
        if (prefix.empty()) {
            return name;
        }

        if (name == prefix) {
            return std::string{};
        }

        if (!name.starts_with(prefix + "/")) {
            fail();
        }

        return name.substr(prefix.size() + 1);
    };
    std::istringstream input(listing);
    std::map<std::string, std::string> fields;
    std::set<std::string> names;
    std::vector<std::string> exclusions;
    std::ostringstream output;
    std::string line;
    unsigned count = 0;
    std::uint64_t bytes = 0;
    auto entry = [&] {
        if (fields.empty()) {
            return;
        }

        if (!fields.contains("Path") || !fields.contains("Folder") || !fields.contains("Size") ||
            ++count > 100000) {
            fail();
        }

        const auto source = clean(fields.at("Path")), name = strip(source);

        if (!names.insert(source).second) {
            fail();
        }

        std::size_t used = 0;
        const auto size = std::stoull(fields.at("Size"), &used);

        if (used != fields.at("Size").size() || size > (2ULL << 30) - bytes) {
            fail();
        }

        bytes += size;
        const auto sym = fields["Symbolic Link"], hard = fields["Hard Link"];

        if (!sym.empty() && !hard.empty()) {
            fail();
        }

        if (!sym.empty() || !hard.empty()) {
            if (name.empty()) {
                fail();
            }

            auto target = separators(sym.empty() ? hard : sym);

            if (target.empty() || target.size() > 4096 ||
                target.find_first_of("\\:\t\r\n") != std::string::npos) {
                fail();
            }

            for (unsigned char c : target) {
                if (c < 32 || c == 127) {
                    fail();
                }
            }

            // This build does not compile firmware, Rust, CI fixtures or Linux vhost backends.
            if (omit_unused_qemu_links &&
                (sym.empty() || !(name.starts_with("roms/") || name.starts_with("rust/") ||
                                  name.starts_with("tests/lcitool/") ||
                                  name.starts_with("subprojects/libvduse/") ||
                                  name.starts_with("subprojects/libvhost-user/")))) {
                fail();
            }

            // Hard-link names are archive-root-relative; symbolic link names are parent-relative or
            // MSYS absolute.
            if (sym.empty()) {
                target = strip(clean(target));
            }

            exclusions.push_back(fields.at("Path"));
            output << (sym.empty() ? "H" : "S") << '\t' << name << '\t' << target << '\n';
        } else if (fields.contains("Mode") && !fields.at("Mode").empty() &&
                   fields.at("Mode")[0] != '-' && fields.at("Mode")[0] != 'd') {
            fail();
        }

        fields.clear();
    };

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            entry();
            continue;
        }

        const auto equal = line.find(" = ");

        if (equal == std::string::npos ||
            !fields.emplace(line.substr(0, equal), line.substr(equal + 3)).second) {
            fail();
        }
    }

    entry();

    if (!count) {
        fail();
    }

    std::ofstream file(records, std::ios::binary | std::ios::trunc);
    file << output.str();
    file.close();

    if (!file) {
        throw VoyemuError("QNX_HOST", "symlink record write failed");
    }

    return exclusions;
}

// Ordinary copies give MSYS executable aliases the same representation with and without Windows
// link privilege, and keep the bootstrap inventory free of native reparse points and multiply
// linked files.
inline unsigned restore_msys_symlinks(const std::filesystem::path &directory,
                                      const std::filesystem::path &records) {
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(records))) {
        throw VoyemuError("QNX_HOST", "symlink record missing");
    }

    if (std::filesystem::file_size(records) > (16ULL << 20)) {
        throw VoyemuError("QNX_HOST", "symlink record exceeds bound");
    }

    const auto fail = [](const std::string &text) {
        throw VoyemuError("QNX_HOST", "symlink record " + text);
    };
    auto safe = [&](const std::filesystem::path &path) {
        if (path.empty() || path.is_absolute() || path.lexically_normal() != path) {
            fail("path differs");
        }

        for (const auto &part : path) {
            if (part == ".." || part == ".") {
                fail("target escapes");
            }
        }

        auto at = directory;

        for (const auto &part : path) {
            at /= part;
            std::error_code error;

            if (std::filesystem::is_symlink(std::filesystem::symlink_status(at, error))) {
                fail("linked ancestor rejected");
            }
        }
    };
    std::ifstream input(records);
    std::string line;
    std::map<std::string, std::filesystem::path> links;

    while (std::getline(input, line)) {
        const auto first = line.find('\t'),
                   second = first == std::string::npos ? first : line.find('\t', first + 1);

        if (first != 1 || second == std::string::npos || (line[0] != 'S' && line[0] != 'H')) {
            fail("line differs");
        }

        const auto name = line.substr(2, second - 2), target = line.substr(second + 1);
        const std::filesystem::path relative(name);
        safe(relative);

        if (target.empty() || target.find_first_of("\\:\t\r\n") != std::string::npos) {
            fail("target differs");
        }

        const auto resolved = (line[0] == 'H'     ? std::filesystem::path(target)
                               : target[0] == '/' ? std::filesystem::path(target).relative_path()
                                                  : relative.parent_path() / target)
                                      .lexically_normal();
        safe(resolved);

        if (links.size() >= 100000 || !links.emplace(name, resolved).second) {
            fail("duplicate or excessive links");
        }
    }

    if (!input.eof()) {
        fail("read failed");
    }

    input.clear();
    input.close();

    if (input.fail()) {
        fail("close failed");
    }

    std::set<std::string> active, done;
    std::uint64_t copied = 0;
    unsigned copied_entries = 0;
    std::function<void(const std::string &)> materialize;
    std::function<void(const std::filesystem::path &, const std::filesystem::path &)> copy;
    copy = [&](const auto &from, const auto &to) {
        if (++copied_entries > 100000) {
            fail("materialized entry bound exceeded");
        }

        const auto state = std::filesystem::symlink_status(from);

        if (std::filesystem::is_symlink(state)) {
            fail("linked target rejected");
        }

        if (std::filesystem::is_regular_file(state)) {
            const auto bytes = std::filesystem::file_size(from);

            if (bytes > (2ULL << 30) - copied) {
                fail("materialized byte bound exceeded");
            }

            copied += bytes;

            if (!std::filesystem::copy_file(from, to)) {
                fail("copy failed");
            }
        } else if (std::filesystem::is_directory(state)) {
            if (!std::filesystem::create_directory(to)) {
                fail("directory destination differs");
            }

            for (const auto &item : std::filesystem::directory_iterator(from)) {
                copy(item.path(), to / item.path().filename());
            }
        } else {
            fail("target missing");
        }
    };
    materialize = [&](const std::string &name) {
        if (done.contains(name)) {
            return;
        }

        if (active.size() >= 256 || !active.insert(name).second) {
            fail("cyclic links");
        }

        const auto relative = links.at(name);
        const auto target = relative.generic_string();

        // Resolve aliases used as a target or as one of its parent components before touching the
        // filesystem.
        for (const auto &[other, unused] : links) {
            if (target == other || target.starts_with(other + "/")) {
                materialize(other);
            }
        }

        const auto source = directory / relative, destination = directory / name;

        if (std::filesystem::is_directory(std::filesystem::symlink_status(source))) {
            if (name.starts_with(target + "/") || target.starts_with(name + "/")) {
                fail("cyclic directory links");
            }

            // Directory copies include all recorded aliases below their source tree.
            for (const auto &[other, unused] : links) {
                if (other.starts_with(target + "/")) {
                    materialize(other);
                }
            }
        }

        safe(relative);
        safe(std::filesystem::path(name));
        std::error_code error;
        const auto state = std::filesystem::symlink_status(destination, error);

        if (std::filesystem::exists(state)) {
            if (!std::filesystem::is_regular_file(state)) {
                fail("destination differs");
            }

            std::filesystem::remove(destination);
        }

        ensure_private_directory(destination.parent_path());
        copy(source, destination);
        active.erase(name);
        done.insert(name);
    };

    for (const auto &[name, unused] : links) {
        materialize(name);
    }

    if (!std::filesystem::remove(records)) {
        fail("removal failed");
    }

    return static_cast<unsigned>(done.size());
}
} // namespace voyemu
