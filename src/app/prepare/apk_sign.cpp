#include "apk_sign.hpp"
#include "apk_alignment.hpp"
#include "apk_crypto.hpp"
#include "apk_digest.hpp"
#include "model.hpp"
#include "os/fs.hpp"
#include "process.hpp"
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <limits>
#include <stdexcept>
namespace voyemu {
namespace {
[[noreturn]] void invalid() {
    throw VoyemuError("APK_SIGN", "invalid input, output or publication");
}

std::string le(std::uint64_t value, unsigned count) {
    std::string bytes;

    for (unsigned i = 0; i < count; ++i) {
        bytes += char(value >> (8 * i));
    }

    return bytes;
}

std::uint64_t number(std::string_view bytes, std::size_t at, unsigned count) {
    if (at > bytes.size() || count > bytes.size() - at) {
        invalid();
    }

    std::uint64_t n = 0;

    for (unsigned i = 0; i < count; ++i) {
        n |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }

    return n;
}

std::string lp(const std::string &value) {
    if (value.size() > (1 << 20)) {
        invalid();
    }

    return le(value.size(), 4) + value;
}
} // namespace

std::string sign_pinned_apk_v2(const std::filesystem::path &source, const std::string &source_pin,
                               std::string_view pkcs8, const std::string &key_pin,
                               std::string_view certificate, const std::string &cert_pin,
                               const std::filesystem::path &destination, std::uint64_t reserve) {
    if (!destination.is_absolute() || destination.filename().empty() ||
        destination.filename() == "." || destination.filename() == ".." ||
        std::filesystem::exists(destination) || std::filesystem::is_symlink(destination)) {
        invalid();
    }

    const auto content_hex = unsigned_apk_v2_digest(source, source_pin);
    std::string content;

    for (std::size_t i = 0; i < content_hex.size(); i += 2) {
        content += char(std::stoul(content_hex.substr(i, 2), nullptr, 16));
    }

    const auto public_key = apk_rsa_public_key_info(certificate, cert_pin);
    const auto algorithm = le(0x0103, 4);
    const auto signed_data = lp(lp(algorithm + lp(content))) + lp(lp(std::string(certificate))) +
                             lp("");
    const auto signature = sign_apk_rsa_sha256(pkcs8, key_pin, certificate, cert_pin, signed_data);
    const auto signer = lp(signed_data) + lp(lp(algorithm + lp(signature))) + lp(public_key);
    const auto value = lp(lp(signer)), pair = le(4 + value.size(), 8) + le(0x7109871a, 4) + value;
    const auto block = le(pair.size() + 24, 8) + pair + le(pair.size() + 24, 8) +
                       "APK Sig Block 42";
    const auto size = std::filesystem::file_size(source);
    auto parent = destination.parent_path();
    auto existing = parent;

    while (!std::filesystem::exists(existing) && existing != existing.parent_path()) {
        existing = existing.parent_path();
    }

    const auto peak = size + block.size();

    if (reserve > std::numeric_limits<std::uint64_t>::max() - peak ||
        std::filesystem::space(existing).available < reserve + peak) {
        throw VoyemuError("APK_SIGN_SPACE", "reserve plus planned output required");
    }

    std::ifstream input(source, std::ios::binary);
    const auto tail_size = static_cast<std::size_t>(std::min<std::uint64_t>(size, 65557));
    std::string tail(tail_size, '\0');
    input.seekg(size - tail_size);
    input.read(tail.data(), tail.size());

    if (!input) {
        invalid();
    }

    std::size_t end = tail.size() - 22;

    while (true) {
        if (number(tail, end, 4) == 0x06054b50 &&
            number(tail, end + 20, 2) == tail.size() - end - 22) {
            break;
        }

        if (!end) {
            invalid();
        }

        --end;
    }

    const auto central = number(tail, end + 16, 4);
    const auto eocd = size - tail_size + end;

    if (central > eocd || central + number(tail, end + 12, 4) != eocd ||
        central + block.size() > 0xffffffff) {
        invalid();
    }

    std::string end_record = tail.substr(end);
    end_record.replace(16, 4, le(central + block.size(), 4));
    ensure_private_directory(parent);
    const auto pending = parent /
                         ("." + destination.filename().string() + ".pending-" + random_token());
    struct Cleanup {
        std::filesystem::path pending;
        bool owned = false;

        ~Cleanup() {
            if (owned) {
                std::error_code e;
                std::filesystem::remove(pending, e);
            }
        }
    } cleanup;
    cleanup.pending = pending;
    os::ExclusiveFile file(pending);

    if (!file.open()) {
        invalid();
    }

    cleanup.owned = true;
    auto write = [&](std::string_view bytes) {
        if (!file.write(bytes.data(), bytes.size())) {
            invalid();
        }
    };
    auto copy = [&](std::uint64_t offset, std::uint64_t length) {
        input.clear();
        input.seekg(offset);
        std::string buffer(1 << 20, '\0');

        while (length) {
            const auto count =
                    static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), length));
            input.read(buffer.data(), count);

            if (!input) {
                invalid();
            }

            write(std::string_view(buffer.data(), count));
            length -= count;
        }
    };
    copy(0, central);
    write(block);
    copy(central, eocd - central);
    write(end_record);

    if (!file.sync() || !file.close()) {
        invalid();
    }

    if (sha256_file(source) != source_pin) {
        invalid();
    }

    const auto digest = sha256_file(pending);
    verify_pinned_apk_alignment(pending, digest);

    if (!os::rename_exclusive(pending, destination)) {
        invalid();
    }

    cleanup.owned = false;

    return digest;
}
} // namespace voyemu
