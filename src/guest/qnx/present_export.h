/* Ordered Screen-stream BMP packaging for the present read-back path.
 * QGFX_PRESENT_EXPORT can override the adapter's default with '0' or '1'.
 * The layout matches the host decoder byte for byte: BM magic, 54-byte offset,
 * BITMAPINFOHEADER (dib=40), 1920x720, negative (top-down) height, bpp 32, no
 * compression. Rows are straight BGRA copies of the pre-post() window buffer
 * snapshot, taken while the client still owns the buffer.
 *
 * Wire contract (host graphics.cpp op 354): a[2]==0 opts OUT of the strict
 * screen-sequence arithmetic; the continuous carrier (screen_stream.c) always
 * sends a[2]>=1 and owns that sequence namespace. The present exporter
 * deliberately sends a[2]==0 so the two exporters cannot fault each other
 * ("Screen stream sequence") and the carrier stream stays distinguishable. */
#ifndef QGFX_PRESENT_EXPORT_H
#define QGFX_PRESENT_EXPORT_H
#ifndef QGFX_PRESENT_EXPORT_TEST
extern char *qgfx_present_getenv(const char *);

static char *present_export_getenv(const char *name) {
    return qgfx_present_getenv(name);
}
#endif

static void present_export_u32(unsigned char *out, unsigned value) {
    out[0] = (unsigned char)(value & 255u);
    out[1] = (unsigned char)((value >> 8) & 255u);
    out[2] = (unsigned char)((value >> 16) & 255u);
    out[3] = (unsigned char)((value >> 24) & 255u);
}

static int present_export_enabled_value(const char *flag, int default_enabled) {
    if (!flag) {
        return default_enabled;
    }

    return flag[0] == '1' && !flag[1];
}

static void present_export_header(unsigned char *header) {
    static const unsigned pixel_bytes = 7680u * 720u, offset = 54u, file_bytes = 54u + pixel_bytes;
    const unsigned topdown_height = 0u - 720u;

    for (unsigned i = 0; i < 54; i++) {
        header[i] = 0;
    }

    header[0] = 'B';
    header[1] = 'M';
    present_export_u32(header + 2, file_bytes);
    present_export_u32(header + 10, offset);
    present_export_u32(header + 14, 40u);
    present_export_u32(header + 18, 1920u);
    present_export_u32(header + 22, topdown_height);
    header[26] = 1;
    header[28] = 32;
}

/* Frame snapshot into the caller-owned scratch BMP: bytes are copied from the
 * mapped buffer BEFORE screen_post_window(), while the client still owns it,
 * so the compositor's post-flip reuse cannot race the read. Returns 1 when the
 * full extent was snapshotted. */
static int present_export_snapshot(unsigned char *bmp, const void *pixels, int width, int height,
                                   int stride) {
    if (!bmp || !pixels || width != 1920 || height != 720 || stride < 7680 || stride > 65536) {
        return 0;
    }

    present_export_header(bmp);

    for (int y = 0; y < height; y++) {
        const unsigned char *src = (const unsigned char *)pixels + (unsigned long long)y * stride;
        unsigned char *dst = bmp + 54 + (unsigned long long)y * 7680;

        for (int x = 0; x < 7680; x += 4) {
            dst[x] = src[x];
            dst[x + 1] = src[x + 1];
            dst[x + 2] = src[x + 2];
            dst[x + 3] = src[x + 3];
        }
    }

    return 1;
}

#endif
