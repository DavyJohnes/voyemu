#include "codec_timing.hpp"
#include "error.hpp"
#include "process.hpp"
#include <cstdint>
#include <stdexcept>
namespace voyemu {
namespace {
constexpr std::string_view input_sha =
        "2262964922a876a51c3b5186f094afb136ce9daae0678f9820f11e86e058e353";
constexpr std::string_view function =
        "_ZN7android6CCodec17onMessageReceivedERKNS_2spINS_8AMessageEEE";
constexpr std::uint64_t function_va = 0x29b74, allocation_va = 0x29cf0,
                        allocation_name_va = 0x79a93;

void need(bool value, std::string_view reason) {
    if (!value) {
        throw VoyemuError("ANDROID_CODEC_TIMING", std::string(reason));
    }
}

std::uint64_t read(std::string_view bytes, std::uint64_t at, unsigned width) {
    need(width <= 8 && at <= bytes.size() && width <= bytes.size() - at, "BOUNDS");
    std::uint64_t value = 0;

    for (unsigned i = 0; i < width; ++i) {
        value |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }

    return value;
}

std::uint64_t executable_offset(std::string_view bytes, std::uint64_t va, std::uint64_t size) {
    const auto table = read(bytes, 32, 8), count = read(bytes, 56, 2);

    need(read(bytes, 54, 2) == 56 && count == 8 && table <= bytes.size() &&
                 count * 56 <= bytes.size() - table,
         "PROGRAM_HEADERS");
    std::uint64_t result = 0;
    unsigned found = 0;

    for (std::uint64_t i = 0; i < count; ++i) {
        const auto header = table + i * 56;

        if (read(bytes, header, 4) != 1) {
            continue;
        }

        const auto offset = read(bytes, header + 8, 8), address = read(bytes, header + 16, 8),
                   length = read(bytes, header + 32, 8);
        need(offset <= bytes.size() && length <= bytes.size() - offset, "SEGMENT_BOUNDS");

        if ((read(bytes, header + 4, 4) & 5) != 5 || va < address || va - address > length ||
            size > length - (va - address)) {
            continue;
        }

        result = offset + va - address;
        ++found;
    }

    need(found == 1, "EXECUTABLE_SEGMENT");

    return result;
}

void check_function(std::string_view bytes) {
    const auto table = read(bytes, 40, 8), count = read(bytes, 60, 2);

    need(read(bytes, 58, 2) == 64 && count > 0 && count <= 64 && table <= bytes.size() &&
                 count * 64 <= bytes.size() - table,
         "SECTION_HEADERS");
    unsigned found = 0;

    for (std::uint64_t i = 0; i < count; ++i) {
        const auto section = table + i * 64;

        if (read(bytes, section + 4, 4) != 11) {
            continue;
        }

        const auto offset = read(bytes, section + 24, 8), size = read(bytes, section + 32, 8),
                   link = read(bytes, section + 40, 4);
        need(read(bytes, section + 56, 8) == 24 && size % 24 == 0 && offset <= bytes.size() &&
                     size <= bytes.size() - offset && link < count,
             "SYMBOL_TABLE");
        const auto strings = table + link * 64, start = read(bytes, strings + 24, 8),
                   length = read(bytes, strings + 32, 8);
        need(read(bytes, strings + 4, 4) == 3 && start <= bytes.size() &&
                     length <= bytes.size() - start,
             "SYMBOL_STRINGS");

        for (std::uint64_t at = offset; at < offset + size; at += 24) {
            const auto name = read(bytes, at, 4);
            need(name < length, "SYMBOL_NAME");
            const auto suffix = bytes.substr(start + name, length - name);
            const auto end = suffix.find('\0');
            need(end != std::string_view::npos, "SYMBOL_NAME");

            if (suffix.substr(0, end) != function) {
                continue;
            }

            need((read(bytes, at + 4, 1) & 15) == 2 && read(bytes, at + 6, 2) != 0 &&
                         read(bytes, at + 8, 8) == function_va && read(bytes, at + 16, 8) == 0x1a08,
                 "FUNCTION_LAYOUT");
            ++found;
        }
    }

    need(found == 1, "FUNCTION_SYMBOL");
}

void instruction(std::string &bytes, std::uint64_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes[offset + i] = static_cast<char>(value >> (8 * i));
    }
}
} // namespace

AndroidCodecTimingPatch android_codec_timing_patch(std::string_view input, bool omx_layout) {
    need(input.size() == 632488, "SIZE");
    need(input.substr(0, 4) == std::string_view("\x7f"
                                                "ELF",
                                                4) &&
                 read(input, 4, 1) == 2 && read(input, 5, 1) == 1 && read(input, 6, 1) == 1 &&
                 read(input, 16, 2) == 3 && read(input, 18, 2) == 183 && read(input, 20, 4) == 1 &&
                 read(input, 52, 2) == 64,
         "ARM64_ELF");
    check_function(input);
    const auto offset = executable_offset(input, allocation_va, 8),
               begin = executable_offset(input, function_va, 0x1a08),
               name = executable_offset(input, allocation_name_va, 9);
    need(offset == 0x1bcf0 && offset - begin == 0x17c, "FUNCTION_OFFSET");
    need(input.substr(name, 9) == std::string_view("allocate\0", 9), "ALLOCATION_NAME");
    need(read(input, offset, 4) == 0x529a3008 && read(input, offset + 4, 4) == 0x72a11e08,
         "ALLOCATION_INSTRUCTIONS");
    need(sha256_bytes(input.substr(offset - 16, 72)) ==
                 "afa9dbc521970a29fa7d8bbc2614fc7275f912183f94b3245997c597c65ae628",
         "INSTRUCTION_WINDOW");
    const auto width = executable_offset(input, 0x2446c, 4),
               height = executable_offset(input, 0x24474, 4);
    need(width == 0x1646c && height == 0x16474 && read(input, width, 4) == 0xb9003049 &&
                 read(input, height, 4) == 0xb9003448,
         "OMX_PORT_LAYOUT");
    need(sha256_bytes(input) == input_sha, "INPUT_SHA256");
    AndroidCodecTimingPatch result;
    result.bytes = std::string(input);

    // Match the 64-bit OMX port layout used by the input-surface service.
    if (omx_layout) {
        instruction(result.bytes, width, 0xb9003849);
        instruction(result.bytes, height, 0xb9003c48);
    }

    // 32768 ms is exact in two ARM64 move-wide instructions; the existing ADD and all flow remain
    // intact.
    instruction(result.bytes, offset, 0xd2b42408);
    instruction(result.bytes, offset + 4, 0xf2c000e8);
    result.receipt = {
            {"schema", 1},
            {"recipe", "android28-ccodec-allocation-tcg-v1"},
            {"input_sha256", input_sha},
            {"output_sha256", sha256_bytes(result.bytes)},
            {"elf_bytes", input.size()},
            {"function", function},
            {"function_va", function_va},
            {"allocation_va", allocation_va},
            {"file_offset", offset},
            {"replacement_extent_bytes", 8},
            {"old_deadline_ns", 150000000},
            {"new_deadline_ns", 32768000000ULL},
            {"other_bytes_unchanged", true},
            {"runtime_verified", false},
            {"aosp_source",
             "https://android.googlesource.com/platform/hardware/google/av/+/89bca80/media/sfplugin/CCodec.cpp"}};

    if (omx_layout) {
        result.receipt["omx_port_layout"] = {{"width_file_offset", width},
                                             {"height_file_offset", height},
                                             {"width_offset", 56},
                                             {"height_offset", 60}};
    }

    return result;
}
} // namespace voyemu
