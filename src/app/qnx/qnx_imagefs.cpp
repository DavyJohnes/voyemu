#include "qnx_imagefs.hpp"
#include "check.hpp"
#include "error.hpp"
#include "process.hpp"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
namespace voyemu {
namespace {
constexpr CodeCheck need{"invalid QNX image file system"};

// The only capacity check for a replaced member: its slot comes from the OEM image itself.
void fits(bool yes, std::string_view relative, std::uint64_t bytes, std::uint64_t slot) {
    if (!yes) {
        throw VoyemuError("QNX_IMAGEFS_SLOT_CAPACITY",
                          std::string(relative) + " needs " + std::to_string(bytes) +
                                  " bytes, the OEM image slot holds " + std::to_string(slot));
    }
}

std::uint32_t word(std::string_view bytes, std::size_t at) {
    need(at + 4 <= bytes.size(), "QNX_IMAGEFS_BOUNDS");
    std::uint32_t value = 0;

    for (unsigned i = 0; i < 4; ++i) {
        value |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    }

    return value;
}
} // namespace

std::string read_qnx_imagefs_file(std::string_view image, std::string_view relative) {
    need(image.size() >= 96 && image.size() <= 512ULL * 1024 * 1024 &&
                 image.substr(0, 8) == std::string_view("imagefs\x04", 8),
         "QNX_IMAGEFS_HEADER");
    need(!relative.empty() && relative.front() != '/' &&
                 relative.find('\\') == std::string_view::npos &&
                 relative.find('\0') == std::string_view::npos,
         "QNX_IMAGEFS_PATH");

    for (std::size_t at = 0; at < relative.size();) {
        auto end = relative.find('/', at);

        if (end == std::string_view::npos) {
            end = relative.size();
        }

        const auto part = relative.substr(at, end - at);
        need(!part.empty() && part != "." && part != "..", "QNX_IMAGEFS_PATH");
        at = end + 1;
    }

    const auto extent = word(image, 8), end = word(image, 12), start = word(image, 16);
    need(extent >= 96 && extent <= image.size() && extent % 4 == 0 && start >= 92 && end > start &&
                 end <= extent,
         "QNX_IMAGEFS_GEOMETRY");
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < extent; at += 4) {
        sum += word(image, at);
    }

    need(sum == 0, "QNX_IMAGEFS_CHECKSUM");
    std::string result;
    bool found = false, terminated = false;
    unsigned entries = 0;

    for (std::size_t at = start; at + 4 <= end;) {
        const auto size = word(image, at);

        if (size == 0) {
            need(at + 4 == end, "QNX_IMAGEFS_DIRECTORY_END");
            terminated = true;
            break;
        }

        need(++entries <= 65536 && size >= 28 && size % 4 == 0 && size <= end - at,
             "QNX_IMAGEFS_ENTRY_BOUNDS");
        const auto kind = word(image, at + 8) & 0170000;
        const unsigned name_offset = kind == 0100000 ? 32 : kind == 0120000 ? 28 : 24;
        need(name_offset < size, "QNX_IMAGEFS_ENTRY_NAME");
        const auto name = image.substr(at + name_offset, size - name_offset);
        const auto zero = name.find('\0');
        need(zero != std::string_view::npos, "QNX_IMAGEFS_NAME_TERMINATOR");

        if (name.substr(0, zero) == relative) {
            need(!found && kind == 0100000, "QNX_IMAGEFS_REGULAR_FILE");
            const auto offset = word(image, at + 24), bytes = word(image, at + 28);
            need(offset >= end && offset <= extent && bytes <= extent - offset,
                 "QNX_IMAGEFS_FILE_BOUNDS");
            result = image.substr(offset, bytes);
            found = true;
        }

        at += size;
    }

    need(terminated && found, "QNX_IMAGEFS_FILE_MISSING");

    return result;
}

