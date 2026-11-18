#include "error.hpp"
#include "library.hpp"
#include "video_decoder.hpp"
#include <cerrno>
#include <cstring>
#include <stdexcept>

namespace voyemu::os {
namespace {
// The system libavcodec of FFmpeg 9 down to FFmpeg 4, newest first; nothing is downloaded or
// shipped. Its C API is declared here: functions, enum values and the leading fields of AVFrame,
// which all these versions share.
constexpr const char *libavcodec_names[] = {"libavcodec.so.63", "libavcodec.so.62",
                                            "libavcodec.so.61", "libavcodec.so.60",
                                            "libavcodec.so.59", "libavcodec.so.58"};
struct AvFrame {
    std::uint8_t *data[8];
    int linesize[8];
    std::uint8_t **extended_data;
    int width, height, nb_samples, format;
};
// AV_CODEC_ID_H264, AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P and AV_LOG_QUIET.
constexpr int codec_h264 = 27, pixel_yuv420p = 0, pixel_yuvj420p = 12, log_quiet = -8;
// AV_INPUT_BUFFER_PADDING_SIZE: zero bytes the bitstream reader may read past a packet.
constexpr std::size_t packet_padding = 64;
// AVERROR(EAGAIN) and AVERROR_EOF.
constexpr int error_again = -EAGAIN, error_eof = -int(unsigned('E') | unsigned('O') << 8 |
                                                      unsigned('F') << 16 | unsigned(' ') << 24);
[[noreturn]] void fail(const std::string &reason) {
    throw VoyemuError("STARTUP_VIDEO", "libavcodec " + reason);
}

class LibavcodecDecoder final : public VideoDecoder {
public:
    LibavcodecDecoder(std::unique_ptr<Library> library, const VideoStream &stream)
        : library_(std::move(library)), parameter_sets_(stream.parameter_sets) {
        const void *(*find_decoder)(int){};
        void *(*alloc_context)(const void *){};
        int (*open)(void *, const void *, void **){};
        void *(*packet_alloc)(){};
        void *(*frame_alloc)(){};
        int (*dict_set)(void **, const char *, const char *, int){};
        void (*dict_free)(void **){};
        void (*log_level)(int){};

        bind(find_decoder, "avcodec_find_decoder");
        bind(alloc_context, "avcodec_alloc_context3");
        bind(open, "avcodec_open2");
        bind(free_context_, "avcodec_free_context");
        bind(send_packet_, "avcodec_send_packet");
        bind(receive_frame_, "avcodec_receive_frame");
        bind(packet_alloc, "av_packet_alloc");
        bind(packet_free_, "av_packet_free");
        bind(packet_from_data_, "av_packet_from_data");
        bind(packet_unref_, "av_packet_unref");
        bind(frame_alloc, "av_frame_alloc");
        bind(frame_free_, "av_frame_free");
        bind(frame_unref_, "av_frame_unref");
        bind(malloc_, "av_malloc");
        bind(free_, "av_free");
        bind(dict_set, "av_dict_set");
        bind(dict_free, "av_dict_free");
        bind(log_level, "av_log_set_level");
        // Failures surface as status codes; the library's own log stays quiet.
        log_level(log_quiet);
        const auto *codec = find_decoder(codec_h264);

        if (!codec) {
            fail("has no H.264 decoder");
        }

        try {
            context_ = alloc_context(codec);
            packet_ = packet_alloc();
            frame_ = frame_alloc();

            if (!context_ || !packet_ || !frame_) {
                fail("cannot allocate the decoder");
            }

            void *options = nullptr;
            dict_set(&options, "threads", "auto", 0);
            const int opened = open(context_, codec, &options);
            dict_free(&options);

            if (opened < 0) {
                fail("cannot open the H.264 decoder: " + std::to_string(opened));
            }
        } catch (...) {
            release();
            throw;
        }
    }

    ~LibavcodecDecoder() override {
        release();
    }

    LibavcodecDecoder(const LibavcodecDecoder &) = delete;
    LibavcodecDecoder &operator=(const LibavcodecDecoder &) = delete;

