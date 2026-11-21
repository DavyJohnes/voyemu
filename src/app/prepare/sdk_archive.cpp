#include "sdk_archive.hpp"
#include "check.hpp"
#include "disk_space.hpp"
#include "error.hpp"
#include "interruption.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include "zip_archive.hpp"
#include "zip_layout.hpp"
#include <array>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <utility>
namespace voyemu {
namespace {
std::string safe_path(std::string name) {
    if (name.empty() || name.size() > 2048 || name.front() == '/' ||
        name.find_first_of("\\:") != std::string::npos || name.find("//") != std::string::npos) {
        throw VoyemuError("SDK_PATH", "unsafe archive path");
    }

    for (unsigned char c : name) {
        if (c < 32 || c == 127) {
            throw VoyemuError("SDK_PATH", "control character");
        }
    }

    if (name.back() == '/') {
        name.pop_back();
    }

    const std::u8string utf8(name.begin(), name.end());
    const std::filesystem::path path(utf8);

    if (path.empty() || path.is_absolute() || path.lexically_normal() != path) {
        throw VoyemuError("SDK_PATH", "normalized relative UTF8 path required");
    }

    for (const auto &part : path) {
        if (part == "." || part == ".." || part.empty()) {
            throw VoyemuError("SDK_PATH", "invalid component");
        }
    }

    return name;
}

bool omit(const std::string &name) {
    const auto path = std::filesystem::path(name);

    for (const auto &part : path) {
        const auto text = part.string();

        if (text == "python3" || text.starts_with("python3.") || text == "python-packages") {
            return true;
        }
    }

    return path.extension() == ".jar" || path.extension() == ".py" || path.filename() == "d8" ||
           path.filename() == "d8.bat" || path.filename() == "apksigner" ||
           path.filename() == "apksigner.bat";
}
} // namespace

Json extract_sdk_archive(const std::filesystem::path &file, const std::string &pin,
                         const std::filesystem::path &destination, std::uint64_t reserve,
                         std::uint64_t maximum, bool strip_root) {
    if (!sha256_text(pin) || !maximum || maximum > (16ULL << 30)) {
        throw VoyemuError("SDK_PIN", "bounded lowercase archive SHA256 required");
    }

    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(file)) ||
        std::filesystem::hard_link_count(file) != 1) {
        throw VoyemuError("SDK_INPUT", "ordinary unaliased archive required");
    }

    const auto epoch = cache_file_identity(file);

    if (sha256_file(file) != pin) {
        throw VoyemuError("SDK_SHA", "archive differs from pin");
    }

    // Case aliases, as in the Linux NDK sysroot, extract only to a case-sensitive volume.
    const auto layout = zip_layout(file, ZipNamePolicy::exact);

    if (!destination.is_absolute() || destination.lexically_normal() != destination ||
        std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        throw VoyemuError("SDK_OUTPUT", "fresh absolute destination required");
    }

    std::string wrapper;
    std::uint64_t expanded = 0;
    unsigned omitted = 0;
    std::vector<ZipTreeEntry> selected;
    std::vector<std::filesystem::path> directories, executables;
    // Link members become copies of their final target, as on the libarchive path.
    std::map<std::string, std::string> links;
    std::map<std::string, std::uint64_t> regular;
    const auto targets = pinned_zip_link_targets(file, pin);

    for (const auto &member : layout) {
        if (std::any_of(member.name.begin(), member.name.end(), [](unsigned char c) {
                return c >= 128;
            })) {
            throw VoyemuError("SDK_PATH", "SDK package paths must be unambiguous ASCII");
        }

        if (member.bytes > maximum - expanded) {
            throw VoyemuError("SDK_BOUND", "complete extraction exceeds bound");
        }

        expanded += member.bytes;
        const auto slash = member.name.find('/');

        if (strip_root) {
            const auto top = member.name.substr(0, slash);

            if (wrapper.empty()) {
                wrapper = top;
            } else if (wrapper != top) {
                throw VoyemuError("SDK_ROOT", "one wrapper required");
            }

            if (slash == std::string::npos && !member.directory) {
                throw VoyemuError("SDK_ROOT", "directory wrapper required");
            }
        }

        auto name = strip_root ? (slash == std::string::npos ? std::string{}
                                                             : member.name.substr(slash + 1))
                               : member.name;

        if (name.ends_with('/')) {
            name.pop_back();
        }

        if (name.empty()) {
            continue;
        }

        safe_path(name);

        if (omit(name)) {
            ++omitted;
            continue;
        }

        const auto path = destination /
                          std::filesystem::path(std::u8string(name.begin(), name.end()));

        if (member.directory) {
            directories.push_back(path);
        } else if (member.symlink) {
            const auto &link = targets.at(member.name);

            if (link.empty() || link.size() > 2048 || link.front() == '/' ||
                link.find_first_of("\\:\r\n") != std::string::npos) {
                throw VoyemuError("SDK_LINK", "relative component alias required");
            }

            links.emplace(name, safe_path((std::filesystem::path(name).parent_path() / link)
                                                  .lexically_normal()
                                                  .generic_string()));
        } else {
            selected.push_back({member.name, path, member.bytes});
            regular.emplace(name, member.bytes);

            if (member.mode & 0111) {
                executables.push_back(path);
            }
        }
    }

    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> copies;

    for (const auto &[name, link] : links) {
        std::set<std::string> seen;
        auto target = link;

        while (links.contains(target)) {
            if (!seen.insert(target).second) {
                throw VoyemuError("SDK_LINK", "missing cyclic or excluded target");
            }

            target = links.at(target);
        }

        if (!regular.contains(target)) {
            throw VoyemuError("SDK_LINK", "missing cyclic or excluded target");
        }

        if (regular.at(target) > maximum - expanded) {
            throw VoyemuError("SDK_BOUND", "materialized aliases exceed maximum");
        }

        expanded += regular.at(target);
        copies.emplace_back(destination / target, destination / name);
    }

    require_disk_space("SDK_SPACE", destination.parent_path(), expanded, reserve);

    ensure_private_directory(destination.parent_path());

    if (!std::filesystem::create_directory(destination)) {
        throw VoyemuError("SDK_OUTPUT", "exclusive directory creation failed");
    }

    const auto token = random_token();
    write_private_json(destination / ".extract-owner.json", {{"token", token}});
    struct Cleanup {
        std::filesystem::path path;
        std::string token;
        bool published = false;

        ~Cleanup() {
            try {
                if (!published &&
                    read_private_json(path / ".extract-owner.json").at("token") == token) {
                    std::filesystem::remove_all(path);
                }
            } catch (...) { /* Ignored: cleanup of owned temporary state is best-effort. */
            }
        }
    } cleanup{destination, token};

    for (const auto &path : directories) {
        std::filesystem::create_directories(path);
    }

    std::vector<ZipTreeEntry> files;
    files.reserve(selected.size());
    std::uint64_t bytes = 0;

    for (const auto &entry : selected) {
        std::filesystem::create_directories(entry.output.parent_path());
        files.push_back(entry);
        bytes += entry.bytes;
    }

    extract_pinned_zip_tree(file, pin, files, reserve, ZipNamePolicy::exact);

    for (const auto &path : executables) {
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_exec |
                                             std::filesystem::perms::group_exec |
                                             std::filesystem::perms::others_exec,
                                     std::filesystem::perm_options::add);
    }

    for (const auto &[from, to] : copies) {
        std::filesystem::create_directories(to.parent_path());

        if (!std::filesystem::copy_file(from, to, std::filesystem::copy_options::none)) {
            throw VoyemuError("SDK_LINK", "alias materialization failed");
        }

        bytes += std::filesystem::file_size(to);
    }

    if (cache_file_identity(file) != epoch || sha256_file(file) != pin) {
        throw VoyemuError("SDK_CHANGED", "source differs");
    }

    std::filesystem::remove(destination / ".extract-owner.json");
    cleanup.published = true;

    return {{"files", files.size() + copies.size()},
            {"bytes", bytes},
            {"omitted", omitted},
            {"archive_sha256", pin},
            {"native", true},
            {"backend", "pinned ZIP"}};
}
} // namespace voyemu
