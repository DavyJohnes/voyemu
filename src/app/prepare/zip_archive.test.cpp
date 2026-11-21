#include "zip_archive.hpp"
#include "model.hpp"
#include "os/platform.hpp"
#include "process.hpp"
#include "progress.hpp"
#include "quickboot.hpp"
#include "sdk_archive.hpp"
#include "zip_layout.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
using namespace voyemu;

static void check(bool value, const char *text) {
    if (!value) {
        throw std::runtime_error(text);
    }
}

// Python zipfile vectors of an SDK component: an executable, a link to it, a link to that link and
// a 256 KiB zero member whose last input the decoder consumes while output is still pending; and a
// link that escapes the component.
constexpr char sdk_links_hex[] =
        "504b030414000000000000006d5d0000000000000000000000000400000073646b2f504b030414000000000000006d"
        "5d0841f57b0f0000000f0000001500000073646b2f736f757263652e70726f70657274696573506b672e5265766973"
        "696f6e3d310a504b030414000000080000006d5def1723c307000000050000000c00000073646b2f62696e2f746f6f"
        "6c2bc9cfcfe10200504b030414000000000000006d5dd13ef32004000000040000000d00000073646b2f62696e2f61"
        "6c696173746f6f6c504b030414000000000000006d5d653c65570c0000000c0000000d00000073646b2f6c69622f63"
        "6861696e2e2e2f62696e2f616c696173504b030414000000080000006d5d22ea0ee20f010000000004001100000073"
        "646b2f6c69622f7a65726f732e62696eedc13101000000c2a0f54fed6d07a000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
        "000000de00504b0102140314000000000000006d5d000000000000000000000000040000000000000000000000ed41"
        "0000000073646b2f504b0102140314000000000000006d5d0841f57b0f0000000f0000001500000000000000000000"
        "00a4812200000073646b2f736f757263652e70726f70657274696573504b0102140314000000080000006d5def1723"
        "c307000000050000000c0000000000000000000000ed816400000073646b2f62696e2f746f6f6c504b010214031400"
        "0000000000006d5dd13ef32004000000040000000d0000000000000000000000ffa19500000073646b2f62696e2f61"
        "6c696173504b0102140314000000000000006d5d653c65570c0000000c0000000d0000000000000000000000ffa1c4"
        "00000073646b2f6c69622f636861696e504b0102140314000000080000006d5d22ea0ee20f01000000000400110000"
        "000000000000000000a481fb00000073646b2f6c69622f7a65726f732e62696e504b05060000000006000600640100"
        "00390200000000";
constexpr char sdk_escape_hex[] =
        "504b030414000000000000006d5d0000000000000000000000000400000073646b2f504b030414000000080000006d"
        "5def1723c307000000050000000c00000073646b2f62696e2f746f6f6c2bc9cfcfe10200504b030414000000000000"
        "006d5d404affb10d0000000d0000000e00000073646b2f62696e2f6573636170652e2e2f2e2e2f6f75747369646550"
        "4b0102140314000000000000006d5d000000000000000000000000040000000000000000000000ed41000000007364"
        "6b2f504b0102140314000000080000006d5def1723c307000000050000000c0000000000000000000000ed81220000"
        "0073646b2f62696e2f746f6f6c504b0102140314000000000000006d5d404affb10d0000000d0000000e0000000000"
        "000000000000ffa15300000073646b2f62696e2f657363617065504b05060000000003000300a80000008c00000000"
        "00";
// The case aliases xt_MARK.h and xt_mark.h, as in the Linux NDK sysroot.
constexpr char sdk_case_hex[] =
        "504b030414000000000000006d5d0000000000000000000000000400000073646b2f504b030414000000000000006d"
        "5d238600cb06000000060000000d00000073646b2f78745f4d41524b2e6875707065720a504b030414000000000000"
        "006d5d9aedbe9006000000060000000d00000073646b2f78745f6d61726b2e686c6f7765720a504b01021403140000"
        "00000000006d5d000000000000000000000000040000000000000000000000ed410000000073646b2f504b01021403"
        "14000000000000006d5d238600cb06000000060000000d0000000000000000000000a4812200000073646b2f78745f"
        "4d41524b2e68504b0102140314000000000000006d5d9aedbe9006000000060000000d0000000000000000000000a4"
        "815300000073646b2f78745f6d61726b2e68504b05060000000003000300a8000000840000000000";
