#include "capture_png.hpp"
#include "model.hpp"
#include <algorithm>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>

#include <objidl.h>
namespace voyemu::output {
namespace {
template <class T> struct Com {
    Com() = default;
    Com(const Com &) = delete;
    Com &operator=(const Com &) = delete;
    T *value = nullptr;

    ~Com() {
        if (value) {
            value->Release();
        }
    }

    T **out() {
        return &value;
    }

    T *operator->() {
        return value;
    }
};

void checked(HRESULT value) {
    if (FAILED(value)) {
        throw VoyemuError("TEST_PNG",
                          "WIC failure " + std::to_string(static_cast<std::uint32_t>(value)));
    }
}

struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Apartment() {
        if (FAILED(status) && status != RPC_E_CHANGED_MODE) {
            checked(status);
        }
    }

    ~Apartment() {
        if (SUCCEEDED(status)) {
            CoUninitialize();
        }
    }
};
} // namespace

std::vector<std::uint8_t> png_frame(std::span<const std::uint8_t> pixels, unsigned width,
                                    unsigned height, bool bgra) {
    if (!width || !height || width > 3840 || height > 2160 ||
        pixels.size() != std::size_t(width) * height * 4) {
        throw VoyemuError("TEST_FRAME", "complete bounded pixels required");
    }

    Apartment apartment;
    Com<IWICImagingFactory> factory;
    checked(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_IWICImagingFactory, reinterpret_cast<void **>(factory.out())));
    Com<IStream> stream;
    checked(CreateStreamOnHGlobal(nullptr, TRUE, stream.out()));
    Com<IWICBitmapEncoder> encoder;
    checked(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.out()));
    checked(encoder->Initialize(stream.value, WICBitmapEncoderNoCache));
    Com<IWICBitmapFrameEncode> frame;
    Com<IPropertyBag2> properties;
    checked(encoder->CreateNewFrame(frame.out(), properties.out()));
    checked(frame->Initialize(properties.value));
    checked(frame->SetSize(width, height));
    auto format = GUID_WICPixelFormat32bppBGRA;
    checked(frame->SetPixelFormat(&format));

    if (!IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        throw VoyemuError("TEST_PNG", "exact32-bit PNG format required");
    }

    Com<IWICBitmap> bitmap;
    checked(factory->CreateBitmapFromMemory(
            width, height, bgra ? GUID_WICPixelFormat32bppBGR : GUID_WICPixelFormat32bppRGBA,
            width * 4, UINT(pixels.size()), const_cast<BYTE *>(pixels.data()), bitmap.out()));
    Com<IWICFormatConverter> converter;
    checked(factory->CreateFormatConverter(converter.out()));
    checked(converter->Initialize(bitmap.value, format, WICBitmapDitherTypeNone, nullptr, 0,
                                  WICBitmapPaletteTypeCustom));
    checked(frame->WriteSource(converter.value, nullptr));
    checked(frame->Commit());
    checked(encoder->Commit());
    STATSTG info{};
    checked(stream->Stat(&info, STATFLAG_NONAME));

    if (!info.cbSize.QuadPart || info.cbSize.QuadPart > 40ULL * 1024 * 1024) {
        throw VoyemuError("TEST_PNG", "bounded encoded frame required");
    }

    LARGE_INTEGER zero{};
    checked(stream->Seek(zero, STREAM_SEEK_SET, nullptr));
    std::vector<std::uint8_t> result(std::size_t(info.cbSize.QuadPart));
    ULONG copied = 0;
    checked(stream->Read(result.data(), ULONG(result.size()), &copied));

    if (copied != result.size()) {
        throw VoyemuError("TEST_PNG", "incomplete encoded frame");
    }

    return result;
}

void write_artifact(const std::filesystem::path &path, std::span<const std::uint8_t> bytes) {
    if (!path.is_absolute() || bytes.empty() || bytes.size() > 40 * 1024 * 1024) {
        throw VoyemuError("TEST_ARTIFACT", "bounded absolute artifact required");
    }

    ensure_private_directory(path.parent_path());
    auto file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        throw VoyemuError("TEST_ARTIFACT_EXISTS", "artifact must be new");
    }

    bool complete = true;
    std::size_t at = 0;

    while (at < bytes.size()) {
        const DWORD amount = DWORD(std::min<std::size_t>(bytes.size() - at, 1024 * 1024));
        DWORD written = 0;

        if (!WriteFile(file, bytes.data() + at, amount, &written, nullptr) || written != amount) {
            complete = false;
            break;
        }

        at += written;
    }

    if (!FlushFileBuffers(file)) {
        complete = false;
    }

    CloseHandle(file);

    if (!complete) {
        DeleteFileW(path.c_str());
        throw VoyemuError("TEST_ARTIFACT_IO", "artifact write failed");
    }
}
} // namespace voyemu::output