std::string replace_qnx_imagefs_file(std::string_view image, std::string_view relative,
                                     std::string_view pin, std::string_view payload,
                                     unsigned alignment, bool retain_size) {
    const auto original = read_qnx_imagefs_file(image, relative);

    need(sha256_bytes(original) == pin, "QNX_IMAGEFS_ORIGINAL_SHA");
    need(!payload.empty() && alignment > 0 && alignment <= 4096 &&
                 (alignment & (alignment - 1)) == 0,
         "QNX_IMAGEFS_PAYLOAD");
    const auto end = word(image, 12);
    std::size_t at = word(image, 16);

    for (; at + 4 <= end;) {
        const auto size = word(image, at);
        need(size != 0, "QNX_IMAGEFS_FILE_MISSING");

        if ((word(image, at + 8) & 0170000) == 0100000) {
            const auto names = image.substr(at + 32, size - 32);

            if (names.substr(0, names.find('\0')) == relative) {
                break;
            }
        }

        at += size;
    }

    const auto old_offset = word(image, at + 24), capacity = word(image, at + 28);
    const std::uint64_t aligned = (std::uint64_t(old_offset) + alignment - 1) &
                                  ~std::uint64_t(alignment - 1);
    fits(aligned >= old_offset && aligned <= std::uint64_t(old_offset) + capacity &&
                 payload.size() <= std::uint64_t(old_offset) + capacity - aligned,
         relative, payload.size(), capacity);
    need(!retain_size || aligned == old_offset, "QNX_IMAGEFS_PADDED_ALIGNMENT");
    std::string output(image);
    std::fill(output.begin() + old_offset, output.begin() + old_offset + capacity, 0);
    output.replace(static_cast<std::size_t>(aligned), payload.size(), payload);
    auto put = [&](std::size_t offset, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            output[offset + i] = static_cast<char>(value >> (i * 8));
        }
    };
    put(at + 24, static_cast<std::uint32_t>(aligned));
    put(at + 28, retain_size ? capacity : static_cast<std::uint32_t>(payload.size()));
    const auto extent = word(image, 8);
    put(extent - 4, 0);
    std::uint32_t sum = 0;

    for (std::size_t off = 0; off < extent; off += 4) {
        sum += word(output, off);
    }

    put(extent - 4, 0 - sum);
    const auto checked = read_qnx_imagefs_file(output, relative);
    need(checked.substr(0, payload.size()) == payload &&
                 checked.size() == (retain_size ? capacity : payload.size()),
         "QNX_IMAGEFS_REPLACEMENT_VERIFY");

    return output;
}

std::string replace_qnx_imagefs_file_in_zero_padding(std::string_view image,
                                                     std::string_view relative,
                                                     std::string_view pin, std::string_view payload,
                                                     unsigned max_growth) {
    const auto original = read_qnx_imagefs_file(image, relative);

    need(sha256_bytes(original) == pin, "QNX_IMAGEFS_ORIGINAL_SHA");
    need(!payload.empty() && max_growth > 0 && max_growth <= 4096 &&
                 payload.size() > original.size(),
         "QNX_IMAGEFS_GROWTH_BOUNDS");
    fits(payload.size() - original.size() <= max_growth, relative, payload.size(),
         original.size() + max_growth);
    const auto extent = word(image, 8), end = word(image, 12);
    std::size_t selected = 0;

    for (std::size_t at = word(image, 16); at + 4 <= end;) {
        const auto size = word(image, at);

        if (!size) {
            break;
        }

        if ((word(image, at + 8) & 0170000) == 0100000) {
            const auto names = image.substr(at + 32, size - 32);

            if (names.substr(0, names.find('\0')) == relative) {
                selected = at;
            }
        }

        at += size;
    }

    need(selected != 0, "QNX_IMAGEFS_FILE_MISSING");
    const auto offset = word(image, selected + 24);
    const std::uint64_t new_end = std::uint64_t(offset) + payload.size();
    fits(new_end <= extent - 4, relative, payload.size(), extent - 4 - offset);

    for (std::size_t at = word(image, 16); at + 4 <= end;) {
        const auto size = word(image, at);

        if (!size) {
            break;
        }

        if (at != selected && (word(image, at + 8) & 0170000) == 0100000) {
            const auto other = word(image, at + 24), bytes = word(image, at + 28);
            need(bytes == 0 || std::uint64_t(other) + bytes <= offset || other >= offset,
                 "QNX_IMAGEFS_GROWTH_OVERLAP");
            fits(bytes == 0 || other < offset || other >= new_end, relative, payload.size(),
                 other - offset);
        }

        at += size;
    }

    const auto padding = image.substr(offset + original.size(), payload.size() - original.size())
                                 .find_first_not_of('\0');
    fits(padding == std::string_view::npos, relative, payload.size(), original.size() + padding);
    std::string output(image);
    output.replace(offset, payload.size(), payload);
    auto put = [&](std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            output[at + i] = static_cast<char>(value >> (i * 8));
        }
    };
    put(selected + 28, static_cast<std::uint32_t>(payload.size()));
    put(extent - 4, 0);
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < extent; at += 4) {
        sum += word(output, at);
    }

    put(extent - 4, 0 - sum);
    need(read_qnx_imagefs_file(output, relative) == payload, "QNX_IMAGEFS_GROWTH_VERIFY");

    return output;
}

