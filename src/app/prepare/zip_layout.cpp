#include "zip_layout.hpp"
#include "error.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
namespace voyemu {
namespace {
[[noreturn]] void bad(const char *message) {
    throw VoyemuError("ZIP_LAYOUT", message);
}

std::uint64_t number(std::string_view bytes, std::size_t offset, unsigned count) {
    if (offset > bytes.size() || count > bytes.size() - offset) {
        bad("truncated integer");
    }

    std::uint64_t value = 0;

    for (unsigned i = 0; i < count; ++i) {
        value |= std::uint64_t(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    }

    return value;
}

struct File {
    std::ifstream stream;
    std::uint64_t size;

    explicit File(const std::filesystem::path &file)
        : stream(file, std::ios::binary), size(std::filesystem::file_size(file)) {
        if (!stream || size < 22 || size > (16ULL << 30)) {
            bad("bounded ordinary ZIP required");
        }
    }

    std::string read(std::uint64_t offset, std::size_t length) {
        if (offset > size || length > size - offset || length > 16 * 1024 * 1024) {
            bad("read extent outside ZIP");
        }

        std::string bytes(length, '\0');
        stream.clear();
        stream.seekg(static_cast<std::streamoff>(offset));
        stream.read(bytes.data(), static_cast<std::streamsize>(length));

        if (static_cast<std::size_t>(stream.gcount()) != length) {
            bad("short ZIP read");
        }

        return bytes;
    }
};

void extra_paths(std::string_view extra, std::string_view filename,
                 bool alignment_padding = false) {
    for (std::size_t at = 0; at < extra.size();) {
        if (alignment_padding && std::all_of(extra.begin() + at, extra.end(), [](char byte) {
                return byte == 0;
            })) {
            return;
        }

        const auto tag = number(extra, at, 2), size = number(extra, at + 2, 2);
        at += 4;

        if (size > extra.size() - at) {
            bad("extra field extent invalid");
        }

        const auto payload = extra.substr(at, static_cast<std::size_t>(size));

        if (tag == 0x000d) {
            bad("Unix link-bearing extra metadata unsupported");
        }

        if (tag == 0x7075 &&
            (payload.size() < 5 || payload[0] != 1 || payload.substr(5) != filename)) {
            bad("alternate Unicode path differs from declared name");
        }

        at += static_cast<std::size_t>(size);
    }
}

void utf8(std::string_view value) {
    for (std::size_t at = 0; at < value.size();) {
        const auto first = static_cast<unsigned char>(value[at++]);

        if (first < 128) {
            continue;
        }

        unsigned count = 0, code = 0, minimum = 0;

        if (first >= 0xc2 && first <= 0xdf) {
            count = 1;
            code = first & 31;
            minimum = 128;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            code = first & 15;
            minimum = 2048;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            code = first & 7;
            minimum = 65536;
        } else {
            bad("invalid UTF8 path");
        }

        if (count > value.size() - at) {
            bad("truncated UTF8 path");
        }

        while (count--) {
            const auto next = static_cast<unsigned char>(value[at++]);

            if ((next & 0xc0) != 0x80) {
                bad("invalid UTF8 continuation");
            }

            code = (code << 6) | (next & 63);
        }

        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) {
            bad("noncanonical UTF8 path");
        }
    }
}

void name(std::string_view value) {
    utf8(value);

    if (value.empty() || value.size() > 2048 || value.front() == '/' ||
        value.find_first_of("\\:") != std::string_view::npos ||
        value.find("//") != std::string_view::npos) {
        bad("unsafe path");
    }

    for (unsigned char c : value) {
        if (c < 32 || c == 127) {
            bad("control in path");
        }
    }

    if (value.back() == '/') {
        value.remove_suffix(1);
    }

    for (std::size_t start = 0; start < value.size();) {
        const auto end = value.find('/', start);
        const auto part = value.substr(start, end == std::string_view::npos ? value.size() - start
                                                                            : end - start);

        if (part.empty() || part == "." || part == ".." || part.back() == '.' ||
            part.back() == ' ') {
            bad("unsafe path component");
        }

        auto stem = std::string(part.substr(0, part.find('.')));

        for (char &c : stem) {
            if (c >= 'a' && c <= 'z') {
                c -= 32;
            }
        }

        if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" ||
            stem == "CONOUT$" ||
            (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
             stem[3] >= '1' && stem[3] <= '9')) {
            bad("Windows reserved path component");
        }

        if (end == std::string_view::npos) {
            break;
        }

        start = end + 1;
    }
}
} // namespace

