#include "frame_transport.hpp"
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace voyemu::frames;
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

template <class T> void put(std::vector<std::byte> &bytes, std::size_t at, T value) {
    std::memcpy(bytes.data() + at, &value, sizeof(value));
}

std::vector<std::byte> bmp(unsigned bits, bool top_down) {
    auto row = std::size_t(width) * (bits / 8);
    std::vector<std::byte> b(54 + row * height);

    b[0] = std::byte{'B'};
    b[1] = std::byte{'M'};
    put(b, 10, std::uint32_t(54));
    put(b, 14, std::uint32_t(40));
    put(b, 18, std::int32_t(width));
    put(b, 22, top_down ? -std::int32_t(height) : std::int32_t(height));
    put(b, 26, std::uint16_t(1));
    put(b, 28, std::uint16_t(bits));

    for (std::size_t y = 0; y < height; y++) {
        for (std::size_t x = 0; x < width; x++) {
            auto at = 54 + (top_down ? y : height - 1 - y) * row + x * (bits / 8);
            b[at] = std::byte(y % 251);
            b[at + 1] = std::byte(x % 251);
            b[at + 2] = std::byte{53};

            if (bits == 32) {
                b[at + 3] = std::byte{0};
            }
        }
    }

    return b;
}
} // namespace

int main() try {
    auto a = decode_screen_bmp(bmp(24, false));
    auto b = decode_screen_bmp(bmp(32, true));

    assert(a == b && a.size() == pixel_bytes && a[3] == std::byte{255} &&
           a[stride] == std::byte{1});
    auto malformed = bmp(24, false);
    malformed[0] = std::byte{0};
    rejects([&] {
        decode_screen_bmp(malformed);
    });
    malformed = bmp(24, false);
    put(malformed, 18, std::int32_t(960));
    rejects([&] {
        decode_screen_bmp(malformed);
    });
    malformed = bmp(32, true);
    malformed.pop_back();
    rejects([&] {
        decode_screen_bmp(malformed);
    });
    malformed = bmp(32, true);
    put(malformed, 30, std::uint32_t(3));
    rejects([&] {
        decode_screen_bmp(malformed);
    });
    auto path = std::filesystem::temp_directory_path() /
                ("voyemu-frame-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                 ".bgra");
    struct Cleanup {
        std::filesystem::path p;

        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove(p, ec);
        }
    } cleanup{path};
    // Sessions archive a retired backing by rename; Windows refuses to delete a mapped file.
    const auto archived = std::filesystem::path(path.string() + ".archived");
    Cleanup archived_cleanup{archived};
    Reader missing(path);
    assert(!missing.read_latest());
    {
        Writer writer(path);
        Reader reader(path);
        assert(!reader.read_latest());
        writer.publish(a, 1, 100);
        auto first = reader.read_latest();
        assert(first && first->sequence == 1 && first->timestamp_ns == 100 && first->bgra == a);
        assert(!reader.read_latest(1));
        rejects([&] {
            Writer duplicate(path);
        });
        rejects([&] {
            writer.publish(a, 1, 200);
        });
        rejects([&] {
            writer.publish(std::span(a).first(7), 2, 200);
        });
        writer.publish(b, 2, 200);
        writer.publish(a, 3, 300);
        writer.publish(b, 4, 400);
        auto latest = reader.read_latest(1);
        assert(latest && latest->sequence == 4 && latest->bgra == b);
        Frame reusable;
        assert(reader.read_latest(reusable, 3) && reusable.sequence == 4 &&
               reusable.timestamp_ns == 400 && reusable.bgra == b);
        const auto first_allocation = reusable.bgra.data();
        assert(reader.read_latest(reusable, 0));
        const auto second_allocation = reusable.bgra.data();
        assert(first_allocation != second_allocation);

        for (std::uint64_t sequence = 5; sequence < 15; ++sequence) {
            auto &pixels = sequence % 2 ? a : b;
            writer.publish(pixels, sequence, sequence * 100);
            assert(reader.read_latest(reusable, sequence - 1) && reusable.sequence == sequence &&
                   reusable.timestamp_ns == sequence * 100 && reusable.bgra == pixels);
            assert(reusable.bgra.data() == first_allocation ||
                   reusable.bgra.data() == second_allocation);
        }

        assert(!reader.read_latest(reusable, 14) && reusable.sequence == 14 && reusable.bgra == b);
        // Inject an in-progress slot marker: no incomplete pixels may be returned.
        const std::uint64_t odd_stamp = 29, even_stamp = 28;
        const auto slot_offset = 128 + (14 % slots) * (64 + pixel_bytes);
        std::fstream backing(path, std::ios::binary | std::ios::in | std::ios::out);
        backing.seekp(slot_offset);
        backing.write(reinterpret_cast<const char *>(&odd_stamp), 8);
        backing.flush();
        assert(!reader.read_latest(13));
        assert(!reader.read_latest(reusable, 13) && reusable.sequence == 14 &&
               reusable.timestamp_ns == 1400 && reusable.bgra == b);
        backing.seekp(slot_offset);
        backing.write(reinterpret_cast<const char *>(&even_stamp), 8);
        backing.flush();
        assert(reader.read_latest(13));
        assert(reader.read_latest(reusable, 13) && reusable.bgra == b);
    }
    // A cached mapping must stop yielding the retired producer's last frame.
    Reader cached(path);
    assert(cached.invalidated());
    assert(cached.read_latest());
    assert(!cached.invalidated());
    assert(!cached.read_latest(14) && !cached.invalidated());
    const auto original_revision = cached.revision();
    Frame retained;
    assert(cached.read_latest(retained) && retained.sequence == 14);
    invalidate(path);
    assert(!cached.read_latest() && cached.invalidated() && cached.revision() > original_revision);
    assert(!cached.read_latest(retained) && retained.sequence == 14 &&
           retained.timestamp_ns == 1400);
    std::filesystem::rename(path, archived);
    assert(!cached.read_latest());
    invalidate(path);
    {
        Writer successor(path);
        assert(!cached.read_latest(14) && cached.invalidated());
        successor.publish(a, 1, 1500);
        // A successor starts a new sequence domain even when the caller retains its old cursor.
        auto restarted = cached.read_latest(14);
        assert(restarted && restarted->sequence == 1 && restarted->timestamp_ns == 1500 &&
               restarted->bgra == a && !cached.invalidated());
        assert(!cached.read_latest(1) && !cached.invalidated());
    }
    // Replacement also reopens when a reader did not observe the missing-file gap.
    // Rename permits replacement while Windows readers still hold the old file open.
    const auto retired = std::filesystem::path(path.string() + ".retired");
    Cleanup retired_cleanup{retired};
    const auto previous_revision = cached.revision();
    std::filesystem::rename(path, retired);
    {
        Writer successor(path);
        successor.publish(b, 1, 1600);
        auto replaced = cached.read_latest(1);
        assert(replaced && replaced->timestamp_ns == 1600 && replaced->bgra == b &&
               cached.revision() > previous_revision && !cached.invalidated());
        assert(!cached.read_latest(1));
    }
    std::cout << "frame transport guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