    // The first access unit carries the parameter sets in front of its own NAL units.
    void decode(const std::vector<std::string_view> &units, const VideoPictureSink &sink) override {
        std::string bytes;

        if (first_) {
            bytes = annex_b({parameter_sets_.begin(), parameter_sets_.end()});
            first_ = false;
        }

        bytes += annex_b(units);
        auto *data = static_cast<std::uint8_t *>(malloc_(bytes.size() + packet_padding));

        if (!data) {
            fail("cannot allocate a packet");
        }

        std::memcpy(data, bytes.data(), bytes.size());
        std::memset(data + bytes.size(), 0, packet_padding);

        if (packet_from_data_(packet_, data, int(bytes.size())) < 0) {
            free_(data);
            fail("cannot wrap a packet");
        }

        int status = send_packet_(context_, packet_);

        while (status == error_again) {
            receive(sink);
            status = send_packet_(context_, packet_);
        }

        packet_unref_(packet_);

        if (status < 0) {
            fail("rejects an access unit: " + std::to_string(status));
        }

        receive(sink);
    }

    void flush(const VideoPictureSink &sink) override {
        const int status = send_packet_(context_, nullptr);

        if (status < 0 && status != error_eof) {
            fail("cannot drain: " + std::to_string(status));
        }

        receive(sink);
    }

private:
    void release() {
        if (frame_) {
            frame_free_(&frame_);
        }

        if (packet_) {
            packet_free_(&packet_);
        }

        if (context_) {
            free_context_(&context_);
        }
    }

    template <class Function> void bind(Function &function, const char *name) {
        function = reinterpret_cast<Function>(library_->symbol(name));

        if (!function) {
            fail(std::string("lacks ") + name);
        }
    }

    // Emits every ready picture until the decoder needs input or has drained.
    void receive(const VideoPictureSink &sink) {
        for (;;) {
            const int status = receive_frame_(context_, frame_);

            if (status == error_again || status == error_eof) {
                return;
            }

            if (status < 0) {
                fail("rejects a picture: " + std::to_string(status));
            }

            const auto &frame = *static_cast<const AvFrame *>(frame_);
            const bool planar = (frame.format == pixel_yuv420p || frame.format == pixel_yuvj420p) &&
                                frame.width > 0 && frame.height > 0 && frame.linesize[0] > 0 &&
                                frame.linesize[1] > 0 && frame.linesize[1] == frame.linesize[2];

            try {
                if (!planar) {
                    fail("decodes into another picture format");
                }

                sink({unsigned(frame.width), unsigned(frame.height), frame.data[0], frame.data[1],
                      frame.data[2], std::size_t(frame.linesize[0]), std::size_t(frame.linesize[1]),
                      1});
            } catch (...) {
                frame_unref_(frame_);
                throw;
            }

            frame_unref_(frame_);
        }
    }

    std::unique_ptr<Library> library_;
    std::vector<std::string> parameter_sets_;
    bool first_ = true;
    void *context_{}, *packet_{}, *frame_{};
    void (*free_context_)(void **){};
    int (*send_packet_)(void *, const void *){};
    int (*receive_frame_)(void *, void *){};
    void (*packet_free_)(void **){};
    int (*packet_from_data_)(void *, std::uint8_t *, int){};
    void (*packet_unref_)(void *){};
    void (*frame_free_)(void **){};
    void (*frame_unref_)(void *){};
    void *(*malloc_)(std::size_t){};
    void (*free_)(void *){};
};
} // namespace

std::unique_ptr<VideoDecoder> host_video_decoder(const VideoStream &stream) {
    std::string reasons;

    for (const auto *name : libavcodec_names) {
        auto library = std::make_unique<Library>(name);

        if (library->loaded()) {
            return std::make_unique<LibavcodecDecoder>(std::move(library), stream);
        }

        reasons += (reasons.empty() ? "" : "; ") + library->failure();
    }

    fail("absent: " + reasons);
}
} // namespace voyemu::os
