#include "error.hpp"
#include "library.hpp"
#include "video_decoder.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <objbase.h>
// The Media Foundation GUIDs are defined here rather than linked from mfuuid.
#include <initguid.h>

#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>

namespace voyemu::os {
namespace {
// CLSID_CMSH264DecoderMFT, the H.264 decoder of Windows; Windows N without the Media Feature Pack
// has neither it nor mfplat.dll.
constexpr CLSID h264_decoder_class = {
        0x62ce7e72, 0x4c71, 0x4d20, {0xb1, 0x5d, 0x45, 0x28, 0x31, 0xa8, 0x7d, 0x9d}};
[[noreturn]] void fail(const std::string &reason) {
    throw VoyemuError("STARTUP_VIDEO", "Media Foundation " + reason);
}

void checked(HRESULT status, const char *what) {
    if (FAILED(status)) {
        char code[16];
        std::snprintf(code, sizeof code, "0x%08lx", static_cast<unsigned long>(status));
        fail(std::string(what) + " failed with " + code);
    }
}

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

    T *operator->() const {
        return value;
    }
};
struct Apartment {
    HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    Apartment() {
        if (FAILED(status) && status != RPC_E_CHANGED_MODE) {
            checked(status, "COM initialization");
        }
    }

    Apartment(const Apartment &) = delete;
    Apartment &operator=(const Apartment &) = delete;

    ~Apartment() {
        if (SUCCEEDED(status)) {
            CoUninitialize();
        }
    }
};
// mfplat.dll is loaded at run time, so that Voyemu also starts where it is absent.
class MediaFoundationDecoder final : public VideoDecoder {
public:
    explicit MediaFoundationDecoder(const VideoStream &stream)
        : parameter_sets_(stream.parameter_sets) {
        if (!library_.loaded()) {
            fail("is absent: " + library_.failure());
        }

        bind(startup_, "MFStartup");
        bind(shutdown_, "MFShutdown");
        bind(create_type_, "MFCreateMediaType");
        bind(create_sample_, "MFCreateSample");
        bind(create_buffer_, "MFCreateMemoryBuffer");
        checked(startup_(MF_VERSION, MFSTARTUP_LITE), "startup");
        started_.shutdown = shutdown_;
        checked(CoCreateInstance(h264_decoder_class, nullptr, CLSCTX_INPROC_SERVER,
                                 IID_IMFTransform, reinterpret_cast<void **>(decoder_.out())),
                "H.264 decoder");
        Com<IMFMediaType> input;
        checked(create_type_(input.out()), "media type");
        checked(input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video), "media type");
        checked(input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264), "media type");
        checked(MFSetAttributeSize(input.value, MF_MT_FRAME_SIZE, stream.width, stream.height),
                "media type");
        checked(decoder_->SetInputType(0, input.value, 0), "H.264 input");
        select_output();
        checked(decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0), "streaming");
        checked(decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0), "streaming");
    }

    // The first access unit carries the parameter sets in front of its own NAL units.
    void decode(const std::vector<std::string_view> &units, const VideoPictureSink &sink) override {
        std::string bytes;

        if (first_) {
            bytes = annex_b({parameter_sets_.begin(), parameter_sets_.end()});
            first_ = false;
        }

        bytes += annex_b(units);
        Com<IMFMediaBuffer> buffer;
        checked(create_buffer_(DWORD(bytes.size()), buffer.out()), "input buffer");
        BYTE *data = nullptr;
        checked(buffer->Lock(&data, nullptr, nullptr), "input buffer");
        std::copy(bytes.begin(), bytes.end(), data);
        buffer->Unlock();
        checked(buffer->SetCurrentLength(DWORD(bytes.size())), "input buffer");
        Com<IMFSample> sample;
        checked(create_sample_(sample.out()), "input sample");
        checked(sample->AddBuffer(buffer.value), "input sample");
        // Sample times only order the input; the frame rate comes from the MP4.
        checked(sample->SetSampleTime(LONGLONG(time_++)), "input sample");
        HRESULT status = decoder_->ProcessInput(0, sample.value, 0);

        if (status == MF_E_NOTACCEPTING) {
            receive(sink);
            status = decoder_->ProcessInput(0, sample.value, 0);
        }

        checked(status, "H.264 access unit");
        receive(sink);
    }

    void flush(const VideoPictureSink &sink) override {
        checked(decoder_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0), "drain");
        checked(decoder_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0), "drain");
        receive(sink);
    }