std::string pad_qnx_imagefs_first_member(std::string_view image, unsigned padding) {
    need(image.size() >= 96 && image.size() <= 512ULL * 1024 * 1024 &&
                 image.substr(0, 8) == std::string_view("imagefs\x04", 8),
         "QNX_IMAGEFS_HEADER");
    need(padding >= 4 && padding <= 4096 && padding % 4 == 0, "QNX_IMAGEFS_PAD_BOUNDS");
    const auto extent = word(image, 8), end = word(image, 12), start = word(image, 16);
    need(extent >= 96 && extent <= image.size() && extent % 4 == 0 && start >= 92 && end > start &&
                 end <= extent,
         "QNX_IMAGEFS_GEOMETRY");
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < extent; at += 4) {
        sum += word(image, at);
    }

    need(sum == 0, "QNX_IMAGEFS_CHECKSUM");
    std::size_t selected = 0, first_offset = 0, last_end = 0;
    bool terminated = false;

    for (std::size_t at = start; at + 4 <= end;) {
        const auto size = word(image, at);

        if (size == 0) {
            need(at + 4 == end, "QNX_IMAGEFS_DIRECTORY_END");
            terminated = true;
            break;
        }

        need(size >= 28 && size % 4 == 0 && size <= end - at, "QNX_IMAGEFS_ENTRY_BOUNDS");

        if ((word(image, at + 8) & 0170000) == 0100000) {
            const auto offset = word(image, at + 24), bytes = word(image, at + 28);
            need(offset >= end && offset <= extent && bytes <= extent - offset,
                 "QNX_IMAGEFS_FILE_BOUNDS");

            if (selected == 0 || offset < first_offset) {
                selected = at;
                first_offset = offset;
            }

            last_end = std::max(last_end, std::size_t(offset) + bytes);
        }

        at += size;
    }

    // The trailer word follows the last member, so the first member can only grow
    // when every later member starts after it.
    need(terminated && selected != 0 && last_end == std::size_t(extent) - 4,
         "QNX_IMAGEFS_PAD_TRAILER");
    const std::uint32_t first_bytes = word(image, selected + 28);
    const std::size_t boundary = std::size_t(first_offset) + first_bytes;
    need(boundary <= std::size_t(extent) - 4, "QNX_IMAGEFS_PAD_CAPACITY");
    std::string output(image.size() + padding, '\0');
    output.replace(0, boundary, image.substr(0, boundary));
    output.replace(boundary + padding, image.size() - boundary, image.substr(boundary));
    auto put = [&](std::size_t at, std::uint32_t value) {
        for (unsigned i = 0; i < 4; ++i) {
            output[at + i] = static_cast<char>(value >> (i * 8));
        }
    };
    put(selected + 28, first_bytes + padding);

    if (image.substr(first_offset, 8) == std::string_view("imagefs\x04", 8)) {
        need(word(image, first_offset + 8) == first_bytes, "QNX_IMAGEFS_PAD_NESTED_EXTENT");
        put(first_offset + 8, first_bytes + padding);
    }

    for (std::size_t at = start; at + 4 <= end;) {
        const auto size = word(image, at);

        if (size == 0) {
            break;
        }

        if (at != selected && (word(image, at + 8) & 0170000) == 0100000) {
            const auto offset = word(image, at + 24), bytes = word(image, at + 28);
            need(offset >= boundary, "QNX_IMAGEFS_PAD_OVERLAP");
            need(std::string_view(output).substr(std::size_t(offset) + padding, bytes) ==
                         image.substr(offset, bytes),
                 "QNX_IMAGEFS_PAD_PAYLOAD");
            put(at + 24, offset + padding);
        }

        at += size;
    }

    const auto next_extent = std::uint32_t(extent + padding);
    put(8, next_extent);
    put(next_extent - 4, 0);
    std::uint32_t total = 0;

    for (std::size_t at = 0; at < next_extent; at += 4) {
        total += word(output, at);
    }

    put(next_extent - 4, 0 - total);
    const auto names = image.substr(selected + 32, word(image, selected) - 32);
    const auto zero = names.find('\0');
    need(zero != std::string_view::npos, "QNX_IMAGEFS_NAME_TERMINATOR");
    need(read_qnx_imagefs_file(output, names.substr(0, zero)) ==
                 std::string(output.substr(first_offset, first_bytes)) + std::string(padding, '\0'),
         "QNX_IMAGEFS_PAD_VERIFY");

    return output;
}

