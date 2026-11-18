#include "codec_timing.hpp"
#include "process.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace voyemu;
namespace {
void check(bool value) {
    if (!value) {
        throw std::runtime_error("codec timing check failed");
    }
}

void refuses(std::string bytes, std::string_view reason) {
    bool refused = false;

    try {
        android_codec_timing_patch(bytes);
    } catch (const std::exception &error) {
        refused = std::string_view(error.what()).find(reason) != std::string_view::npos;
    }

    check(refused);
}

std::string load(const std::filesystem::path &path) {
    check(path.is_absolute() &&
          std::filesystem::is_regular_file(std::filesystem::symlink_status(path)) &&
          std::filesystem::hard_link_count(path) == 1 &&
          std::filesystem::file_size(path) == 632488);
    std::ifstream file(path, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(file)), {});
    check(bytes.size() == 632488);

    return bytes;
}

std::uint32_t word(std::string_view bytes, std::size_t at) {
    std::uint32_t value = 0;

    for (unsigned i = 0; i < 4; ++i) {
        value |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }

    return value;
}

void fixture(const std::string &original) {
    const auto legacy = android_codec_timing_patch(original, false);

    check(sha256_bytes(legacy.bytes) ==
                  "36da952afee36f4e768ec74080aa157e92ba29881718dd9d65c8a5a8401b166a" &&
          !legacy.receipt.contains("omx_port_layout"));
    const auto patched = android_codec_timing_patch(original);
    constexpr std::size_t site = 0x1bcf0, width = 0x1646c, height = 0x16474;
    check(word(patched.bytes, width) == 0xb9003849 && word(patched.bytes, height) == 0xb9003c48);
    check(patched.bytes.size() == original.size() && patched.bytes != original);

    for (std::size_t i = 0; i < original.size(); ++i) {
        if ((i < site || i >= site + 8) && (i < width || i >= width + 4) &&
            (i < height || i >= height + 4)) {
            check(original[i] == patched.bytes[i]);
        }
    }

    std::string port(0x70, char(0));
    const auto store = [&](std::size_t instructionOffset, std::uint32_t value) {
        const auto insn = word(patched.bytes, instructionOffset);
        check((insn & 0xffc003ff) == 0xb9000049 || (insn & 0xffc003ff) == 0xb9000048);
        const auto target = ((insn >> 10) & 0xfff) * 4;
        check(target + 4 <= port.size());

        for (unsigned i = 0; i < 4; ++i) {
            port[target + i] = static_cast<char>(value >> (8 * i));
        }
    };
    store(width, 640);
    store(height, 480);
    check(word(port, 0x38) == 640 && word(port, 0x3c) == 480);
    const auto first = word(patched.bytes, site), second = word(patched.bytes, site + 4);
    check((first & 0xff80001f) == 0xd2800008 && (second & 0xff80001f) == 0xf2800008);
    const auto deadline = (std::uint64_t((first >> 5) & 65535) << (((first >> 21) & 3) * 16)) |
                          (std::uint64_t((second >> 5) & 65535) << (((second >> 21) & 3) * 16));
    check(deadline == 32768000000ULL);
    check(patched.receipt.at("new_deadline_ns") == deadline &&
          patched.receipt.at("runtime_verified") == false &&
          patched.receipt.at("input_sha256") == sha256_bytes(original) &&
          patched.receipt.at("output_sha256") == sha256_bytes(patched.bytes));
    auto bad = original;
    bad[18] = 0;
    refuses(bad, "ARM64_ELF");
    bad = original;

    for (unsigned i = 0; i < 8; ++i) {
        bad[32 + i] = static_cast<char>(255);
    }

    refuses(bad, "PROGRAM_HEADERS");
    bad = original;
    bad[site] ^= 1;
    refuses(bad, "ALLOCATION_INSTRUCTIONS");
    bad = original;
    bad[width] ^= 1;
    refuses(bad, "OMX_PORT_LAYOUT");
    bad = original;
    bad[height] ^= 1;
    refuses(bad, "OMX_PORT_LAYOUT");
    bad = original;
    bad[site - 1] ^= 1;
    refuses(bad, "INSTRUCTION_WINDOW");
    bad = original;
    bad[0x79a93 - 0xe000] = 'x';
    refuses(bad, "ALLOCATION_NAME");
    bad = original;
    const auto symbol = bad.find("_ZN7android6CCodec17onMessageReceivedERKNS_2spINS_8AMessageEEE");
    check(symbol != std::string::npos);
    bad[symbol] = 'x';
    refuses(bad, "FUNCTION_SYMBOL");
    bad = original;
    bad[0x10000] ^= 1;
    refuses(bad, "INPUT_SHA256");
    refuses(patched.bytes, "ALLOCATION_INSTRUCTIONS");
}
} // namespace

int main(int argc, char **argv) {
    try {
        refuses({}, "SIZE");
        refuses(std::string(632488, '\0'), "ARM64_ELF");

        if (argc == 3 && std::string_view(argv[1]) == "--fixture") {
            fixture(load(std::filesystem::absolute(argv[2])));
            std::cout << "pinned guest ELF: exact allocation budget and rejection guards passed\n";

            return 0;
        }

        if (argc == 4 && std::string_view(argv[1]) == "--apply") {
            const auto input = std::filesystem::absolute(argv[2]),
                       destination = std::filesystem::path(argv[3]);
            const auto original = load(input);
            fixture(original);
            const auto patch = android_codec_timing_patch(original);
            check(destination.is_absolute() && !std::filesystem::exists(destination) &&
                  !std::filesystem::is_symlink(destination) &&
                  std::filesystem::is_directory(destination.parent_path()));
            ensure_private_directory(destination);
            std::ofstream file(destination / "libstagefright_ccodec.so", std::ios::binary);
            file.write(patch.bytes.data(), static_cast<std::streamsize>(patch.bytes.size()));
            file.close();
            check(bool(file) && sha256_file(destination / "libstagefright_ccodec.so") ==
                                        patch.receipt.at("output_sha256").get<std::string>());
            write_private_json(destination / "receipt.json", patch.receipt);
            std::cout << patch.receipt.dump(2) << '\n';

            return 0;
        }

        check(argc == 1);
        std::cout
                << "unregistered codec inputs refused; pass --fixture to exercise the pinned guest ELF\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
