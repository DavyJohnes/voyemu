#include "sdk_archive.hpp"
#include "check.hpp"
#include "disk_space.hpp"
#include "error.hpp"
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
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#ifdef __APPLE__
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
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

#ifdef __APPLE__
struct Reader {
    archive *value = archive_read_new();

    explicit Reader(const std::filesystem::path &file) {
        if (!value || archive_read_support_format_zip(value) != ARCHIVE_OK ||
            archive_read_open_filename(value, file.c_str(), 65536) != ARCHIVE_OK) {
            if (value) {
                archive_read_free(value);
            }

            value = nullptr;
            throw VoyemuError("SDK_ZIP", "cannot open native ZIP reader");
        }
    }

    ~Reader() {
        if (value) {
            archive_read_free(value);
        }
    }

    Reader(const Reader &) = delete;
    Reader &operator=(const Reader &) = delete;
};
struct Entry {
    std::string source, name, link;
    std::uint64_t bytes{};
    bool directory = false, omitted = false, executable = false;
};

std::map<std::string, Entry> inventory(const std::filesystem::path &file, std::uint64_t maximum,
                                       bool strip_root, bool full = false) {
    Reader reader(file);
    archive_entry *header = nullptr;
    std::map<std::string, Entry> entries;
    std::set<std::string> folded;
    std::uint64_t total = 0;
    std::string root;

    for (;;) {
        const auto code = archive_read_next_header(reader.value, &header);

        if (code == ARCHIVE_EOF) {
            break;
        }

        if (code != ARCHIVE_OK) {
            throw VoyemuError("SDK_ZIP", "invalid entry header");
        }

        const auto *raw = archive_entry_pathname_utf8(header);

        if (!raw) {
            throw VoyemuError("SDK_PATH", "UTF8 name required");
        }

        const auto source = safe_path(raw);
        const auto slash = source.find('/');
        const auto wrapper = source.substr(0, slash);

        if (strip_root && root.empty()) {
            root = wrapper;
        } else if (strip_root && root != wrapper) {
            throw VoyemuError("SDK_ROOT", "one top-level component required");
        }

        const auto type = archive_entry_filetype(header);

        if (archive_entry_is_encrypted(header) || archive_entry_hardlink(header) ||
            (type != AE_IFREG && type != AE_IFDIR && type != AE_IFLNK) ||
            !archive_entry_size_is_set(header) || archive_entry_size(header) < 0) {
            throw VoyemuError("SDK_TYPE", "unsupported encrypted or special entry");
        }

        if (strip_root && slash == std::string::npos) {
            if (type != AE_IFDIR) {
                throw VoyemuError("SDK_ROOT", "directory wrapper required");
            }

            continue;
        }

        const auto name = strip_root ? safe_path(source.substr(slash + 1)) : source;
        auto lower = name;

        for (char &c : lower) {
            if (c >= 'A' && c <= 'Z') {
                c += 32;
            }
        }

        if (entries.size() >= 100000 || !folded.insert(lower).second) {
            throw VoyemuError("SDK_DUPLICATE", "duplicate or case-aliased path");
        }

        Entry item{source,
                   name,
                   "",
                   static_cast<std::uint64_t>(archive_entry_size(header)),
                   type == AE_IFDIR,
                   !full && omit(name),
                   (archive_entry_perm(header) & 0111) != 0};

        if (type == AE_IFLNK) {
            const auto *target = archive_entry_symlink_utf8(header);

            if (!target) {
                throw VoyemuError("SDK_LINK", "UTF8 link target required");
            }

            std::string link(target);

            if (link.empty() || link.size() > 2048 || link.front() == '/' ||
                link.find_first_of("\\:\r\n") != std::string::npos) {
                throw VoyemuError("SDK_LINK", "relative component alias required");
            }

            item.link = safe_path((std::filesystem::path(name).parent_path() / link)
                                          .lexically_normal()
                                          .generic_string());
            item.bytes = 0;
        }

        if (!item.omitted) {
            if (item.bytes > maximum - total) {
                throw VoyemuError("SDK_BOUND", "expanded archive exceeds maximum");
            }

            total += item.bytes;
        }

        entries.emplace(name, std::move(item));

        if (archive_read_data_skip(reader.value) != ARCHIVE_OK) {
            throw VoyemuError("SDK_ZIP", "invalid entry data");
        }
    }

    if (entries.empty()) {
        throw VoyemuError("SDK_ROOT", "empty component");
    }

    for (const auto &[name, item] : entries) {
        for (auto parent = std::filesystem::path(name).parent_path(); !parent.empty();
             parent = parent.parent_path()) {
            if (entries.contains(parent.generic_string()) &&
                !entries.at(parent.generic_string()).directory) {
                throw VoyemuError("SDK_PATH", "file or alias used as parent");
            }
        }

        if (!item.link.empty() && !item.omitted) {
            std::set<std::string> seen;
            auto target = item.link;

            while (true) {
                if (!seen.insert(target).second || !entries.contains(target) ||
                    entries.at(target).omitted || entries.at(target).directory) {
                    throw VoyemuError("SDK_LINK", "missing cyclic or excluded target");
                }

                const auto &next = entries.at(target);

                if (next.link.empty()) {
                    if (!full) {
                        if (next.bytes > maximum - total) {
                            throw VoyemuError("SDK_BOUND", "materialized aliases exceed maximum");
                        }

                        total += next.bytes;
                    }

                    break;
                }

                target = next.link;
            }
        }
    }

    return entries;
}
#endif
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