std::string rename_qnx_imagefs_file(std::string_view image, std::string_view before,
                                    std::string_view after) {
    const auto original = read_qnx_imagefs_file(image, before);

    need(!after.empty() && after.size() <= before.size() && after.front() != '/' &&
                 after.find('\0') == std::string_view::npos &&
                 after.find('\\') == std::string_view::npos,
         "QNX_IMAGEFS_RENAME_PATH");

    for (std::size_t p = 0; p < after.size();) {
        auto end = after.find('/', p);

        if (end == std::string_view::npos) {
            end = after.size();
        }

        const auto part = after.substr(p, end - p);
        need(!part.empty() && part != "." && part != "..", "QNX_IMAGEFS_RENAME_PATH");
        p = end + 1;
    }

    const auto end = word(image, 12);
    std::size_t found = 0;

    for (std::size_t at = word(image, 16); at + 4 <= end;) {
        const auto size = word(image, at);

        if (!size) {
            break;
        }

        const auto kind = word(image, at + 8) & 0170000;
        const unsigned name_offset = kind == 0100000 ? 32 : kind == 0120000 ? 28 : 24;
        const auto names = image.substr(at + name_offset, size - name_offset),
                   name = names.substr(0, names.find('\0'));
        need(name != after, "QNX_IMAGEFS_RENAME_EXISTS");

        if (name == before) {
            need(kind == 0100000, "QNX_IMAGEFS_RENAME_REGULAR");
            found = at;
        }

        at += size;
    }

    need(found != 0, "QNX_IMAGEFS_FILE_MISSING");
    std::string output(image);
    std::fill(output.begin() + found + 32, output.begin() + found + word(image, found), 0);
    output.replace(found + 32, after.size(), after);
    const auto extent = word(image, 8);

    for (unsigned i = 0; i < 4; ++i) {
        output[extent - 4 + i] = 0;
    }

    std::uint32_t sum = 0;

    for (std::size_t off = 0; off < extent; off += 4) {
        sum += word(output, off);
    }

    for (unsigned i = 0; i < 4; ++i) {
        output[extent - 4 + i] = static_cast<char>((0 - sum) >> (i * 8));
    }

    need(read_qnx_imagefs_file(output, after) == original, "QNX_IMAGEFS_RENAME_VERIFY");

    return output;
}

