/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#include "helper_dispatch.h"
#include "screen_capture.h"
/* FILE stays opaque: only public stdio functions access its representation. */
typedef struct VoyemuCaptureFile VoyemuCaptureFile;
#ifndef QGFX_CAPTURE_ENTRY_TEST
#define CAPTURE_LIBC(name) __asm__(#name)
#else
#define CAPTURE_LIBC(name)
#endif
extern VoyemuCaptureFile *capture_file_open(const char *, const char *) CAPTURE_LIBC(fopen);
extern size_t capture_file_write(const void *, size_t, size_t, VoyemuCaptureFile *)
        CAPTURE_LIBC(fwrite);
extern int capture_file_flush(VoyemuCaptureFile *) CAPTURE_LIBC(fflush);
extern int capture_file_close(VoyemuCaptureFile *) CAPTURE_LIBC(fclose);
extern int capture_file_remove(const char *) CAPTURE_LIBC(unlink);
extern const char *capture_environment(const char *) CAPTURE_LIBC(getenv);
extern void capture_process_exit(int) CAPTURE_LIBC(exit) __attribute__((noreturn));
#undef CAPTURE_LIBC
static const char source_capture_path[] = "/log/oem-screen.bmp";
typedef struct {
    VoyemuCaptureFile *file;
    size_t bytes;
    int failed;
} SourceCaptureSink;

static int source_capture_write(void *opaque, const unsigned char *bytes, size_t size) {
    SourceCaptureSink *sink = (SourceCaptureSink *)opaque;

    if (!sink->file || sink->failed || size > 5529654 - sink->bytes) {
        return -1;
    }

    size_t at = 0;

    while (at < size) {
        size_t count = capture_file_write(bytes + at, 1, size - at, sink->file);

        if (!count || count > size - at) {
            sink->failed = 1;

            return -1;
        }

        at += count;
    }

    sink->bytes += size;

    return 0;
}

static int source_capture_finish(void *opaque, int complete) {
    SourceCaptureSink *sink = (SourceCaptureSink *)opaque;
    int valid = complete && !sink->failed && sink->bytes == 5529654;

    if (sink->file) {
        if (valid && capture_file_flush(sink->file)) {
            valid = 0;
        }

        if (capture_file_close(sink->file)) {
            valid = 0;
        }

        sink->file = 0;
    } else {
        valid = 0;
    }

    if (!valid) {
        sink->failed = 1;
        capture_file_remove(source_capture_path);

        return -1;
    }

    return 0;
}

static int source_capture_enabled(const char *value) {
    return value && value[0] == '1' && !value[1];
}

#ifndef QGFX_CAPTURE_ENTRY_TEST
QGFX_INITIALIZER
#endif

static void source_capture_start(void) {
    QGFX_HELPER_DISPATCH();

    if (!source_capture_enabled(capture_environment("QGFX_SOURCE_CAPTURE"))) {
        return;
    }

    /* This path is used only in an initialized, dedicated preload carrier. */
    SourceCaptureSink sink = {0, 0, 0};
    capture_file_remove(source_capture_path);
    sink.file = capture_file_open(source_capture_path, "wb");

    if (!sink.file) {
        capture_process_exit(1);
    }

    int result = voyemu_screen_capture(source_capture_write, source_capture_finish, &sink);

    if (sink.file) {
        source_capture_finish(&sink, 0);
    }

    if (result || sink.failed) {
        capture_file_remove(source_capture_path);
        capture_process_exit(1);
    }

    capture_process_exit(0);
}

/* NOLINTEND(modernize-use-nullptr) */