private:
    template <class Function> void bind(Function &function, const char *name) {
        function = reinterpret_cast<Function>(library_.symbol(name));

        if (!function) {
            fail(std::string("lacks ") + name);
        }
    }

    // Chooses NV12 output and learns its coded size, stride and sample size.
    void select_output() {
        for (DWORD index = 0;; ++index) {
            Com<IMFMediaType> type;
            const HRESULT status = decoder_->GetOutputAvailableType(0, index, type.out());

            if (status == MF_E_NO_MORE_TYPES) {
                fail("offers no NV12 output");
            }

            checked(status, "output type");
            GUID subtype{};

            if (FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
                !IsEqualGUID(subtype, MFVideoFormat_NV12)) {
                continue;
            }

            checked(decoder_->SetOutputType(0, type.value, 0), "NV12 output");
            UINT32 width = 0, height = 0, stride = 0;
            checked(MFGetAttributeSize(type.value, MF_MT_FRAME_SIZE, &width, &height),
                    "output size");
            width_ = width;
            height_ = height;
            stride_ = SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) &&
                                      INT32(stride) >= INT32(width)
                              ? stride
                              : width;
            MFT_OUTPUT_STREAM_INFO info{};
            checked(decoder_->GetOutputStreamInfo(0, &info), "output stream");
            output_bytes_ = info.cbSize;
            provides_ = (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;

            return;
        }
    }

    // Emits every ready picture until the decoder needs more input.
    void receive(const VideoPictureSink &sink) {
        for (;;) {
            Com<IMFSample> own, provided;
            Com<IMFMediaBuffer> buffer;
            MFT_OUTPUT_DATA_BUFFER output{};

            if (!provides_) {
                checked(create_buffer_(output_bytes_, buffer.out()), "output buffer");
                checked(create_sample_(own.out()), "output sample");
                checked(own->AddBuffer(buffer.value), "output sample");
                output.pSample = own.value;
            }

            DWORD flags = 0;
            const HRESULT status = decoder_->ProcessOutput(0, 1, &output, &flags);

            if (output.pEvents) {
                output.pEvents->Release();
            }

            if (provides_) {
                provided.value = output.pSample;
            }

            if (status == MF_E_TRANSFORM_NEED_MORE_INPUT) {
                return;
            }

            if (status == MF_E_TRANSFORM_STREAM_CHANGE) {
                select_output();
                continue;
            }

            checked(status, "H.264 picture");
            picture(output.pSample, sink);
        }
    }

    void picture(IMFSample *sample, const VideoPictureSink &sink) const {
        if (!sample) {
            fail("returned no picture");
        }

        Com<IMFMediaBuffer> buffer;
        checked(sample->ConvertToContiguousBuffer(buffer.out()), "picture buffer");
        Com<IMF2DBuffer> plane;
        BYTE *base = nullptr;
        LONG pitch = 0;
        DWORD length = 0;
        const bool two_dimensional =
                SUCCEEDED(buffer->QueryInterface(IID_IMF2DBuffer,
                                                 reinterpret_cast<void **>(plane.out()))) &&
                SUCCEEDED(plane->Lock2D(&base, &pitch));

        if (!two_dimensional) {
            checked(buffer->Lock(&base, nullptr, &length), "picture buffer");
            pitch = LONG(stride_);
        }

        const auto unlock = [&] {
            two_dimensional ? plane->Unlock2D() : buffer->Unlock();
        };

        try {
            if (pitch < LONG(width_) ||
                (!two_dimensional && length < std::size_t(pitch) * height_ / 2 * 3)) {
                fail("returned a truncated picture");
            }

            const auto *chroma = base + std::size_t(pitch) * height_;
            sink({width_, height_, base, chroma, chroma + 1, std::size_t(pitch), std::size_t(pitch),
                  2});
        } catch (...) {
            unlock();
            throw;
        }

        unlock();
    }

    struct Started {
        HRESULT(WINAPI *shutdown)() = nullptr;
        Started() = default;
        Started(const Started &) = delete;
        Started &operator=(const Started &) = delete;

        ~Started() {
            if (shutdown) {
                shutdown();
            }
        }
    };

    Library library_{"mfplat.dll"};
    Apartment apartment_;
    Started started_;
    Com<IMFTransform> decoder_;
    std::vector<std::string> parameter_sets_;
    bool first_ = true, provides_ = false;
    unsigned width_{}, height_{}, stride_{};
    DWORD output_bytes_{};
    std::uint64_t time_{};
    decltype(&::MFStartup) startup_{};
    decltype(&::MFShutdown) shutdown_{};
    decltype(&::MFCreateMediaType) create_type_{};
    decltype(&::MFCreateSample) create_sample_{};
    decltype(&::MFCreateMemoryBuffer) create_buffer_{};
};
} // namespace

std::unique_ptr<VideoDecoder> host_video_decoder(const VideoStream &stream) {
    return std::make_unique<MediaFoundationDecoder>(stream);
}
} // namespace voyemu::os
