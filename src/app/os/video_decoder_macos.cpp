#include "error.hpp"
#include "video_decoder.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>
#include <exception>
#include <stdexcept>

namespace voyemu::os {
namespace {
[[noreturn]] void fail(const std::string &reason) {
    throw VoyemuError("STARTUP_VIDEO", "VideoToolbox " + reason);
}

void checked(OSStatus status, const char *what) {
    if (status != noErr) {
        fail(std::string(what) + " failed with " + std::to_string(int(status)));
    }
}

// A Core Foundation object released with its owner.
template <class T> struct Owned {
    Owned() = default;

    explicit Owned(T object) : value(object) {}

    Owned(const Owned &) = delete;
    Owned &operator=(const Owned &) = delete;

    ~Owned() {
        if (value) {
            CFRelease(value);
        }
    }

    T value{};
};
// Pictures leave the session in decoding order, which is their presentation order because the
// startup video reader rejects composition offsets.
class VideoToolboxDecoder final : public VideoDecoder {
public:
    explicit VideoToolboxDecoder(const VideoStream &stream) {
        std::vector<const std::uint8_t *> sets;
        std::vector<std::size_t> sizes;

        for (const auto &set : stream.parameter_sets) {
            sets.push_back(reinterpret_cast<const std::uint8_t *>(set.data()));
            sizes.push_back(set.size());
        }

        checked(CMVideoFormatDescriptionCreateFromH264ParameterSets(
                        kCFAllocatorDefault, sets.size(), sets.data(), sizes.data(), 4,
                        &format_.value),
                "H.264 parameter sets");
        // NV12 in the range of the stream, so that the session does not convert it.
        const auto pixel = SInt32(stream.full_range
                                          ? kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                                          : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
        const Owned<CFNumberRef> number(
                CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &pixel));
        const void *keys[] = {kCVPixelBufferPixelFormatTypeKey};
        const void *values[] = {number.value};
        const Owned<CFDictionaryRef> attributes(
                number.value ? CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1,
                                                  &kCFTypeDictionaryKeyCallBacks,
                                                  &kCFTypeDictionaryValueCallBacks)
                             : nullptr);

        if (!attributes.value) {
            fail("cannot describe the output");
        }

        const VTDecompressionOutputCallbackRecord callback{&VideoToolboxDecoder::output, this};
        checked(VTDecompressionSessionCreate(kCFAllocatorDefault, format_.value, nullptr,
                                             attributes.value, &callback, &session_.value),
                "H.264 decoder");
    }

    ~VideoToolboxDecoder() override {
        if (session_.value) {
            VTDecompressionSessionInvalidate(session_.value);
        }
    }

    VideoToolboxDecoder(const VideoToolboxDecoder &) = delete;
    VideoToolboxDecoder &operator=(const VideoToolboxDecoder &) = delete;

    // One sample of NAL units with 4-byte lengths, as the format description declares.
    void decode(const std::vector<std::string_view> &units, const VideoPictureSink &sink) override {
        std::string bytes;

        for (const auto unit : units) {
            for (unsigned shift = 24;; shift -= 8) {
                bytes += char(unit.size() >> shift);

                if (shift == 0) {
                    break;
                }
            }

            bytes += unit;
        }

        Owned<CMBlockBufferRef> block;
        checked(CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, nullptr, bytes.size(),
                                                   kCFAllocatorDefault, nullptr, 0, bytes.size(),
                                                   kCMBlockBufferAssureMemoryNowFlag, &block.value),
                "access unit buffer");
        checked(CMBlockBufferReplaceDataBytes(bytes.data(), block.value, 0, bytes.size()),
                "access unit buffer");
        Owned<CMSampleBufferRef> sample;
        const std::size_t size = bytes.size();
        checked(CMSampleBufferCreateReady(kCFAllocatorDefault, block.value, format_.value, 1, 0,
                                          nullptr, 1, &size, &sample.value),
                "access unit sample");
        run(sink, [&] {
            VTDecodeInfoFlags flags = 0;

            return VTDecompressionSessionDecodeFrame(session_.value, sample.value, 0, nullptr,
                                                     &flags);
        });
    }

    void flush(const VideoPictureSink &sink) override {
        run(sink, [&] {
            return VTDecompressionSessionFinishDelayedFrames(session_.value);
        });
    }

private:
    // Runs one session call, waits for its pictures and reports the first failure of either.
    template <class Call> void run(const VideoPictureSink &sink, Call call) {
        sink_ = &sink;
        failure_ = noErr;
        error_ = nullptr;
        OSStatus status = call();

        if (status == noErr) {
            status = VTDecompressionSessionWaitForAsynchronousFrames(session_.value);
        }

        sink_ = nullptr;

        if (error_) {
            std::rethrow_exception(error_);
        }

        checked(status, "H.264 access unit");
        checked(failure_, "H.264 picture");
    }

    static void output(void *decoder, void *, OSStatus status, VTDecodeInfoFlags flags,
                       CVImageBufferRef image, CMTime, CMTime) {
        auto &self = *static_cast<VideoToolboxDecoder *>(decoder);

        if (self.error_ || self.failure_ != noErr) {
            return;
        }

        if (status != noErr || !image || (flags & kVTDecodeInfo_FrameDropped) != 0) {
            self.failure_ = status != noErr ? status : OSStatus(kVTVideoDecoderBadDataErr);

            return;
        }

        if (CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
            self.failure_ = OSStatus(kVTVideoDecoderMalfunctionErr);

            return;
        }

        try {
            if (!CVPixelBufferIsPlanar(image) || CVPixelBufferGetPlaneCount(image) != 2) {
                fail("decodes into another picture format");
            }

            const auto *luma =
                    static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(image, 0));
            const auto *chroma =
                    static_cast<const std::uint8_t *>(CVPixelBufferGetBaseAddressOfPlane(image, 1));
            (*self.sink_)({unsigned(CVPixelBufferGetWidth(image)),
                           unsigned(CVPixelBufferGetHeight(image)), luma, chroma, chroma + 1,
                           CVPixelBufferGetBytesPerRowOfPlane(image, 0),
                           CVPixelBufferGetBytesPerRowOfPlane(image, 1), 2});
        } catch (...) {
            self.error_ = std::current_exception();
        }

        CVPixelBufferUnlockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
    }

    Owned<CMFormatDescriptionRef> format_;
    Owned<VTDecompressionSessionRef> session_;
    const VideoPictureSink *sink_{};
    OSStatus failure_ = noErr;
    std::exception_ptr error_;
};
} // namespace

std::unique_ptr<VideoDecoder> host_video_decoder(const VideoStream &stream) {
    return std::make_unique<VideoToolboxDecoder>(stream);
}
} // namespace voyemu::os
