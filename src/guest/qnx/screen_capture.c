/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#include "screen_capture.h"
/* Public QNX 7 Screen ABI declarations; see the capture design for official
 * function/property/context/usage/format references. No OEM function bodies. */
extern int screen_create_context(void **, int);
extern int screen_destroy_context(void *);
extern int screen_get_context_property_iv(void *, int, int *);
extern int screen_get_context_property_pv(void *, int, void **);
extern int screen_get_display_property_iv(void *, int, int *);
extern int screen_create_pixmap(void **, void *);
extern int screen_destroy_pixmap(void *);
extern int screen_set_pixmap_property_iv(void *, int, const int *);
extern int screen_create_pixmap_buffer(void *);
extern int screen_get_pixmap_property_pv(void *, int, void **);
extern int screen_get_buffer_property_iv(void *, int, int *);
extern int screen_get_buffer_property_pv(void *, int, void **);
extern int screen_read_display(void *, void *, int, const int *, int);

static void capture_word(unsigned char *p, unsigned value) {
    for (unsigned i = 0; i < 4; i++) {
        p[i] = (unsigned char)(value >> (i * 8));
    }
}

int voyemu_screen_capture(VoyemuCaptureWrite write, VoyemuCaptureFinish finish, void *sink) {
    void *context = 0, *pixmap = 0, *displays[16] = {0}, *buffers[16] = {0}, *pixels = 0;
    int count = 0, size[2] = {1920, 720}, actual[2] = {0, 0}, format = 8, usage = 10, stride = 0,
        result = -1, output_started = 0, output_finished = 0;
    unsigned char header[54] = {0};

    if (!write || !finish) {
        return -1;
    }

    if (screen_create_context(&context, 8) || !context) {
        return -1;
    }

    if (screen_get_context_property_iv(context, 59, &count) || count < 1 || count > 16) {
        goto done;
    }

    if (screen_get_context_property_pv(context, 60, displays) || !displays[0]) {
        goto done;
    }

    if (screen_get_display_property_iv(displays[0], 40, actual) || actual[0] != 1920 ||
        actual[1] != 720) {
        goto done;
    }

    if (screen_create_pixmap(&pixmap, context) || !pixmap) {
        goto done;
    }

    if (screen_set_pixmap_property_iv(pixmap, 5, size) ||
        screen_set_pixmap_property_iv(pixmap, 14, &format) ||
        screen_set_pixmap_property_iv(pixmap, 48, &usage) || screen_create_pixmap_buffer(pixmap)) {
        goto done;
    }

    /* create_pixmap_buffer creates one buffer; reserve a bounded array as well. */
    if (screen_get_pixmap_property_pv(pixmap, 37, buffers) || !buffers[0]) {
        goto done;
    }

    if (screen_get_buffer_property_iv(buffers[0], 5, actual) || actual[0] != 1920 ||
        actual[1] != 720 || screen_get_buffer_property_iv(buffers[0], 14, &format) || format != 8 ||
        screen_get_buffer_property_iv(buffers[0], 44, &stride) || stride < 7680 || stride > 65536 ||
        screen_get_buffer_property_pv(buffers[0], 34, &pixels) || !pixels) {
        goto done;
    }

    if (screen_read_display(displays[0], buffers[0], 0, 0, 0)) {
        goto done;
    }

    header[0] = 'B';
    header[1] = 'M';
    capture_word(header + 2, 5529654);
    capture_word(header + 10, 54);
    capture_word(header + 14, 40);
    capture_word(header + 18, 1920);
    capture_word(header + 22, (unsigned)-720);
    header[26] = 1;
    header[28] = 32;
    capture_word(header + 34, 5529600);
    output_started = 1;

    if (write(sink, header, sizeof(header))) {
        goto done;
    }

    for (unsigned y = 0; y < 720; y++) {
        if (write(sink, (const unsigned char *)pixels + (size_t)y * (unsigned)stride, 7680)) {
            goto done;
        }
    }

    output_finished = 1;

    if (finish(sink, 1)) {
        goto done;
    }

    result = 0;
done:

    if (output_started && !output_finished) {
        finish(sink, 0);
    }

    if (pixmap && screen_destroy_pixmap(pixmap)) {
        result = -1;
    }

    if (screen_destroy_context(context)) {
        result = -1;
    }

    return result;
}

/* NOLINTEND(modernize-use-nullptr) */
