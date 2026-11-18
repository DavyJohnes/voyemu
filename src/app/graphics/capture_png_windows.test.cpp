#include "capture_png.hpp"
#include "model.hpp"
#include "process.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#define NOMINMAX
#include <windows.h>
#include <exception>
#include <wincodec.h>
using namespace voyemu;

void decoded(std::span<const std::uint8_t> png, std::span<const std::uint8_t> expected,
             unsigned width, unsigned height) {
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    assert(SUCCEEDED(initialized));
    IWICImagingFactory *factory = nullptr;
    assert(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IWICImagingFactory,
                                      reinterpret_cast<void **>(&factory))));
    IWICStream *stream = nullptr;
    assert(SUCCEEDED(factory->CreateStream(&stream)));
    assert(SUCCEEDED(
            stream->InitializeFromMemory(const_cast<BYTE *>(png.data()), DWORD(png.size()))));
    IWICBitmapDecoder *decoder = nullptr;
    assert(SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad,
                                                      &decoder)));
    IWICBitmapFrameDecode *frame = nullptr;
    assert(SUCCEEDED(decoder->GetFrame(0, &frame)));
    UINT w = 0, h = 0;
    assert(SUCCEEDED(frame->GetSize(&w, &h)) && w == width && h == height);
    IWICFormatConverter *convert = nullptr;
    assert(SUCCEEDED(factory->CreateFormatConverter(&convert)));
    assert(SUCCEEDED(convert->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                         WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)));
    std::vector<std::uint8_t> actual(expected.size());
    assert(SUCCEEDED(convert->CopyPixels(nullptr, width * 4, DWORD(actual.size()), actual.data())));
    assert(std::equal(actual.begin(), actual.end(), expected.begin()));
    convert->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();
    CoUninitialize();
}

int main() try {
    const std::array<std::uint8_t, 16> rgba{255, 0,  0,   255, 1,   2,  3,   255,
                                            20,  80, 160, 17,  250, 60, 120, 93};
    auto png = output::png_frame(rgba, 2, 2);

    assert(png.size() > 32 && png[0] == 137 && png[1] == 'P');
    decoded(png, rgba, 2, 2);
    const std::array<std::uint8_t, 16> bgra{0,   0,  255, 0, 3,   2,  1,   9,
                                            160, 80, 20,  2, 120, 60, 250, 0},
            opaque{255, 0, 0, 255, 1, 2, 3, 255, 20, 80, 160, 255, 250, 60, 120, 255};
    decoded(output::png_frame(bgra, 2, 2, true), opaque, 2, 2);
    std::vector<std::uint8_t> panel(1920 * 720 * 4);

    for (unsigned y = 0; y < 720; ++y) {
        for (unsigned x = 0; x < 1920; ++x) {
            auto i = (y * 1920 + x) * 4;
            panel[i] = 18 + x / 128;
            panel[i + 1] = 20 + y / 96;
            panel[i + 2] = 32;
            panel[i + 3] = 255;
        }
    }

    auto large = output::png_frame(panel, 1920, 720);
    assert(large.size() < panel.size() / 6);
    decoded(large, panel, 1920, 720);
    bool refused = false;
    try {
        output::png_frame(rgba, 3, 2);
    } catch (...) {
        refused = true;
    }

    assert(refused);
    auto root = std::filesystem::canonical(std::filesystem::temp_directory_path()) /
                ("voyemu-wic-png-" + random_token());
    ensure_private_directory(root);
    output::write_artifact(root / "frame.png", png);
    auto pin = sha256_file(root / "frame.png");
    refused = false;
    try {
        output::write_artifact(root / "frame.png", large);
    } catch (...) {
        refused = true;
    }

    assert(refused && sha256_file(root / "frame.png") == pin);
    std::filesystem::create_hard_link(root / "frame.png", root / "linked.png");
    refused = false;
    try {
        output::write_artifact(root / "linked.png", large);
    } catch (...) {
        refused = true;
    }

    assert(refused && sha256_file(root / "frame.png") == pin);
    std::filesystem::remove_all(root);
    std::cout
            << "Windows WIC PNG: RGBA/alpha/BGRA opaque/full1920x720 identity and exclusive artifacts passed\n";
} catch (const std::exception &error) {
    std::cerr << error.what() << '\n';

    return 1;
}
