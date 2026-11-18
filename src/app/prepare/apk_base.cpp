#include "apk_base.hpp"
#include "check.hpp"
#include "error.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include "quickboot.hpp"
#include <algorithm>
#include <fstream>
#include <string_view>
namespace voyemu {
namespace {
[[noreturn]] void bad(const char *reason) {
    throw VoyemuError("APK_BASE", reason);
}

std::uint64_t number(std::string_view bytes, std::size_t at, unsigned count) {
    if (at > bytes.size() || count > bytes.size() - at) {
        bad("integer outside container");
    }

    std::uint64_t result = 0;

    for (unsigned i = 0; i < count; ++i) {
        result |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    }

    return result;
}

std::string_view take(std::string_view &bytes) {
    const auto count = number(bytes, 0, 4);

    bytes.remove_prefix(4);

    if (count > bytes.size()) {
        bad("length outside container");
    }

    const auto result = bytes.substr(0, count);
    bytes.remove_prefix(count);

    return result;
}
} // namespace

std::string pinned_apk_v2_certificate(const std::filesystem::path &file, const std::string &pin) {
    if (!file.is_absolute() || file.lexically_normal() != file ||
        std::filesystem::canonical(file) != file || !sha256_text(pin)) {
        bad("canonical pinned APK required");
    }

    for (const auto &at : os::ancestors(file)) {
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(at))) {
            bad("linked input path");
        }
    }

    if (!std::filesystem::is_regular_file(file) || std::filesystem::hard_link_count(file) != 1) {
        bad("ordinary input required");
    }

    const auto epoch = cache_file_identity(file);
    const auto size = std::filesystem::file_size(file);

    if (size < 54 || size > (2ULL << 30) || sha256_file(file) != pin ||
        cache_file_identity(file) != epoch) {
        bad("APK pin or extent mismatch");
    }

    std::ifstream stream(file, std::ios::binary);
    auto read = [&](std::uint64_t at, std::size_t count) {
        if (at > size || count > size - at) {
            bad("read outside APK");
        }

        std::string result(count, '\0');
        stream.seekg(static_cast<std::streamoff>(at));
        stream.read(result.data(), static_cast<std::streamsize>(count));

        if (!stream) {
            bad("APK read failed");
        }

        return result;
    };
    const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, 65557));
    const auto tail = read(size - tail_size, tail_size);
    std::size_t end = tail.size() - 22;

    while (number(tail, end, 4) != 0x06054b50 ||
           number(tail, end + 20, 2) != tail.size() - end - 22) {
        if (!end) {
            bad("EOCD missing");
        }

        --end;
    }

    const auto central = number(tail, end + 16, 4);
    const auto eocd = size - tail_size + end;

    if (number(tail, end + 4, 2) || number(tail, end + 6, 2) ||
        number(tail, end + 8, 2) != number(tail, end + 10, 2) ||
        number(tail, end + 10, 2) == 65535 || central < 32 ||
        central + number(tail, end + 12, 4) != eocd) {
        bad("classic contiguous APK ZIP required");
    }

    const auto footer = read(central - 24, 24);

    if (std::string_view(footer).substr(8) != "APK Sig Block 42") {
        bad("v2 signing block missing");
    }

    const auto block_size = number(footer, 0, 8);

    if (block_size < 24 || block_size > (4ULL << 20) || block_size > central - 8) {
        bad("signing block bounds");
    }

    const auto block = read(central - block_size - 8, static_cast<std::size_t>(block_size + 8));

    if (number(block, 0, 8) != block_size) {
        bad("signing block size mismatch");
    }

    std::string_view value;
    unsigned found = 0;
    const auto pair_end = block.size() - 24;

    for (std::size_t at = 8; at < pair_end;) {
        if (pair_end - at < 8) {
            bad("truncated signing pair");
        }

        const auto length = number(block, at, 8);

        if (length < 4 || length > pair_end - at - 8) {
            bad("signing pair bounds");
        }

        if (number(block, at + 8, 4) == 0x7109871a) {
            if (++found != 1) {
                bad("ambiguous v2 block");
            }

            value = std::string_view(block).substr(at + 12, length - 4);
        }

        at += 8 + length;
    }

    if (found != 1) {
        bad("v2 signer missing");
    }

    auto signers = take(value);

    if (!value.empty()) {
        bad("v2 trailing bytes");
    }

    auto signer = take(signers);

    if (!signers.empty()) {
        bad("one base signer required");
    }

    auto signed_data = take(signer);
    const auto signatures = take(signer), public_key = take(signer);

    if (!signer.empty() || signatures.empty() || public_key.empty()) {
        bad("signer structure");
    }

    const auto digests = take(signed_data);
    auto certificates = take(signed_data);
    (void)take(signed_data);

    // AOSP V2SchemeSigner also emits a fourth empty length-prefixed element.
    // Accept only that exact terminal element; arbitrary trailing data stays invalid.
    if (signed_data == std::string_view("\0\0\0\0", 4)) {
        signed_data.remove_prefix(4);
    }

    if (!signed_data.empty() || digests.empty()) {
        bad("signed data structure");
    }

    const auto certificate = take(certificates);

    if (!certificates.empty() || certificate.size() < 4 || certificate.size() > 65536 ||
        static_cast<unsigned char>(certificate[0]) != 0x30) {
        bad("one bounded DER certificate required");
    }

    if (cache_file_identity(file) != epoch || sha256_file(file) != pin) {
        bad("APK changed during read");
    }

    return std::string(certificate);
}
} // namespace voyemu
