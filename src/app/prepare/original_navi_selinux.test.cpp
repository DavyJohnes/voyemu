#include "original_navi_selinux.hpp"
#include "file_patch.hpp"
#include "process.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace voyemu;

static void check(bool x) {
    if (!x) {
        throw std::runtime_error("original Navi SELinux test failed");
    }
}

static std::string read(const std::filesystem::path &p) {
    std::ifstream f(p, std::ios::binary);

    check(bool(f));

    return std::string(std::istreambuf_iterator<char>(f), {});
}

static std::string exact_mapping(std::string_view xml, std::string_view cert,
                                 std::string_view package) {
    std::string hex;
    constexpr char digits[] = "0123456789abcdef";

    for (unsigned char c : cert) {
        hex += digits[c >> 4];
        hex += digits[c & 15];
    }

    const auto marker = "<signer signature=\"" + hex + "\">";
    const auto begin = xml.find(marker);

    if (begin == xml.npos) {
        return {};
    }

    const auto end = xml.find("</signer>", begin);

    if (end == xml.npos) {
        throw std::runtime_error("bad signer");
    }

    const auto block = xml.substr(begin, end - begin);
    const auto target = "<package name=\"" + std::string(package) +
                        "\"><seinfo value=\"platform\"/></package>";

    return block.find(target) == block.npos ? std::string{} : "platform";
}

static std::string signer_hex(std::string_view cert) {
    std::string hex;
    constexpr char digits[] = "0123456789abcdef";

    for (unsigned char c : cert) {
        hex += digits[c >> 4];
        hex += digits[c & 15];
    }

    return hex;
}

static std::string patch_error(const PinnedFilePatch &patch) {
    try {
        prepare_file_patches(std::span<const PinnedFilePatch>(&patch, 1));
    } catch (const std::exception &error) {
        return error.what();
    }

    return {};
}

// Exact-XML insertion, pinned production rejection and recipe negatives on a synthetic certificate.
static void synthetic_mode(const std::filesystem::path &xml_path) {
    const auto xml = read(xml_path);
    std::string cert;

    for (int i = 0; i < 640; ++i) {
        cert += static_cast<char>((i * 37 + 11) & 255);
    }

    const auto result = original_navi_selinux_insert(xml, cert);
    const auto at = xml.find("</policy>");
    check(at != std::string::npos && result.starts_with(xml.substr(0, at)) &&
          result.ends_with(xml.substr(at)));
    const auto inserted =
            "<signer signature=\"" + signer_hex(cert) +
            "\"><package name=\"com.baidu.naviauto\"><seinfo value=\"platform\"/></package></signer>";
    check(result == xml.substr(0, at) + inserted + xml.substr(at));
    check(exact_mapping(result, cert, "com.baidu.naviauto") == "platform");
    check(exact_mapping(result, cert, "com.other.app").empty());
    check(exact_mapping(result, std::string(cert.size(), 'x'), "com.baidu.naviauto").empty());
    bool rejected = false;
    try {
        original_navi_selinux_insert(result, cert);
    } catch (...) {
        rejected = true;
    }

    check(rejected);
    rejected = false;
    try {
        original_navi_selinux_insert(xml + "</policy>", cert);
    } catch (...) {
        rejected = true;
    }

    check(rejected);
    rejected = false;
    try {
        apply_original_navi_selinux(30, xml, cert);
    } catch (...) {
        rejected = true;
    }

    check(rejected);
    // The donor stands for a non-OEM certificate file; only the OEM pin is accepted.
    PinnedFilePatch patch{
            "original-navi-selinux",
            30,
            std::filesystem::absolute(xml_path),
            "34309b705a40fbc6a52a96cfc6713c90a683223e0652b936b7227584546169d8",
            "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392",
            PinnedFileDependency{
                    std::filesystem::absolute(xml_path),
                    "b2e962ed163aed0831d6d08ee41ca9406ae970032aa4df8a4c3b41d222b201d3"},
            "",
            "",
            "api30-fixture"};
    patch.original_navi = true;
    check(patch_error(patch) == "FILE_PATCH_INPUT_SHA: pinned input mismatch");
    auto bad = patch;
    // The recipe follows the original_navi feature of the profile, not its id.
    bad.original_navi = false;
    check(patch_error(bad).starts_with("FILE_PATCH_PROFILE:"));
    bad = patch;
    bad.api = 28;
    check(patch_error(bad).starts_with("FILE_PATCH_PROFILE:"));
    bad = patch;
    bad.input_sha256 = std::string(64, '0');
    check(patch_error(bad) == "FILE_PATCH_INPUT_SHA: pinned input mismatch");
    bad = patch;
    bad.donor.value().sha256 = std::string(64, '0');
    check(patch_error(bad) == "QINGGAN11_ORIGINAL_NAVI_SELINUX: donor pin differs");
    bad = patch;
    bad.output_sha256 = std::string(64, '0');
    check(patch_error(bad) ==
          "QINGGAN11_ORIGINAL_NAVI_SELINUX: exact disabled recipe contract required");
    bad = patch;
    bad.certificate_apk = patch.donor;
    check(patch_error(bad) ==
          "QINGGAN11_ORIGINAL_NAVI_SELINUX: exact disabled recipe contract required");
    std::cout << "PASS synthetic certificate on exact XML: unchanged bytes, signer mapping, "
                 "collision, OEM pin and recipe negatives\n";
}

// Optional pinned original Navi APK: the derived OEM certificate reproduces the output pin.
static void original_mode(const std::filesystem::path &xml_path,
                          const std::filesystem::path &apk_path) {
    PinnedFilePatch patch{"original-navi-selinux",
                          30,
                          std::filesystem::absolute(xml_path),
                          "34309b705a40fbc6a52a96cfc6713c90a683223e0652b936b7227584546169d8",
                          "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392",
                          std::nullopt,
                          "",
                          "",
                          "api30-fixture"};

    patch.original_navi = true;
    patch.certificate_apk = PinnedFileDependency{
            std::filesystem::canonical(apk_path),
            "dceddff70db38bacb50ebffa46d36da76f70e5596c6184bca51c48c3ed562a05"};
    const auto result = prepare_file_patches(std::span<const PinnedFilePatch>(&patch, 1)).at(0);
    check(sha256_bytes(result) ==
          "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392");
    auto wrong = patch;
    wrong.certificate_apk.value().sha256 = std::string(64, '0');
    check(!patch_error(wrong).empty());
    std::cout << "PASS actual original APK-derived certificate reproduces the output pin\n";
}

int main(int argc, char **argv) try {
    const std::string cert = "abc";

    check(!original_navi_selinux_collision("<policy></policy>", cert));
    check(original_navi_selinux_collision("<policy><package name=\"com.baidu.naviauto\"/></policy>",
                                          cert));
    check(original_navi_selinux_collision("<policy><signer signature=\"616263\"/></policy>", cert));
    check(original_navi_selinux_collision("<policy><signer signature=\"616263\"/></policy>",
                                          "abc"));
    check(native_file_patch_steps("original-navi-selinux") ==
          std::vector<std::string>{"original-navi-selinux.json"});
    bool rejected = false;
    try {
        apply_original_navi_selinux(28, "<policy></policy>", cert);
    } catch (...) {
        rejected = true;
    }

    check(rejected);
    check(argc <= 3);

    if (argc >= 2) {
        synthetic_mode(argv[1]);
    }

    if (argc == 3) {
        original_mode(argv[1], argv[2]);
    }

    if (argc == 1) {
        std::cout << "PASS policy collision and API fail-closed unit\n";
    }

    return 0;
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