// Independently generated Python zipfile vectors: stored/deflated and UTF-8 names.
constexpr char good_hex[] =
        "504b030414000000080083314c5d12e573450e0000000c00000013000000416e64726f69644d616e69666573742e78"
        "6d6ccb4b2cc92c4b65a8ca2cf8cf0500504b030414000008080083314c5d03ea60490d0000000b0000001700000061"
        "73736574732fd0b4d0b0d0bdd0bdd18bd0b52e747874cb2fcf5348cbac28292d4a0500504b01021403140000000800"
        "83314c5d12e573450e0000000c000000130000000000000000000000800100000000416e64726f69644d616e696665"
        "73742e786d6c504b0102140314000008080083314c5d03ea60490d0000000b00000017000000000000000000000080"
        "013f0000006173736574732fd0b4d0b0d0bdd0bdd18bd0b52e747874504b0506000000000200020086000000810000"
        "000000";
constexpr char duplicate_hex[] =
        "504b030414000000000083314c5d12e573450c0000000c00000013000000416e64726f69644d616e69666573742e78"
        "6d6c6e6174697665007a6970ff0a504b030414000000000083314c5d12e573450c0000000c00000013000000416e64"
        "726f69644d616e69666573742e786d6c6e6174697665007a6970ff0a504b0102140314000000000083314c5d12e573"
        "450c0000000c000000130000000000000000000000800100000000416e64726f69644d616e69666573742e786d6c50"
        "4b0102140314000000000083314c5d12e573450c0000000c00000013000000000000000000000080013d000000416e"
        "64726f69644d616e69666573742e786d6c504b05060000000002000200820000007a0000000000";
constexpr char traversal_hex[] =
        "504b030414000000000083314c5d12e573450c0000000c000000090000002e2e2f6573636170656e6174697665007a"
        "6970ff0a504b0102140314000000000083314c5d12e573450c0000000c000000090000000000000000000000800100"
        "0000002e2e2f657363617065504b0506000000000100010037000000330000000000";
constexpr char symlink_hex[] =
        "504b030414000000000000002100fc2f6f460600000006000000040000006c696e6b746172676574504b0102140314"
        "000000000000002100fc2f6f460600000006000000040000000000000000000000ffa1000000006c696e6b504b0506"
        "000000000100010032000000280000000000";
constexpr char crc_hex[] =
        "504b030414000000000083314c5d12e573450c0000000c00000013000000416e64726f69644d616e69666573742e78"
        "6d6c6f6174697665007a6970ff0a504b0102140314000000000083314c5d12e573450c0000000c0000001300000000"
        "00000000000000800100000000416e64726f69644d616e69666573742e786d6c504b05060000000001000100410000"
        "003d0000000000";
// Independently generated Python zipfile vector for the portable rewriter: two stored native
// entries, one stored resource table, one deflated manifest and two deflated asset names.
constexpr char rewrite_hex[] =
        "504b030414000000000000002158712836b518000000180000000e0000007265736f75726365732e6172736341525343000102037265736f75726365"
        "2d7461626c65fffe504b03041400000000000000215880171b1348000000480000000b000000636c61737365732e6465786465780a30333500000102"
        "030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e"
        "3f504b03041400000000000000215847455cb52c0000002c0000001a0000006c69622f61726d36342d7638612f6c69626e61746976652e736f7f454c"
        "46020101006e61746976656c69626e61746976656c69626e61746976656c69626e61746976656c6962504b03041400000008000000215835b8f98424"
        "0000002200000013000000416e64726f69644d616e69666573742e786d6cb3c94dcccb4c4b2d2e5128484cce4e4c4fb5554acecfd54bad48cc2dc849"
        "55d2b7e30200504b030414000000080000002158652b95971200000027000000140000006173736574732f72656d6f7661626c652e62696e4b49cd49"
        "2d49d5cd4d656064624ec1c90100504b03041400000808000000215820e44e541900000039000000170000006173736574732fd0b4d0b0d0bdd0bdd1"
        "8bd0b52e747874bbb0ff62c3851d17365dd87ab149a13cbf282785eb02514200504b0102140314000000000000002158712836b51800000018000000"
        "0e0000000000000000000000a481000000007265736f75726365732e61727363504b010214031400000000000000215880171b134800000048000000"
        "0b0000000000000000000000a48144000000636c61737365732e646578504b010214031400000000000000215847455cb52c0000002c0000001a0000"
        "000000000000000000a481b50000006c69622f61726d36342d7638612f6c69626e61746976652e736f504b010214031400000008000000215835b8f9"
        "842400000022000000130000000000000000000000a48119010000416e64726f69644d616e69666573742e786d6c504b010214031400000008000000"
        "2158652b95971200000027000000140000000000000000000000a4816e0100006173736574732f72656d6f7661626c652e62696e504b010214031400"
        "000808000000215820e44e541900000039000000170000000000000000000000a481b20100006173736574732fd0b4d0b0d0bdd0bdd18bd0b52e7478"
        "74504b0506000000000600060085010000000200000000";

