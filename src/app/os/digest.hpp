#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace voyemu::os {
// SHA-256 block engines: portable on every CPU, SHA-NI on x86-64 CPUs that report it at run time
// and the ARMv8 SHA2 instructions where the target guarantees them (every arm64 Mac).
enum class Sha256Engine {
    portable,
    x86_sha,
    arm_sha2
};
// Engines this CPU runs, fastest last; Sha256() takes the last.
std::vector<Sha256Engine> sha256_engines();

// FIPS 180-4 SHA-256 and RFC 1321 MD5, identical on every host.
class Sha256 {
public:
    Sha256();
    explicit Sha256(Sha256Engine engine);
    void update(const void *data, std::size_t size);
    std::array<std::uint8_t, 32> finish();

private:
    using Blocks = void (*)(std::array<std::uint32_t, 8> &state, const std::uint8_t *data,
                            std::size_t count);
    Blocks blocks_;
    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> block_{};
    std::uint64_t length_ = 0;
};

class Md5 {
public:
    Md5();
    void update(const void *data, std::size_t size);
    std::array<std::uint8_t, 16> finish();

private:
    std::array<std::uint32_t, 4> state_;
    std::array<std::uint8_t, 64> block_{};
    std::uint64_t length_ = 0;
};

std::string hex_digest(const std::uint8_t *data, std::size_t size);

template <std::size_t N> std::string hex_digest(const std::array<std::uint8_t, N> &digest) {
    return hex_digest(digest.data(), N);
}
} // namespace voyemu::os
