#include "little_endian.hpp"
#include "check.hpp"
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
using namespace voyemu;

void check(bool value) {
    if (!value) {
        throw std::runtime_error("little-endian field contract differs");
    }
}

template <typename Work> void rejects(Work work, std::string_view code) {
    try {
        work();
    } catch (const VoyemuError &error) {
        check(error.code() == code);

        return;
    }

    throw std::runtime_error("field outside buffer accepted");
}
} // namespace

int main() {
    try {
        const std::string bytes("\x01\x02\x03\x04\x05\x06\x07\x08\xff", 9);
        constexpr Check need{"FIELD"};
        check(le_read(bytes, 0, 2, need, "extent") == 0x0201);
        check(le_read(bytes, 0, 4, need, "extent") == 0x04030201);
        check(le_read(bytes, 1, 8, need, "extent") == 0xff08070605040302ULL);
        check(le_read(bytes, 8, 1, need, "extent") == 0xff);
        check(le_read(bytes, 9, 0, need, "extent") == 0);
        const std::array<std::byte, 4> span{std::byte{0x78}, std::byte{0x56}, std::byte{0x34},
                                            std::byte{0x12}};
        check(le_read(std::span<const std::byte>(span), 0, 4, need, "extent") == 0x12345678);
        rejects(
                [&] {
                    le_read(bytes, 8, 2, need, "extent");
                },
                "FIELD");
        rejects(
                [&] {
                    le_read(bytes, 10, 0, need, "extent");
                },
                "FIELD");
        // An offset near the type limit must not wrap past the extent check.
        rejects(
                [&] {
                    le_read(bytes, ~std::uint64_t{0}, 2, need, "extent");
                },
                "FIELD");
        rejects(
                [&] {
                    le_read(std::string(16, '\0'), 0, 9, need, "extent");
                },
                "FIELD");
        // CodeCheck readers report their own code for an extent failure.
        rejects(
                [&] {
                    le_read(bytes, 6, 4, CodeCheck{"reader input"}, "READER_BOUNDS");
                },
                "READER_BOUNDS");
        std::string out(6, '\0');
        le_write(out, 1, 0x0a0b0c0d, 4, need, "extent");
        check(out == std::string("\x00\x0d\x0c\x0b\x0a\x00", 6));
        le_write(out, 4, 0x1234, 2, need, "extent");
        check(le_read(out, 4, 2, need, "extent") == 0x1234);
        rejects(
                [&] {
                    le_write(out, 5, 0, 2, need, "extent");
                },
                "FIELD");
        check(out == std::string("\x00\x0d\x0c\x0b\x34\x12", 6));
        std::cout << "little-endian read, write and extent contracts passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