std::vector<ZipMember> zip_layout(const std::filesystem::path &file, ZipNamePolicy policy) {
    File input(file);
    const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(input.size, 65557));
    const auto tail = input.read(input.size - tail_size, tail_size);
    std::size_t end = std::string::npos;

    for (std::size_t at = tail.size() - 22;; --at) {
        if (number(tail, at, 4) == 0x06054b50 &&
            at + 22 + number(tail, at + 20, 2) == tail.size()) {
            end = at;
            break;
        }

        if (!at) {
            break;
        }
    }

    if (end == std::string::npos) {
        bad("end record missing");
    }

    const auto eocd = input.size - tail_size + end;

    if (number(tail, end + 4, 2) || number(tail, end + 6, 2) ||
        number(tail, end + 8, 2) != number(tail, end + 10, 2)) {
        bad("multi-disk ZIP unsupported");
    }

    std::uint64_t count = number(tail, end + 10, 2), central_size = number(tail, end + 12, 4),
                  central = number(tail, end + 16, 4), limit = eocd;
    const bool saturated = count == 0xffff || central_size == 0xffffffff || central == 0xffffffff;
    bool zip64 = false;

    if (eocd >= 20) {
        const auto locator = input.read(eocd - 20, 20);
        zip64 = number(locator, 0, 4) == 0x07064b50 && !number(locator, 4, 4) &&
                number(locator, 16, 4) == 1;
    }

    if (saturated && !zip64) {
        bad("ZIP64 locator missing");
    }

    // A ZIP64 end record is accepted when the single-disk fields are saturated, or when a writer
    // appended it unconditionally: then it must repeat exactly the values the legacy fields
    // declare.
    if (zip64) {
        const auto locator = input.read(eocd - 20, 20);
        const auto at = number(locator, 8, 8);
        const auto header = input.read(at, 56);

        if (number(header, 0, 4) != 0x06064b50 || number(header, 4, 8) < 44 ||
            number(header, 4, 8) > 1048576 || at + 12 + number(header, 4, 8) != eocd - 20 ||
            number(header, 16, 4) || number(header, 20, 4) ||
            number(header, 24, 8) != number(header, 32, 8)) {
            bad("ZIP64 end invalid");
        }

        const auto wide_count = number(header, 32, 8), wide_size = number(header, 40, 8),
                   wide_central = number(header, 48, 8);

        if (saturated) {
            count = wide_count;
            central_size = wide_size;
            central = wide_central;
        } else if (wide_count != count || wide_size != central_size || wide_central != central) {
            bad("ZIP64 end differs from single-disk fields");
        }

        limit = at;
    }

    if (!count || count > 100000 || central > limit || central_size > limit - central ||
        central + central_size != limit || central_size > 64 * 1024 * 1024) {
        bad("central directory bound invalid");
    }

    std::vector<ZipMember> members;
    std::set<std::string> names;
    std::map<std::string, bool> directories;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> extents;
    std::uint64_t position = central, total = 0;

    for (std::uint64_t index = 0; index < count; ++index) {
        const auto fixed = input.read(position, 46);

        if (number(fixed, 0, 4) != 0x02014b50) {
            bad("central header missing");
        }

        const auto flags = number(fixed, 8, 2), method = number(fixed, 10, 2),
                   length = number(fixed, 28, 2), extra_length = number(fixed, 30, 2),
                   comment = number(fixed, 32, 2);

        // Bits 1 and 2 only record the deflate compression level; the Linux NDK archive sets them
        // on stored members too.
        if (flags & ~std::uint64_t(0x80e) || method > 8 || (method != 0 && method != 8) ||
            number(fixed, 34, 2)) {
            bad("encrypted or unsupported entry");
        }

        if (position > central + central_size ||
            46 + length + extra_length + comment > central + central_size - position) {
            bad("central entry extent invalid");
        }

        const auto variable = input.read(position + 46,
                                         static_cast<std::size_t>(length + extra_length));
        const auto filename = variable.substr(0, static_cast<std::size_t>(length));
        name(filename);
        auto folded = filename;

        if (folded.back() == '/') {
            folded.pop_back();
        }

        if (policy == ZipNamePolicy::portable) {
            for (char &c : folded) {
                if (c >= 'A' && c <= 'Z') {
                    c += 32;
                }
            }
        }

        if (!names.insert(folded).second) {
            bad("duplicate or case-aliased entry");
        }

        auto compressed = number(fixed, 20, 4), bytes = number(fixed, 24, 4),
             offset = number(fixed, 42, 4);
        const auto extra = std::string_view(variable).substr(static_cast<std::size_t>(length));
        extra_paths(extra, filename);
        bool zip64 = false;

        for (std::size_t at = 0; at < extra.size();) {
            const auto tag = number(extra, at, 2), size = number(extra, at + 2, 2);
            at += 4;

            if (size > extra.size() - at) {
                bad("extra field extent invalid");
            }

            if (tag == 1) {
                if (zip64) {
                    bad("duplicate ZIP64 extra");
                }

                zip64 = true;
                const auto payload = extra.substr(at, static_cast<std::size_t>(size));
                std::size_t cursor = 0;
                auto take = [&] {
                    const auto value = number(payload, cursor, 8);
                    cursor += 8;

                    return value;
                };

                if (bytes == 0xffffffff) {
                    bytes = take();
                }

                if (compressed == 0xffffffff) {
                    compressed = take();
                }

                if (offset == 0xffffffff) {
                    offset = take();
                }
            }

            at += static_cast<std::size_t>(size);
        }

        if (bytes == 0xffffffff || compressed == 0xffffffff || offset == 0xffffffff) {
            bad("ZIP64 extent missing");
        }

        if (bytes > (16ULL << 30) || bytes > (32ULL << 30) - total) {
            bad("expanded ZIP bound exceeded");
        }

        total += bytes;
        const auto attributes = number(fixed, 38, 4);
        const auto mode = static_cast<unsigned>(attributes >> 16);
        const auto kind = mode & 0170000;
        const bool directory = filename.ends_with('/'), symlink = kind == 0120000;

        if (kind && kind != 0100000 && kind != 0040000 && !symlink) {
            bad("special entry unsupported");
        }

        if ((directory && kind && kind != 0040000) || (!directory && kind == 0040000)) {
            bad("directory type differs from name");
        }

        if (offset >= central || central - offset < 30) {
            bad("local header outside payload");
        }

        const auto local = input.read(offset, 30);

        if (number(local, 0, 4) != 0x04034b50 || number(local, 6, 2) != flags ||
            number(local, 8, 2) != method || number(local, 26, 2) != length) {
            bad("local and central headers differ");
        }

        const auto local_extra = number(local, 28, 2);
        const auto data = offset + 30 + length + local_extra;

        if (data > central || compressed > central - data ||
            input.read(offset + 30, static_cast<std::size_t>(length)) != filename) {
            bad("local filename or data extent invalid");
        }

        extra_paths(input.read(offset + 30 + length, static_cast<std::size_t>(local_extra)),
                    filename, true);

        if (!(flags & 8) &&
            (number(local, 14, 4) != number(fixed, 16, 4) ||
             (number(local, 18, 4) != 0xffffffff && number(local, 18, 4) != compressed) ||
             (number(local, 22, 4) != 0xffffffff && number(local, 22, 4) != bytes))) {
            bad("local extent or CRC differs");
        }

        if (directory && bytes) {
            bad("nonempty directory");
        }

        directories.emplace(folded, directory);
        members.push_back({filename, bytes, compressed, data, mode, static_cast<unsigned>(method),
                           static_cast<std::uint32_t>(number(fixed, 16, 4)), directory, symlink});
        extents.emplace_back(offset, data + compressed);
        position += 46 + length + extra_length + comment;
    }

    if (position != central + central_size) {
        bad("central directory trailing bytes");
    }

    std::sort(extents.begin(), extents.end());

    for (std::size_t i = 1; i < extents.size(); ++i) {
        if (extents[i].first < extents[i - 1].second) {
            bad("overlapping local entries");
        }
    }

    for (const auto &item : members) {
        auto value = item.name;

        if (value.ends_with('/')) {
            value.pop_back();
        }

        for (auto parent = std::filesystem::path(value).parent_path(); !parent.empty();
             parent = parent.parent_path()) {
            auto path = parent.generic_string();

            if (policy == ZipNamePolicy::portable) {
                for (char &c : path) {
                    if (c >= 'A' && c <= 'Z') {
                        c += 32;
                    }
                }
            }

            const auto found = directories.find(path);

            if (found != directories.end() && !found->second) {
                bad("file or link used as parent");
            }
        }
    }

    return members;
}
} // namespace voyemu
