#include "original_navi_mac.hpp"
#include "error.hpp"
#include "process.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
namespace voyemu {
namespace {
constexpr std::string_view profile_id = "free-11-chinese-20260731-7.3.1";
constexpr std::string_view input_pin =
        "34309b705a40fbc6a52a96cfc6713c90a683223e0652b936b7227584546169d8";
constexpr std::string_view cert_pin =
        "b2e962ed163aed0831d6d08ee41ca9406ae970032aa4df8a4c3b41d222b201d3";
constexpr std::string_view output_pin =
        "c5c361ff9623c3c3798f60210684ae10b5eec48bed7e9e43a3b275f2b36db392";

std::string hex(std::string_view bytes) {
    constexpr char table[] = "0123456789abcdef";
    std::string out;

    out.reserve(bytes.size() * 2);

    for (unsigned char b : bytes) {
        out += table[b >> 4];
        out += table[b & 15];
    }

    return out;
}
} // namespace

bool original_navi_mac_collision(std::string_view xml, std::string_view certificate) {
    std::string lower(xml);

    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return lower.find("com.baidu.naviauto") != std::string::npos ||
           lower.find(hex(certificate)) != std::string::npos;
}

std::string original_navi_mac_insert(std::string_view xml, std::string_view certificate) {
    if (original_navi_mac_collision(xml, certificate)) {
        throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_MAC", "signer or package collision");
    }

    constexpr std::string_view close = "</policy>";
    const auto at = xml.find(close);

    if (at == xml.npos || xml.find(close, at + close.size()) != xml.npos) {
        throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_MAC", "exact policy terminator required");
    }

    const auto insertion =
            "<signer signature=\"" + hex(certificate) +
            "\"><package name=\"com.baidu.naviauto\"><seinfo value=\"platform\"/></package></signer>";

    return std::string(xml.substr(0, at)) + insertion + std::string(xml.substr(at));
}

std::string apply_original_navi_mac(std::string_view profile, unsigned api, std::string_view xml,
                                    std::string_view certificate) {
    if (profile != profile_id || api != 30 || sha256_bytes(std::string(xml)) != input_pin ||
        sha256_bytes(std::string(certificate)) != cert_pin) {
        throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_MAC",
                          "exact profile, XML and OEM certificate required");
    }

    const auto output = original_navi_mac_insert(xml, certificate);

    if (sha256_bytes(output) != output_pin) {
        throw VoyemuError("QINGGAN11_ORIGINAL_NAVI_MAC", "output pin mismatch");
    }

    return output;
}
} // namespace voyemu
