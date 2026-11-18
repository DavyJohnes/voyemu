#include "media_wire.hpp"
#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace voyemu::media;

template <class Function> void rejects(Function f) {
    bool rejected = false;

    try {
        f();
    } catch (const std::exception &) {
        rejected = true;
    }

    assert(rejected);
}

int main() try {
    const GuestDisplay zero{0, 1920, 720}, one{1, 1920, 720};
    const auto pixel_bytes = one.pixel_bytes();
    const auto body = image_request(one);

    assert(body == Bytes({8, 1, 24, 128, 15, 32, 208, 5, 40, 1}));
    auto empty = decode_image({}, zero);
    assert(!empty);
    const std::string handle = "file:///owned/frame.rgba";
    auto format = image_request(one, handle);
    assert(format.size() < 128);
    Bytes metadata{10, static_cast<std::uint8_t>(format.size())};
    metadata.insert(metadata.end(), format.begin(), format.end());
    metadata.insert(metadata.end(), {40, 9, 48, 11});
    Bytes mapped(pixel_bytes, 42);
    auto shared = decode_mapped_image(metadata, one, mapped, handle);
    assert(shared && shared->sequence == 9 && shared->timestamp_us == 11 &&
           shared->pixels().front() == 42);
    auto signal = decode_mapped_metadata(metadata, one, handle);
    assert(signal && signal->width == 1920 && signal->height == 720 && signal->sequence == 9 &&
           signal->timestamp_us == 11);
    rejects([&] {
        decode_mapped_metadata(metadata, one, "file:///wrong.rgba");
    });
    const auto thumbnail = frame_change_request(one, handle);
    Bytes change{10, static_cast<std::uint8_t>(thumbnail.size())};
    change.insert(change.end(), thumbnail.begin(), thumbnail.end());
    auto event = decode_frame_change(change, one, handle);
    assert(event && event->width == 8 && event->height == 3);
    rejects([&] {
        decode_frame_change(metadata, one, handle);
    });
    rejects([&] {
        decode_mapped_image(change, one, mapped, handle);
    });

    mapped[0] = 99;
    assert(shared->pixels().front() == 42); // The next SDK write cannot change a published Image.
    rejects([&] {
        decode_image(metadata, one);
    });
    rejects([&] {
        decode_mapped_image(metadata, zero, mapped, handle);
    });
    rejects([&] {
        decode_mapped_image(metadata, one, mapped, "file:///wrong/frame.rgba");
    });
    rejects([&] {
        decode_mapped_image(metadata, one, std::span(mapped).first(12), handle);
    });
    auto no_transport = image_request(one);
    Bytes wrong{10, static_cast<std::uint8_t>(no_transport.size())};
    wrong.insert(wrong.end(), no_transport.begin(), no_transport.end());
    rejects([&] {
        decode_mapped_image(wrong, one, mapped, handle);
    });

    for (const char *bad : {"relative", "http://host/frame", "file:///owned/\nframe"}) {
        rejects([&] {
            image_request(zero, bad);
        });
    }

    Bytes image{10, 10};
    image.insert(image.end(), body.begin(), body.end());
    image.insert(image.end(), {34, 128, 192, 209, 2});
    image.resize(image.size() + pixel_bytes, 77);
    image.insert(image.end(), {40, 123, 48, 128, 128, 128, 128, 128, 128, 1});
    auto parsed = decode_image(image, one);
    assert(parsed && parsed->sequence == 123 && parsed->timestamp_us == (std::uint64_t(1) << 42) &&
           parsed->pixels().front() == 77 && parsed->pixels().size() == pixel_bytes);
    rejects([&] {
        decode_image(image, zero);
    });
    auto truncated = image;
    truncated.pop_back();
    rejects([&] {
        decode_image(truncated, one);
    });
    auto duplicate = image;
    duplicate.insert(duplicate.end(), {40, 1});
    rejects([&] {
        decode_image(duplicate, one);
    });
    rejects([&] {
        decode_image(Bytes{0}, zero);
    });
    rejects([&] {
        decode_image(Bytes{34, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255}, zero);
    });
    auto frame = grpc_message(image);
    unsigned received = 0;
    MessageStream stream([&](Bytes value) {
        assert(value == image);
        ++received;
    });

    for (std::size_t offset = 0; offset < frame.size();) {
        const auto length = std::min<std::size_t>((offset % 1031) + 1, frame.size() - offset);
        stream.feed({frame.data() + offset, length});
        offset += length;
    }

    assert(received == 1 && !stream.incomplete());
    stream.feed(frame);
    assert(received == 2);
    MessageStream malicious([](Bytes) {});
    rejects([&] {
        malicious.feed(Bytes{1, 0, 0, 0, 0});
    });
    MessageStream oversized([](Bytes) {});
    rejects([&] {
        oversized.feed(Bytes{0, 255, 255, 255, 255});
    });
    assert(touch_request(zero, 0, 0, true) != touch_request(zero, 0, 0, false));
    // A rear screen of another size takes its own coordinates and frames.
    const GuestDisplay rear{2, 1902, 540};
    assert(touch_request(rear, 1901, 539, true).back() == 2);
    rejects([&] {
        touch_request(rear, 1902, 0, true);
    });
    rejects([&] {
        touch_request(rear, 0, 540, true);
    });
    rejects([] {
        touch_request({11, 1920, 720}, 0, 0, true);
    });
    rejects([] {
        touch_request({0, 3840, 2160}, 0, 0, true);
    });
    rejects([&] {
        touch_request(zero, 1920, 0, true);
    });
    rejects([&] {
        touch_request(zero, 0, -1, true);
    });
    const auto rear_body = image_request(rear);
    assert(rear_body == Bytes({8, 1, 24, 238, 14, 32, 156, 4, 40, 2}));
    Bytes rear_image{10, 10};
    rear_image.insert(rear_image.end(), rear_body.begin(), rear_body.end());
    rear_image.insert(rear_image.end(), {34, 160, 224, 250, 1});
    rear_image.resize(rear_image.size() + rear.pixel_bytes(), 5);
    const auto rear_frame = decode_image(rear_image, rear);
    assert(rear_frame && rear_frame->width == 1902 && rear_frame->height == 540 &&
           rear_frame->pixels().size() == rear.pixel_bytes());
    rejects([&] {
        decode_image(rear_image, {2, 1920, 720});
    });
    const auto rear_signal = frame_change_request(rear, handle);
    Bytes rear_change{10, static_cast<std::uint8_t>(rear_signal.size())};
    rear_change.insert(rear_change.end(), rear_signal.begin(), rear_signal.end());
    const auto rear_event = decode_frame_change(rear_change, rear, handle);
    assert(rear_event && rear_event->width == 317 && rear_event->height == 90);
    std::cout
            << "SDK frame dimensions, fragmented streams, bounded metadata and touch release guards passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
