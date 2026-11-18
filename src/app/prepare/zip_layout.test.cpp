#include "zip_layout.hpp"
#include "model.hpp"
#include "process.hpp"
#include "zip_archive.hpp"
#include <fstream>
#include <iostream>
#include <map>
using namespace voyemu;

static void check(bool value, const char *message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}

static std::string decode(const std::string &hex) {
    std::string bytes;

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        bytes += char(std::stoul(hex.substr(i, 2), nullptr, 16));
    }

    return bytes;
}

int main(int argc, char **argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--nxp-launcher") {
            const std::filesystem::path file = argv[2];
            const std::string pin =
                    "93f4bea56530bf2de8634b7898fd4275a3c8985ff0392fdc58607ef8bc5b2441";
            const auto entries = pinned_zip_inventory(file, pin, ZipNamePolicy::exact);
            const auto upper = read_pinned_zip_entry(file, pin, "res/-A.png", 1024U,
                                                     ZipNamePolicy::exact),
                       lower = read_pinned_zip_entry(file, pin, "res/-a.png", 1024U,
                                                     ZipNamePolicy::exact);
            check(upper.size() == 421 && lower.size() == 741 && upper != lower,
                  "OEM resource case aliases contain distinct bytes");
            check(sha256_bytes(upper) ==
                                  "ca4f1a1fb757f2a43b213914da7b135db9f00221e877ecf61abd22cb4136c4ff" &&
                          sha256_bytes(lower) ==
                                  "7db3105159263f093d4436a5d9a87d361ac82c8a298b2d7a1bdbbe59d23acecf",
                  "OEM resource exact byte witness");
            bool portable_rejected = false;
            try {
                (void)pinned_zip_inventory(file, pin);
            } catch (...) {
                portable_rejected = true;
            }

            check(portable_rejected, "OEM APK cannot be treated as portable extraction tree");
            bool pin_rejected = false;
            try {
                (void)pinned_zip_inventory(file, std::string(64, 'a'), ZipNamePolicy::exact);
            } catch (...) {
                pin_rejected = true;
            }

            check(pin_rejected, "exact logical index still validates complete archive pin");
            std::cout << "NXP Launcher case-sensitive index and distinct pinned resources passed: "
                      << entries.size() << " entries\n";

            return 0;
        }

        if (argc == 2) {
            const auto entries = zip_layout(std::filesystem::path(argv[1]));
            std::uint64_t bytes = 0;
            unsigned links = 0;

            for (const auto &entry : entries) {
                bytes += entry.bytes;
                links += entry.symlink;
            }

            std::cout << Json{{"entries", entries.size()},
                              {"bytes", bytes},
                              {"links", links},
                              {"native_structural_index", true}}
                                 .dump()
                      << '\n';

            return 0;
        }

        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-zip-layout-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        // deflate_level sets the maximum compression bit of the general purpose flags, as the Linux
        // NDK archive does; encrypted sets the encryption bit of the same member.
        // clang-format off: one fixture per line; clang-format cannot wrap the long hex literals.
        const std::map<std::string, std::string> fixtures = {
                {"deflate_level", "504b030414000200080000006d5d173cf5380a0000000f0000000800000073646b2f746f6f6c2bc9cfcf512881115c00504b0102140314000200080000006d5d173cf5380a0000000f00000008000000000000000000000080010000000073646b2f746f6f6c504b0506000000000100010036000000300000000000"},
                {"encrypted", "504b030414000100080000006d5d173cf5380a0000000f0000000800000073646b2f746f6f6c2bc9cfcf512881115c00504b0102140314000100080000006d5d173cf5380a0000000f00000008000000000000000000000080010000000073646b2f746f6f6c504b0506000000000100010036000000300000000000"},
                {"good", "504b030414000000080000002100b71d88c008000000060000000c00000073646b2f62696e2f746f6f6ccb4b2cc92c4b0500504b030414000000080000002100ab461a990a000000080000000900000073646b2f616c6961734bcaccd32fc9cfcf0100504b0102140314000000080000002100b71d88c008000000060000000c0000000000000000000000ed810000000073646b2f62696e2f746f6f6c504b0102140314000000080000002100ab461a990a00000008000000090000000000000000000000eda13200000073646b2f616c696173504b0506000000000200020071000000630000000000"},
                {"zip64_end", "504b0304140000000000d7514e5d924e846c04000000040000000700000073646b2f6f776e676f6f64504b01021403140000000000d7514e5d924e846c040000000400000007000000000000000000000080010000000073646b2f6f776e504b06062c000000000000002d002d0000000000000000000100000000000000010000000000000035000000000000002900000000000000504b0607000000005e0000000000000001000000504b050600000000ffffffffffffffffffffffff0000"},
                {"zip64_local", "504b03042d000000080000002100b71d88c0ffffffffffffffff0c00140073646b2f62696e2f746f6f6c0100100006000000000000000800000000000000cb4b2cc92c4b0500504b01022d032d000000080000002100b71d88c008000000060000000c0000000000000000000000ed810000000073646b2f62696e2f746f6f6c504b050600000000010001003a000000460000000000"},
                {"escape", "504b030414000000080000002100fb392b8205000000030000000d00000073646b2f2e2e2f6573636170654b4a4c0100504b0102140314000000080000002100fb392b8205000000030000000d0000000000000000000000ed810000000073646b2f2e2e2f657363617065504b050600000000010001003b000000300000000000"},
                {"reserved", "504b030414000000080000002100fb392b8205000000030000000b00000073646b2f434f4e2e7478744b4a4c0100504b0102140314000000080000002100fb392b8205000000030000000b0000000000000000000000ed810000000073646b2f434f4e2e747874504b05060000000001000100390000002e0000000000"},
                {"case", "504b030414000000080000002100fb392b8205000000030000000500000073646b2f414b4a4c0100504b030414000000080000002100fb392b8205000000030000000500000073646b2f614b4a4c0100504b0102140314000000080000002100fb392b820500000003000000050000000000000000000000ed810000000073646b2f41504b0102140314000000080000002100fb392b820500000003000000050000000000000000000000ed812800000073646b2f61504b0506000000000200020066000000500000000000"},
                {"parent", "504b030414000000080000002100fb392b8205000000030000000800000073646b2f66696c654b4a4c0100504b030414000000080000002100fb392b8205000000030000000e00000073646b2f66696c652f6368696c644b4a4c0100504b0102140314000000080000002100fb392b820500000003000000080000000000000000000000ed810000000073646b2f66696c65504b0102140314000000080000002100fb392b8205000000030000000e0000000000000000000000ed812b00000073646b2f66696c652f6368696c64504b05060000000002000200720000005c0000000000"},
                {"local_mismatch", "504b030414000000080000002100b71d88c008000000060000000c00000058646b2f62696e2f746f6f6ccb4b2cc92c4b0500504b030414000000080000002100ab461a990a000000080000000900000073646b2f616c6961734bcaccd32fc9cfcf0100504b0102140314000000080000002100b71d88c008000000060000000c0000000000000000000000ed810000000073646b2f62696e2f746f6f6c504b0102140314000000080000002100ab461a990a00000008000000090000000000000000000000eda13200000073646b2f616c696173504b0506000000000200020071000000630000000000"},
                {"unicode_extra", "504b0304140000000000000021001f28aa6702000000020000000800150073646b2f736166657570110001000000002e2e2f2e2e2f6573636170656e6f504b01021403140000000000000021001f28aa67020000000200000008001500000000000000000080010000000073646b2f736166657570110001000000002e2e2f2e2e2f657363617065504b050600000000010001004b0000003d0000000000"},
                {"truncated", "504b030414000000080000002100b71d88c008000000060000000c00000073646b2f62696e2f746f6f6ccb4b2cc92c4b0500504b030414000000080000002100ab461a990a000000080000000900000073646b2f616c6961734bcaccd32fc9cfcf0100504b0102140314000000080000002100b71d88c008000000060000000c0000000000000000000000ed810000000073646b2f62696e2f746f6f6c504b0102140314000000080000002100ab461a990a00000008000000090000000000000000000000eda13200000073646b2f616c696173504b05060000000002000200710000006300000000"},
        };
        // clang-format on

        for (const auto &[name, hex] : fixtures) {
            const auto file = root / (name + ".zip");
            {
                std::ofstream output(file, std::ios::binary);
                output << decode(hex);
            }
            bool rejected = false;
            std::vector<ZipMember> entries;
            try {
                entries = zip_layout(file);
            } catch (...) {
                rejected = true;
            }

            if (name == "good") {
                check(!rejected && entries.size() == 2 && entries[0].bytes == 6 &&
                              entries[1].symlink,
                      "regular and symlink metadata preserve exact extents");
            } else if (name == "deflate_level") {
                check(!rejected && entries.size() == 1 && entries[0].bytes == 15,
                      "deflate compression level flags accepted");
            } else if (name == "zip64_end") {
                check(!rejected && entries.size() == 1 && entries[0].bytes == 4,
                      "full ZIP64 end and locator preserved");
            } else if (name == "zip64_local") {
                check(!rejected && entries.size() == 1 && entries[0].bytes == 6,
                      "ZIP64 local size sentinel accepted with bounded central metadata");
            } else {
                check(rejected, "unsafe duplicate mismatched or truncated ZIP rejected");
            }
        }

        const auto aliases = zip_layout(root / "case.zip", ZipNamePolicy::exact);
        check(aliases.size() == 2 && aliases[0].name == "sdk/A" && aliases[1].name == "sdk/a",
              "logical APK names preserve case");
        bool extraction_rejected = false;
        try {
            extract_pinned_zip_tree(root / "case.zip", sha256_file(root / "case.zip"),
                                    {{"sdk/A", root / "portable-output", 3}}, 0);
        } catch (...) {
            extraction_rejected = true;
        }

        check(extraction_rejected && !std::filesystem::exists(root / "portable-output"),
              "filesystem extraction retains portable casefold protection");

        for (const auto *name :
             {"escape", "reserved", "parent", "local_mismatch", "unicode_extra", "truncated"}) {
            bool rejected = false;
            try {
                (void)zip_layout(root / (std::string(name) + ".zip"), ZipNamePolicy::exact);
            } catch (...) {
                rejected = true;
            }

            check(rejected,
                  "logical APK indexing retains paths, parent, local headers and extent protections");
            bool read_rejected = false;
            const auto unsafe = root / (std::string(name) + ".zip");
            try {
                (void)read_pinned_zip_entry(unsafe, sha256_file(unsafe), "sdk/bin/tool", 1024U,
                                            ZipNamePolicy::exact);
            } catch (...) {
                read_rejected = true;
            }

            check(read_rejected, "exact selected-byte reader retains structural protections");
        }

        auto duplicate = decode(fixtures.at("case"));

        for (std::size_t at = 0; (at = duplicate.find("sdk/A", at)) != std::string::npos; at += 5) {
            duplicate.replace(at, 5, "sdk/a");
        }

        const auto duplicate_file = root / "exact-duplicate.zip";
        {
            std::ofstream output(duplicate_file, std::ios::binary);
            output << duplicate;
        }
        bool duplicate_rejected = false;
        try {
            (void)zip_layout(duplicate_file, ZipNamePolicy::exact);
        } catch (...) {
            duplicate_rejected = true;
        }

        check(duplicate_rejected, "exact duplicate rejected by logical APK index");
        std::cout << "portable ZIP/ZIP64 structural index and local/central agreement passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