std::string rename_qnx_imagefs_duplicate_file(std::string_view image, std::string_view before,
                                              std::string_view after, std::string_view pin) {
    need(image.size() >= 96 && image.size() <= (512ULL << 20) &&
                 image.substr(0, 8) == std::string_view("imagefs\x04", 8),
         "QNX_IMAGEFS_HEADER");
    need(sha256_text(pin), "QNX_IMAGEFS_DUPLICATE_SHA");

    for (const auto path : {before, after}) {
        need(!path.empty() && path.front() != '/' && path.find('\\') == std::string_view::npos &&
                     path.find('\0') == std::string_view::npos,
             "QNX_IMAGEFS_RENAME_PATH");

        for (std::size_t start = 0; start < path.size();) {
            const auto end = path.find('/', start);
            const auto part = path.substr(start, end == std::string_view::npos ? path.size() - start
                                                                               : end - start);
            need(!part.empty() && part != "." && part != "..", "QNX_IMAGEFS_RENAME_PATH");

            if (end == std::string_view::npos) {
                break;
            }

            start = end + 1;
        }
    }

    need(before != after && after.size() <= before.size(), "QNX_IMAGEFS_RENAME_PATH");
    const auto extent = word(image, 8), end = word(image, 12), start = word(image, 16);
    need(extent >= 96 && extent <= image.size() && extent % 4 == 0 && start >= 92 && end > start &&
                 end <= extent,
         "QNX_IMAGEFS_GEOMETRY");
    std::uint32_t sum = 0;

    for (std::size_t at = 0; at < extent; at += 4) {
        sum += word(image, at);
    }

    need(sum == 0, "QNX_IMAGEFS_CHECKSUM");
    unsigned count = 0, matches = 0, entries = 0;
    std::size_t selected = 0;
    bool terminated = false;
    std::string payload;

    for (std::size_t at = start; at + 4 <= end;) {
        const auto size = word(image, at);

        if (!size) {
            need(at + 4 == end, "QNX_IMAGEFS_DIRECTORY_END");
            terminated = true;
            break;
        }

        need(++entries <= 65536 && size >= 28 && size % 4 == 0 && size <= end - at,
             "QNX_IMAGEFS_ENTRY_BOUNDS");
        const auto kind = word(image, at + 8) & 0170000;
        const unsigned name_offset = kind == 0100000 ? 32 : kind == 0120000 ? 28 : 24;
        need(name_offset < size, "QNX_IMAGEFS_ENTRY_NAME");
        const auto names = image.substr(at + name_offset, size - name_offset);
        const auto zero = names.find('\0');
        need(zero != std::string_view::npos, "QNX_IMAGEFS_NAME_TERMINATOR");
        const auto name = names.substr(0, zero);
        need(name != after, "QNX_IMAGEFS_RENAME_EXISTS");

        if (name == before) {
            need(kind == 0100000, "QNX_IMAGEFS_DUPLICATE_REGULAR");
            ++count;
            const auto offset = word(image, at + 24), bytes = word(image, at + 28);
            need(offset >= end && offset <= extent && bytes <= extent - offset,
                 "QNX_IMAGEFS_FILE_BOUNDS");
            const auto candidate = image.substr(offset, bytes);

            if (sha256_bytes(candidate) == pin) {
                ++matches;
                selected = at;
                payload = candidate;
            }
        }

        at += size;
    }

    need(terminated && count == 2 && matches == 1 && selected != 0,
         "QNX_IMAGEFS_DUPLICATE_IDENTITY");
    std::string output(image);
    std::fill(output.begin() + selected + 32, output.begin() + selected + word(image, selected), 0);
    output.replace(selected + 32, after.size(), after);

    for (unsigned i = 0; i < 4; ++i) {
        output[extent - 4 + i] = 0;
    }

    sum = 0;

    for (std::size_t at = 0; at < extent; at += 4) {
        sum += word(output, at);
    }

    for (unsigned i = 0; i < 4; ++i) {
        output[extent - 4 + i] = static_cast<char>((0 - sum) >> (i * 8));
    }

    need(read_qnx_imagefs_file(output, after) == payload, "QNX_IMAGEFS_RENAME_VERIFY");
    (void)read_qnx_imagefs_file(output, before);

    return output;
}
} // namespace voyemu