static std::uint32_t crc32_of(const std::string &bytes) {
    std::uint32_t crc = 0xffffffffu;

    for (const unsigned char byte : bytes) {
        crc ^= byte;

        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }

    return ~crc;
}

static std::string decode(const std::string &hex) {
    std::string bytes;

    for (std::size_t i = 0; i < hex.size(); i += 2) {
        bytes += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
    }

    return bytes;
}

int main() {
    try {
        const auto directory = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                               ("voyemu-zip-" + random_token());
        ensure_private_directory(directory);
        struct Cleanup {
            std::filesystem::path path;

            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{directory};
        auto store = [&](const std::string &name, const std::string &bytes) {
            auto path = directory / name;
            std::ofstream file(path, std::ios::binary);
            file.write(bytes.data(), bytes.size());
            file.close();
            check(bool(file), "fixture persisted");

            return path;
        };
        const auto good = store("good.zip", decode(good_hex));
        const auto pin = sha256_file(good);
        const std::string expected("native\0zip\xff\n", 12);
        const auto tree_file = directory / "tree-manifest";
        extract_pinned_zip_tree(good, pin, {{"AndroidManifest.xml", tree_file, 12}}, 0);
        check(sha256_file(tree_file) == sha256_bytes(expected),
              "tree streaming preserves deflated bytes");
        bool tree_overwrite = false;
        try {
            extract_pinned_zip_tree(good, pin, {{"AndroidManifest.xml", tree_file, 12}}, 0);
        } catch (...) {
            tree_overwrite = true;
        }

        check(tree_overwrite && sha256_file(tree_file) == sha256_bytes(expected),
              "tree rejects existing output");
        const auto bad_tree = store("tree-crc.zip", decode(crc_hex));
        bool tree_crc = false;
        try {
            extract_pinned_zip_tree(bad_tree, sha256_file(bad_tree),
                                    {{"AndroidManifest.xml", directory / "bad-tree", 12}}, 0);
        } catch (...) {
            tree_crc = true;
        }

        check(tree_crc, "tree rejects corrupt CRC");
        const auto links = store("sdk-links.zip", decode(sdk_links_hex));
        const auto component = directory / "sdk-links";
        extract_sdk_archive(links, sha256_file(links), component, 0, 1 << 20, true);
        auto text = [](const std::filesystem::path &file) {
            std::ifstream stream(file, std::ios::binary);

            return std::string((std::istreambuf_iterator<char>(stream)), {});
        };
        check(text(component / "bin/tool") == "tool\n" &&
                      text(component / "bin/alias") == "tool\n" &&
                      text(component / "lib/chain") == "tool\n" &&
                      !std::filesystem::is_symlink(component / "bin/alias") &&
                      !std::filesystem::is_symlink(component / "lib/chain"),
              "SDK links become copies of their final target");
        check(text(component / "lib/zeros.bin") == std::string(256 * 1024, '\0'),
              "SDK member with pending decoder output extracts completely");

        if (os::executable_format != "PE") {
            const auto mode = [](const std::filesystem::path &file) {
                return std::filesystem::status(file).permissions() &
                       std::filesystem::perms::owner_exec;
            };
            check(mode(component / "bin/tool") != std::filesystem::perms::none &&
                          mode(component / "source.properties") == std::filesystem::perms::none,
                  "SDK executables keep their executable mode");
        }

        // The aliases extract to a case-sensitive volume and stop on a case-insensitive one.
        std::ofstream(directory / "probe") << "case";
        const bool insensitive = std::filesystem::exists(directory / "PROBE");
        const auto aliases = store("sdk-case.zip", decode(sdk_case_hex));
        bool aliased = true;
        try {
            extract_sdk_archive(aliases, sha256_file(aliases), directory / "sdk-case", 0, 1 << 20,
                                true);
            aliased = text(directory / "sdk-case/xt_MARK.h") == "upper\n" &&
                      text(directory / "sdk-case/xt_mark.h") == "lower\n";
        } catch (const std::runtime_error &) {
            aliased = false;
        }

        check(aliased != insensitive, "SDK case aliases follow the volume case sensitivity");
        const auto escape = store("sdk-escape.zip", decode(sdk_escape_hex));
        bool escaped = false;
        try {
            extract_sdk_archive(escape, sha256_file(escape), directory / "sdk-escape", 0, 1 << 20,
                                true);
        } catch (const std::runtime_error &) {
            escaped = true;
        }

        check(escaped && !std::filesystem::exists(directory / "outside") &&
                      !std::filesystem::exists(directory / "sdk-escape"),
              "SDK link outside the component rejected");
#ifdef _WIN32
        const auto sdk = store(
                "sdk.zip",
                decode("504b0304140000000800b380625d86a6103607000000050000000c00000073646b2f62696e2f746f6f6ccb48cdc9c90700504b0304140000000000b380625d0000000000000000000000000d00000073646b2f62696e2f656d707479504b0304140000000000b380625d2e272a2507000000070000001200000073646b2f6c69622f756e757365642e6a61726f6d6974746564504b01021403140000000800b380625d86a6103607000000050000000c000000000000000000000080010000000073646b2f62696e2f746f6f6c504b01021403140000000000b380625d0000000000000000000000000d000000000000000000000080013100000073646b2f62696e2f656d707479504b01021403140000000000b380625d2e272a25070000000700000012000000000000000000000080015c00000073646b2f6c69622f756e757365642e6a6172504b05060000000003000300b5000000930000000000"));
        const auto sdk_out = directory / "sdk-tree";
        const auto sdk_result = extract_sdk_archive(sdk, sha256_file(sdk), sdk_out, 0, 1024);
        check(sdk_result.at("files") == 2 &&
                      sha256_file(sdk_out / "bin/tool") == sha256_bytes("hello") &&
                      std::filesystem::file_size(sdk_out / "bin/empty") == 0 &&
                      !std::filesystem::exists(sdk_out / "lib/unused.jar"),
              "Windows pinned SDK extracts compressed and empty files and excludes jar");
#endif
        auto entries = pinned_zip_inventory(good, pin);
        check(read_pinned_zip_entry(good, pin, "AndroidManifest.xml", std::size_t{32}) == expected,
              "archive-pinned payload read");

        for (unsigned bad = 0; bad < 4; ++bad) {
            bool rejected = false;
            try {
                read_pinned_zip_entry(good, bad == 0 ? std::string(64, '0') : pin,
                                      bad == 1 ? "missing" : "AndroidManifest.xml",
                                      bad == 2   ? 0U
                                      : bad == 3 ? 1U
                                                 : 32U);
            } catch (...) {
                rejected = true;
            }

            check(rejected, "archive-pinned payload inverse guard");
        }

        check(entries.size() == 2 && entries[0].name == "AndroidManifest.xml" &&
                      entries[1].name == "assets/данные.txt",
              "ordinary UTF8 ZIP inventory preserved");
        check(read_pinned_zip_entry(good, pin, "AndroidManifest.xml", sha256_bytes(expected)) ==
                      expected,
              "deflated binary entry survives NUL and nonUTF8 data");

        for (unsigned mode = 0; mode < 4; ++mode) {
            bool rejected = false;
            try {
                if (mode == 0) {
                    read_pinned_zip_entry(good, std::string(64, '0'), "AndroidManifest.xml",
                                          sha256_bytes(expected));
                }

                if (mode == 1) {
                    read_pinned_zip_entry(good, pin, "AndroidManifest.xml", std::string(64, '0'));
                }

                if (mode == 2) {
                    read_pinned_zip_entry(good, pin, "missing", sha256_bytes(expected));
                }

                if (mode == 3) {
                    read_pinned_zip_entry(good, pin, "AndroidManifest.xml", sha256_bytes(expected),
                                          4);
                }
            } catch (...) {
                rejected = true;
            }

            check(rejected, "wrong archive/entry pin missing entry and small bound rejected");
        }

        for (const auto &item :
             std::vector<std::pair<std::string, std::string>>{{"duplicate", duplicate_hex},
                                                              {"traversal", traversal_hex},
                                                              {"symlink", symlink_hex},
                                                              {"crc", crc_hex}}) {
            const auto file = store(item.first + ".zip", decode(item.second));
            bool rejected = false;
            try {
                read_pinned_zip_entry(file, sha256_file(file), "AndroidManifest.xml",
                                      sha256_bytes(expected));
            } catch (...) {
                rejected = true;
            }

            check(rejected, "duplicate unsafe linked or corrupt archive rejected");
        }

        const std::string replacement("updated\0manifest", 16), added("own\0native", 10);
        const auto candidate = directory / "candidate.apk";
        const std::vector<ZipReplacement> replacements{{"AndroidManifest.xml",
                                                        sha256_bytes(expected), replacement,
                                                        sha256_bytes(replacement)}};
        const std::vector<ZipAddition> additions{
                {"lib/arm64-v8a/libown.so", added, sha256_bytes(added)}};
        const auto rewritten = rewrite_pinned_zip(good, pin, replacements, {"assets/данные.txt"},
                                                  additions, candidate, 0);
        check(sha256_file(candidate) == rewritten, "rewriter returns actual published hash");
        std::ifstream candidate_file(candidate, std::ios::binary);
        const std::string candidate_bytes((std::istreambuf_iterator<char>(candidate_file)), {});
        check(candidate_bytes.find(std::string("PK\x06\x06", 4)) == std::string::npos &&
                      candidate_bytes.find(std::string("PK\x06\x07", 4)) == std::string::npos,
              "APK rewrite does not emit ZIP64 end records");
        check(read_pinned_zip_entry(candidate, rewritten, "AndroidManifest.xml",
                                    sha256_bytes(replacement)) == replacement,
              "replacement payload matches declared bytes");
        check(read_pinned_zip_entry(candidate, rewritten, "lib/arm64-v8a/libown.so",
                                    sha256_bytes(added)) == added,
              "added native payload survives NUL");
        const auto rewritten_entries = pinned_zip_inventory(candidate, rewritten);
        check(rewritten_entries.size() == 2, "exact removal applied");
        bool overwrite = false;
        try {
            rewrite_pinned_zip(good, pin, {}, {}, {}, candidate, 0);
        } catch (...) {
            overwrite = true;
        }

        check(overwrite && sha256_file(candidate) == rewritten, "existing output never replaced");
        const auto preserved = directory / "preserved.zip";
        const auto preserved_pin = rewrite_pinned_zip(good, pin, {}, {}, {}, preserved, 0);
        check(read_pinned_zip_entry(preserved, preserved_pin, "AndroidManifest.xml",
                                    sha256_bytes(expected)) == expected,
              "untouched entry content preserved");
        check(read_pinned_zip_entry(preserved, preserved_pin, "assets/данные.txt",
                                    sha256_bytes("own fixture")) == "own fixture",
              "untouched UTF8 entry preserved");

        for (unsigned mode = 0; mode < 4; ++mode) {
            const auto failed = directory / ("failed-" + std::to_string(mode));
            bool rejected = false;
            try {
                if (mode == 0) {
                    rewrite_pinned_zip(good, pin,
                                       {{"AndroidManifest.xml", std::string(64, '0'), replacement,
                                         sha256_bytes(replacement)}},
                                       {}, {}, failed, 0);
                }

                if (mode == 1) {
                    rewrite_pinned_zip(good, pin, {}, {"missing"}, {}, failed, 0);
                }

                if (mode == 2) {
                    rewrite_pinned_zip(good, pin, {}, {},
                                       {{"AndroidManifest.xml", added, sha256_bytes(added)}},
                                       failed, 0);
                }

                if (mode == 3) {
                    rewrite_pinned_zip(good, pin, {}, {}, {}, failed,
                                       std::numeric_limits<std::uint64_t>::max());
                }
            } catch (...) {
                rejected = true;
            }

            check(rejected && !std::filesystem::exists(failed),
                  "bad pin missing removal colliding addition and insufficient reserve create no "
                  "output");
        }

        const auto corrupt = directory / "crc.zip", failed_crc = directory / "failed-crc";
        bool corrupt_rejected = false;
        try {
            rewrite_pinned_zip(corrupt, sha256_file(corrupt), {}, {}, {}, failed_crc, 0);
        } catch (...) {
            corrupt_rejected = true;
        }

        check(corrupt_rejected && !std::filesystem::exists(failed_crc),
              "streaming CRC failure cannot publish partial output");

        for (const auto &item : std::filesystem::directory_iterator(directory)) {
            check(item.path().filename().string().find(".pending-") == std::string::npos,
                  "rewrite leaves no staging files");
        }
        // Portable rewriter scenario: replace, remove, add and preserve entries; every result is
        // read back through the independent reader and the structural index.
        {
            const auto fixture = store("rewrite.zip", decode(rewrite_hex));
            const auto fixture_pin = sha256_file(fixture);
            const std::string manifest_new("<manifest package=\"com.rewritten\"/>\n");
            const std::string added_so("own\0native", 10);
            const std::string added_utf8("\xd0\xbd\xd0\xb0\xd0\xb1\xd0\xbe\xd1\x80.bin");
            const std::string added_utf8_name = "assets/" + added_utf8;
            const auto original_manifest = read_pinned_zip_entry(
                    fixture, fixture_pin, "AndroidManifest.xml", std::size_t{1024});
            const auto preserved_arsc = read_pinned_zip_entry(fixture, fixture_pin,
                                                              "resources.arsc", std::size_t{1024});
            const auto preserved_dex = read_pinned_zip_entry(fixture, fixture_pin, "classes.dex",
                                                             std::size_t{1024});
            const auto preserved_so = read_pinned_zip_entry(
                    fixture, fixture_pin, "lib/arm64-v8a/libnative.so", std::size_t{1024});
            const auto preserved_utf8 = read_pinned_zip_entry(
                    fixture, fixture_pin, "assets/данные.txt", std::size_t{1024});
            const auto candidate = directory / "rewrite.apk";
            const auto rewritten = rewrite_pinned_zip(
                    fixture, fixture_pin,
                    {{"AndroidManifest.xml", sha256_bytes(original_manifest), manifest_new,
                      sha256_bytes(manifest_new)}},
                    {"assets/removable.bin"},
                    {{"lib/arm64-v8a/libadded.so", added_so, sha256_bytes(added_so)},
                     {added_utf8_name, added_utf8, sha256_bytes(added_utf8)}},
                    candidate, 0);
            check(sha256_file(candidate) == rewritten, "portable rewrite returns published hash");
            std::ifstream candidate_file(candidate, std::ios::binary);
            const std::string candidate_bytes((std::istreambuf_iterator<char>(candidate_file)), {});
            check(candidate_bytes.find(std::string("PK\x06\x06", 4)) == std::string::npos &&
                          candidate_bytes.find(std::string("PK\x06\x07", 4)) == std::string::npos,
                  "portable rewrite emits no ZIP64 records");
            const auto result_entries = pinned_zip_inventory(candidate, rewritten);
            check(result_entries.size() == 7, "portable rewrite entry count");
            bool removed_present = false;

            for (const auto &entry : result_entries) {
                if (entry.name == "assets/removable.bin") {
                    removed_present = true;
                }
            }

            check(!removed_present, "removed entry absent from result");
            check(read_pinned_zip_entry(candidate, rewritten, "AndroidManifest.xml",
                                        sha256_bytes(manifest_new)) == manifest_new,
                  "replaced payload matches declared bytes");
            check(read_pinned_zip_entry(candidate, rewritten, "lib/arm64-v8a/libadded.so",
                                        sha256_bytes(added_so)) == added_so,
                  "added store payload round-trips");
            check(read_pinned_zip_entry(candidate, rewritten, added_utf8_name,
                                        sha256_bytes(added_utf8)) == added_utf8,
                  "added non-ASCII payload round-trips");
            check(read_pinned_zip_entry(candidate, rewritten, "resources.arsc",
                                        sha256_bytes(preserved_arsc)) == preserved_arsc,
                  "preserved store payload round-trips");
            check(read_pinned_zip_entry(candidate, rewritten, "classes.dex",
                                        sha256_bytes(preserved_dex)) == preserved_dex,
                  "preserved dex payload round-trips");
            check(read_pinned_zip_entry(candidate, rewritten, "lib/arm64-v8a/libnative.so",
                                        sha256_bytes(preserved_so)) == preserved_so,
                  "preserved native payload round-trips");
            check(read_pinned_zip_entry(candidate, rewritten, "assets/данные.txt",
                                        sha256_bytes(preserved_utf8)) == preserved_utf8,
                  "preserved non-ASCII payload round-trips");
            const auto result_layout = zip_layout(candidate);

            for (const auto &member : result_layout) {
                const bool stored =
                        member.name == "resources.arsc" || member.name.ends_with(".dex") ||
                        (member.name.starts_with("lib/") && member.name.ends_with(".so"));
                check(member.method == (stored ? 0U : 8U),
                      "compression policy matches macOS rules");

                if (stored) {
                    check(member.compressed_bytes == member.bytes, "stored extent is uncompressed");
                }

                std::string expected;

                if (member.name == "AndroidManifest.xml") {
                    expected = manifest_new;
                } else if (member.name == "lib/arm64-v8a/libadded.so") {
                    expected = added_so;
                } else if (member.name == added_utf8_name) {
                    expected = added_utf8;
                } else if (member.name == "resources.arsc") {
                    expected = preserved_arsc;
                } else if (member.name == "classes.dex") {
                    expected = preserved_dex;
                } else if (member.name == "lib/arm64-v8a/libnative.so") {
                    expected = preserved_so;
                } else if (member.name == "assets/данные.txt") {
                    expected = preserved_utf8;
                }

                check(member.bytes == expected.size() && member.crc == crc32_of(expected),
                      "entry size and CRC recorded in the index");
            }

            bool overwrite = false;
            try {
                rewrite_pinned_zip(fixture, fixture_pin, {}, {}, {}, candidate, 0);
            } catch (...) {
                overwrite = true;
            }

            check(overwrite && sha256_file(candidate) == rewritten,
                  "existing portable destination never replaced");

            for (const auto &item : std::filesystem::directory_iterator(directory)) {
                check(item.path().filename().string().find(".pending-") == std::string::npos,
                      "portable rewrite leaves no staging files");
            }

            check(sha256_file(fixture) == fixture_pin, "portable rewrite never modifies source");
        }
        check(sha256_file(good) == pin, "reader never modifies source archive");
        // A large pinned input is hashed once while its identity holds: the first use shows its
        // bytes, a repeated use is silent.
        {
            const auto large = directory / "large.bin";
            std::ofstream(large).close();
            std::filesystem::resize_file(large, 256ULL << 20);
            const auto expected = sha256_file(large);
            std::ostringstream console;
            auto *prior = std::cerr.rdbuf(console.rdbuf());
            progress::console(cli::Language::english, false);
            const auto first = pinned_file_sha256(large);
            const auto shown = console.str();
            const auto second = pinned_file_sha256(large);
            const auto repeated = console.str().size() - shown.size();
            std::cerr.rdbuf(prior);
            check(first == expected && second == expected &&
                          shown.find("Checking large.bin (256 MiB)\n") != std::string::npos,
                  "a large pinned input shows the bytes of its hash");
            bool stable = true;
            try {
                cache_file_identity(large);
            } catch (const std::exception &) {
                stable = false;
            }

            check(!stable || repeated == 0, "an unchanged large input is hashed again");
            std::filesystem::remove(large);
        }
        std::cout << "pinned ZIP inventory and binary read guards passed\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';

        return 1;
    }
}
