#include "sdk_archive.hpp"
#include "process.hpp"
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
        check(argc == 1, "usage: no arguments");
        const auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                          ("voyemu-sdk-archive-" + random_token());
        ensure_private_directory(root);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::filesystem::remove_all(path);
            }
        } cleanup{root};
        const std::map<std::string, std::string> fixtures = {{"good",
                                                              "504b030414000000080000002100b71d88c008000000060000000c00000073646b2f62696e2f746f6f6ccb4b2cc92c4b0500504b030414000000080000002100d13ef32006000000040000000d00000073646b2f62696e2f616c6961732bc9cfcf0100504b030414000000080000002100946b6ce107000000050000000d00000073646b2f62696e2f636861696e4bccc94c2c0600504b030414000000080000002100f9482c9408000000060000001400000073646b2f6c69622f756e6e65656465642e6a61722bcd2b2d4e4d0100504b0304140000000800000021005cbf44280a000000080000001700000073646b2f707974686f6e332f62696e2f707974686f6e332bcdcb4b4d4d494d0100504b0304140000000800000021000841f57b110000000f0000001500000073646b2f736f757263652e70726f706572746965730bc84ed70b4a2dcb2ccecccfb335e40200504b0102140314000000080000002100b71d88c008000000060000000c0000000000000000000000ed810000000073646b2f62696e2f746f6f6c504b0102140314000000080000002100d13ef32006000000040000000d0000000000000000000000eda13200000073646b2f62696e2f616c696173504b0102140314000000080000002100946b6ce107000000050000000d0000000000000000000000eda16300000073646b2f62696e2f636861696e504b0102140314000000080000002100f9482c940800000006000000140000000000000000000000ed819500000073646b2f6c69622f756e6e65656465642e6a6172504b01021403140000000800000021005cbf44280a00000008000000170000000000000000000000ed81cf00000073646b2f707974686f6e332f62696e2f707974686f6e33504b01021403140000000800000021000841f57b110000000f000000150000000000000000000000ed810e01000073646b2f736f757263652e70726f70657274696573504b050600000000060006007a010000520100000000"},
                                                             {"escape",
                                                              "504b030414000000080000002100fb392b8205000000030000000d00000073646b2f2e2e2f6573636170654b4a4c0100504b0102140314000000080000002100fb392b8205000000030000000d0000000000000000000000ed810000000073646b2f2e2e2f657363617065504b050600000000010001003b000000300000000000"},
                                                             {"link_escape",
                                                              "504b030414000000080000002100404affb10f0000000d0000000800000073646b2f6c696e6bd3d3d3d7d3d3cf2f2d29ce4c490500504b0102140314000000080000002100404affb10f0000000d000000080000000000000000000000eda10000000073646b2f6c696e6b504b0506000000000100010036000000350000000000"},
                                                             {"cycle",
                                                              "504b030414000000080000002100f9efbe7103000000010000000500000073646b2f614b0200504b03041400000008000000210043beb7e803000000010000000500000073646b2f624b0400504b0102140314000000080000002100f9efbe710300000001000000050000000000000000000000eda10000000073646b2f61504b010214031400000008000000210043beb7e80300000001000000050000000000000000000000eda12600000073646b2f62504b05060000000002000200660000004c0000000000"},
                                                             {"parent",
                                                              "504b030414000000080000002100b555ad290b000000090000000700000073646b2f62696e4bcd294e2dcf482d4a0500504b030414000000080000002100fb392b8205000000030000000c00000073646b2f62696e2f746f6f6c4b4a4c0100504b030414000000080000002100b71d88c008000000060000000d00000073646b2f656c73657768657265cb4b2cc92c4b0500504b0102140314000000080000002100b555ad290b00000009000000070000000000000000000000eda10000000073646b2f62696e504b0102140314000000080000002100fb392b8205000000030000000c0000000000000000000000ed813000000073646b2f62696e2f746f6f6c504b0102140314000000080000002100b71d88c008000000060000000d0000000000000000000000ed815f00000073646b2f656c73657768657265504b05060000000003000300aa000000920000000000"},
                                                             {"case",
                                                              "504b03041400000008000000210043beb7e803000000010000000500000073646b2f414b0400504b030414000000080000002100f9efbe7103000000010000000500000073646b2f614b0200504b010214031400000008000000210043beb7e80300000001000000050000000000000000000000ed810000000073646b2f41504b0102140314000000080000002100f9efbe710300000001000000050000000000000000000000ed812600000073646b2f61504b05060000000002000200660000004c0000000000"},
                                                             {"omitted",
                                                              "504b030414000000080000002100078463d00e0000000c0000000900000073646b2f616c6961732ba82cc9c8cf33d62fc9cfcf0100504b030414000000080000002100f9482c9408000000060000001000000073646b2f707974686f6e332f746f6f6c2bcd2b2d4e4d0100504b0102140314000000080000002100078463d00e0000000c000000090000000000000000000000eda10000000073646b2f616c696173504b0102140314000000080000002100f9482c940800000006000000100000000000000000000000ed813500000073646b2f707974686f6e332f746f6f6c504b05060000000002000200750000006b0000000000"},
                                                             {"two_roots",
                                                              "504b03041400000008000000210043beb7e80300000001000000050000006f6e652f614b0400504b03041400000008000000210043beb7e803000000010000000500000074776f2f614b0400504b010214031400000008000000210043beb7e80300000001000000050000000000000000000000ed81000000006f6e652f61504b010214031400000008000000210043beb7e80300000001000000050000000000000000000000ed812600000074776f2f61504b05060000000002000200660000004c0000000000"}};

        for (const auto &[name, hex] : fixtures) {
            const auto file = root / (name + ".zip");
            {
                std::ofstream output(file, std::ios::binary);
                output << decode(hex);
            }
            const auto destination = root / name;
            bool failed = false;
            Json result;
            try {
                result = extract_sdk_archive(file, sha256_file(file), destination, 0, 1 << 20);
            } catch (const std::exception &) {
                failed = true;
            }

            if (name == "good") {
                check(!failed && result.at("files") == 4 && result.at("omitted") == 2,
                      "native payload and aliases extracted without interpreters");

                for (const auto *item : {"tool", "alias", "chain"}) {
                    check(sha256_file(destination / "bin" / item) == sha256_bytes("native") &&
                                  !std::filesystem::is_symlink(destination / "bin" / item),
                          "aliases are ordinary exact copies");
                }

                check(!std::filesystem::exists(destination / "lib/unneeded.jar") &&
                              !std::filesystem::exists(destination / "python3"),
                      "host Java/Python payload omitted");
                const auto flat = root / "flat";
                const auto flat_result = extract_sdk_archive(file, sha256_file(file), flat, 0,
                                                             1 << 20, false);
                check(flat_result.at("files") == 4 &&
                              std::filesystem::exists(flat / "sdk/bin/tool"),
                      "explicit flat layout retains first path component");
                bool overwrite = false;
                try {
                    extract_sdk_archive(file, sha256_file(file), destination, 0, 1 << 20);
                } catch (...) {
                    overwrite = true;
                }

                check(overwrite, "existing install preserved");
                bool pin = false;
                try {
                    extract_sdk_archive(file, std::string(64, '0'), root / "bad-pin", 0, 1 << 20);
                } catch (...) {
                    pin = true;
                }

                check(pin && !std::filesystem::exists(root / "bad-pin"),
                      "pin checked before output");
                bool bound = false;
                try {
                    extract_sdk_archive(file, sha256_file(file), root / "small-bound", 0, 8);
                } catch (...) {
                    bound = true;
                }

                check(bound && !std::filesystem::exists(root / "small-bound"),
                      "materialized aliases included in size budget");
            } else {
                check(failed && !std::filesystem::exists(destination),
                      "unsafe archive cannot create output");
            }
        }

        const auto archive = root / "two_roots.zip", installed = root / "two-roots-installed";
        extract_sdk_archive(archive, sha256_file(archive), installed, 0, 1 << 20, false);
        check(std::filesystem::is_regular_file(installed / "one/a"),
              "archive with two roots extracted without stripping");
        check(!std::filesystem::exists(root / "escape"), "no traversal output");
        std::cout
                << "native SDK extraction, alias materialization, path and expanded-size guards passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