#ifdef __APPLE__
    const auto entries = inventory(file, maximum, strip_root);
    std::uint64_t bytes = 0, expanded = 0;
    unsigned count = 0, omitted = 0;

    // Links become copies of their final target and take its size, as below.
    for (const auto &[name, item] : entries) {
        if (item.omitted || item.directory) {
            continue;
        }

        auto target = name;

        while (!entries.at(target).link.empty()) {
            target = entries.at(target).link;
        }

        expanded += entries.at(target).bytes;
    }

    require_disk_space("SDK_SPACE", destination.parent_path(), expanded, reserve);

    ensure_private_directory(destination.parent_path());

    if (!std::filesystem::create_directory(destination)) {
        throw VoyemuError("SDK_OUTPUT", "exclusive directory creation failed");
    }

    struct stat identity{};

    if (::lstat(destination.c_str(), &identity) || !S_ISDIR(identity.st_mode) ||
        identity.st_uid != ::geteuid()) {
        throw VoyemuError("SDK_OUTPUT", "owned directory identity required");
    }

    struct Cleanup {
        std::filesystem::path path;
        struct stat identity;
        bool published = false;

        ~Cleanup() {
            if (published) {
                return;
            }

            try {
                struct stat now{};

                if (!::lstat(path.c_str(), &now) && S_ISDIR(now.st_mode) &&
                    now.st_dev == identity.st_dev && now.st_ino == identity.st_ino) {
                    std::filesystem::remove_all(path);
                }
            } catch (...) { /* Ignored: owned staging cleanup must not escape its destructor. */
            }
        }
    } cleanup{destination, identity};
    Reader reader(file);
    archive_entry *header = nullptr;

    for (;;) {
        const auto code = archive_read_next_header(reader.value, &header);

        if (code == ARCHIVE_EOF) {
            break;
        }

        if (code != ARCHIVE_OK) {
            throw VoyemuError("SDK_ZIP", "extraction header failed");
        }

        const auto *raw = archive_entry_pathname_utf8(header);

        if (!raw) {
            throw VoyemuError("SDK_PATH", "entry changed");
        }

        const auto source = safe_path(raw);
        const auto slash = source.find('/');

        if (strip_root && slash == std::string::npos) {
            archive_read_data_skip(reader.value);
            continue;
        }

        const auto name = strip_root ? source.substr(slash + 1) : source;
        const auto found = entries.find(name);

        if (found == entries.end() || found->second.source != source) {
            throw VoyemuError("SDK_CHANGED", "entry changed between passes");
        }

        const auto &item = found->second;

        if (item.omitted) {
            ++omitted;

            if (archive_read_data_skip(reader.value) != ARCHIVE_OK) {
                throw VoyemuError("SDK_ZIP", "excluded data skip failed");
            }

            continue;
        }

        const auto output = destination /
                            std::filesystem::path(std::u8string(name.begin(), name.end()));

        if (item.directory) {
            std::filesystem::create_directories(output);
            continue;
        }

        if (!item.link.empty()) {
            continue;
        }

        std::filesystem::create_directories(output.parent_path());
        const auto fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC,
                               item.executable ? 0755 : 0644);

        if (fd < 0) {
            throw VoyemuError("SDK_WRITE", "exclusive output failed");
        }

        struct File {
            int fd;

            ~File() {
                if (fd >= 0) {
                    ::close(fd);
                }
            }
        } opened{fd};
        std::uint64_t written = 0;
        std::array<char, 65536> buffer{};

        for (;;) {
            const auto got = archive_read_data(reader.value, buffer.data(), buffer.size());

            if (got < 0) {
                throw VoyemuError("SDK_CRC", "invalid entry bytes");
            }

            if (!got) {
                break;
            }

            const auto size = static_cast<std::size_t>(got);

            if (size > item.bytes - written) {
                throw VoyemuError("SDK_BOUND", "entry expanded beyond header");
            }

            std::size_t offset = 0;

            while (offset < size) {
                const auto done = ::write(fd, buffer.data() + offset, size - offset);

                if (done <= 0) {
                    throw VoyemuError("SDK_WRITE", "incomplete entry write");
                }

                offset += done;
            }

            written += size;

            if (std::filesystem::space(destination).available < reserve) {
                require_disk_space("SDK_SPACE", destination, 0, reserve);
            }
        }

        if (written != item.bytes || ::fsync(fd)) {
            throw VoyemuError("SDK_WRITE", "entry extent or sync failed");
        }

        bytes += written;
        ++count;
    }

    for (const auto &[name, item] : entries) {
        if (!item.omitted && !item.link.empty()) {
            auto target = item.link;

            while (!entries.at(target).link.empty()) {
                target = entries.at(target).link;
            }

            const auto output = destination / name, input = destination / target;
            std::filesystem::create_directories(output.parent_path());

            if (!std::filesystem::copy_file(input, output, std::filesystem::copy_options::none)) {
                throw VoyemuError("SDK_LINK", "alias materialization failed");
            }

            bytes += std::filesystem::file_size(output);
            ++count;
        }
    }

    if (cache_file_identity(file) != epoch || sha256_file(file) != pin) {
        throw VoyemuError("SDK_CHANGED", "source changed during extraction");
    }

    cleanup.published = true;

    return {{"files", count},
            {"bytes", bytes},
            {"omitted", omitted},
            {"archive_sha256", pin},
            {"native", true}};
#else
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
#endif
}
} // namespace voyemu
