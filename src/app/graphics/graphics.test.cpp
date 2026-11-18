#include "graphics.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace voyemu::graphics;
namespace {
template <class F> void rejects(F f) {
    bool thrown = false;

    try {
        f();
    } catch (const std::exception &) {
        thrown = true;
    }

    assert(thrown);
}

template <class T> void add(std::vector<std::byte> &bytes, T value) {
    auto at = bytes.size();

    bytes.resize(at + sizeof(value));
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}

std::vector<std::byte> record(std::uint32_t opcode, std::initializer_list<std::uint64_t> args,
                              std::vector<std::byte> data = {}) {
    std::vector<std::byte> out;

    add(out, opcode);
    add(out, std::uint32_t(args.size()));
    add(out, std::uint32_t(data.size()));
    add(out, std::uint32_t(0));

    for (auto a : args) {
        add(out, a);
    }

    out.insert(out.end(), data.begin(), data.end());

    while (out.size() % 8) {
        out.push_back(std::byte{0});
    }

    return out;
}
} // namespace

int main() {
    auto mcd = [](std::initializer_list<std::uint32_t> values) {
        std::vector<std::byte> out;

        for (auto v : values) {
            add(out, v);
        }

        return out;
    };
    auto language = mcd({24, 7, 8, 8, 3, 1, 0, 42});

    validate_mcd_batch(1, 0, language);
    validate_mcd_batch(1, 3, mcd({3, 1, 6, 8, 4, 99, 0xfffffff9, 42}));

    for (auto invalid : {mcd({24, 7, 8, 8, 3, 2, 0, 42}), mcd({24, 7, 8, 8, 4, 1, 0, 42}),
                         mcd({24, 7, 6, 8, 3, 1, 0, 42}), mcd({24, 7, 8, 4, 3, 1, 0, 42}),
                         mcd({24, 999, 8, 8, 3, 1, 0, 42})}) {
        rejects([&] {
            validate_mcd_batch(1, 0, invalid);
        });
    }

    rejects([&] {
        validate_mcd_batch(0, 0, language);
    });
    rejects([&] {
        validate_mcd_batch(33, 0, language);
    });
    rejects([&] {
        validate_mcd_batch(1, 0x100000000ULL, language);
    });
    auto partial = language;
    partial.pop_back();
    rejects([&] {
        validate_mcd_batch(1, 0, partial);
    });
    RpcIdleWait zero;
    zero.observe(false, 0);
    zero.observe(false, 100000000);
    assert(zero.microseconds() == 500);
    RpcIdleWait idle;
    assert(idle.microseconds() == 100);
    idle.observe(false, 1000000000);
    idle.observe(false, 1099999999);
    assert(idle.microseconds() == 100);
    idle.observe(false, 1100000000);
    assert(idle.microseconds() == 500);
    idle.observe(true, 1100000001);
    assert(idle.microseconds() == 100);
    assert(idle.microseconds(true) == 1000);

    // A 55 Hz producer never enters long-idle backoff, including between frames.
    for (std::uint64_t frame = 0; frame < 100; ++frame) {
        const auto tick = 2000000000 + frame * 18181818;
        idle.observe(true, tick);
        idle.observe(false, tick + 100);
        idle.observe(false, tick + 18000000);
        assert(idle.microseconds() == 100);
    }

    validate_ram_extent(ram_base, 1);
    validate_ram_extent(ram_base + ram_bytes - 1, 1);
    rejects([] {
        validate_ram_extent(ram_base - 1, 1);
    });
    rejects([] {
        validate_ram_extent(ram_base + ram_bytes - 1, 2);
    });
    rejects([] {
        validate_ram_extent(std::numeric_limits<std::uint64_t>::max() - 1, 16);
    });
    rejects([] {
        validate_ram_extent(ram_base, 64 * 1024 * 1024 + 1);
    });
    rejects([] {
        validate_ram_extent(ram_base, 0);
    });
    constexpr auto passion_bytes = 11713ULL << 20;
    validate_ram_extent(ram_base + ram_bytes, 4096, passion_bytes);
    validate_ram_extent(ram_base + passion_bytes - 1, 1, passion_bytes);
    rejects([] {
        validate_ram_extent(ram_base + passion_bytes - 1, 2, passion_bytes);
    });
    rejects([] {
        validate_ram_extent(ram_base, 1, 0);
    });
    rejects([] {
        validate_ram_extent(ram_base, 1, 12ULL << 30);
    });
    Arguments a{};
    a[0] = 1;
    a[1] = 3;
    a[2] = 0x1406;
    a[6] = 2;
    std::vector<std::byte> vertices(24);
    validate_inline_client(a, vertices);
    a[4] = 16;
    vertices.resize(28);
    validate_inline_client(a, vertices);
    vertices.resize(24);
    rejects([&] {
        validate_inline_client(a, vertices);
    });
    a[5] = 1;
    rejects([&] {
        validate_inline_client(a, vertices);
    });
    a = {};
    a[1] = 2;
    std::vector<std::byte> matrix(128);
    validate_inline_uniform("glUniformMatrix4fv", a, matrix);
    matrix.pop_back();
    rejects([&] {
        validate_inline_uniform("glUniformMatrix4fv", a, matrix);
    });
    rejects([&] {
        validate_inline_uniform("glUniform2fv", a, matrix);
    });
    auto batch = record(23, {0x4000});
    auto parsed = validate_batch(1, batch);
    assert(parsed.size() == 1 && parsed[0].opcode == 23 && parsed[0].arguments[0] == 0x4000);
    auto second = record(33, {5});
    batch.insert(batch.end(), second.begin(), second.end());
    parsed = validate_batch(2, batch);
    assert(parsed[1].opcode == 33);
    // A later invalid record is rejected before the caller executes any earlier GL call.
    auto invalid = record(139, {1, 0});
    batch.insert(batch.end(), invalid.begin(), invalid.end());
    rejects([&] {
        validate_batch(3, batch);
    });
    rejects([&] {
        validate_batch(0, batch);
    });
    rejects([&] {
        validate_batch(65, batch);
    });
    auto trailing = record(23, {0x4000});
    trailing.push_back(std::byte{0});
    rejects([&] {
        validate_batch(1, trailing);
    });
    auto uniform = record(194, {7, 1, 0, 0}, std::vector<std::byte>(64));
    assert(validate_batch(1, uniform).size() == 1);
    uniform.pop_back();
    rejects([&] {
        validate_batch(1, uniform);
    });

    for (unsigned op : {113u, 114u, 115u}) {
        auto color_clear = record(op, {0x1800, 0, 0}, std::vector<std::byte>(16));
        assert(validate_batch(1, color_clear).front().payload.size() == 16);
        auto small_clear = record(op, {0x1802, 0, 0}, std::vector<std::byte>(4));
        assert(validate_batch(1, small_clear).front().payload.size() == 4);

        for (unsigned bytes : {0u, 4u, 15u, 17u}) {
            auto bad = record(op, {0x1800, 0, 0}, std::vector<std::byte>(bytes));
            rejects([&] {
                validate_batch(1, bad);
            });
        }

        auto bad = record(op, {0x1800, 0, 9}, std::vector<std::byte>(16));
        rejects([&] {
            validate_batch(1, bad);
        });
    }

    auto wrong_pointer = record(194, {7, 1, 0, 99}, std::vector<std::byte>(64));
    rejects([&] {
        validate_batch(1, wrong_pointer);
    });
    std::cout << "graphics guards passed\n";
}
