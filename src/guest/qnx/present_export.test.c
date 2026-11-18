/* Host unit test for the present read-back BMP packager in present_export.h.
 * Verifies the header layout byte for byte against the host decoder contract
 * (BM, offset 54, dib 40, 1920x-720, planes 1, bpp 32, no compression, exact
 * file extent), the env gate (absent uses the adapter default, '1' on, '0' and
 * any other value off), and the pre-post snapshot: full-extent copy from a strided
 * source with row-preserving geometry, plus the reject rules (wrong geometry,
 * short stride, null pointers). The wire marker a[2]==0 (out-of-sequence)
 * is asserted by construction: no sequence is exported on the wire. */
#define QGFX_PRESENT_EXPORT_TEST
#include "present_export.h"
#include <assert.h>
#include <string.h>
static char *present_export_env;

char *qgfx_present_getenv(const char *name) {
    (void)name;

    return present_export_env;
}

static unsigned header_u32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

static unsigned short header_u16(const unsigned char *p) {
    return (unsigned short)(p[0] | (p[1] << 8));
}

int main(void) {
    unsigned char header[54];

    present_export_header(header);
    assert(header[0] == 'B' && header[1] == 'M');
    assert(header_u32(header + 10) == 54u);
    assert(header_u32(header + 2) == 54u + 7680u * 720u);
    assert(header_u32(header + 14) == 40u);
    assert(header_u32(header + 18) == 1920u);
    assert(header_u32(header + 22) == (unsigned)(-720)); /* top-down */
    assert(header[26] == 1 && header[27] == 0);          /* one plane */
    assert(header[28] == 32 && header[29] == 0);         /* 32 bpp */
    assert(header_u32(header + 30) == 0u);               /* BI_RGB */

    for (unsigned i = 34; i < 54; i++) {
        assert(header[i] == 0); /* rest of DIB zeroed */
    }

    for (int default_enabled = 0; default_enabled <= 1; ++default_enabled) {
        assert(present_export_enabled_value(0, default_enabled) == default_enabled);
        assert(present_export_enabled_value("1", default_enabled) == 1);
        assert(present_export_enabled_value("0", default_enabled) == 0);
        assert(present_export_enabled_value("", default_enabled) == 0);
        assert(present_export_enabled_value("10", default_enabled) == 0);
        assert(present_export_enabled_value("on", default_enabled) == 0);
    }

    /* Snapshot: build a tiny-in-memory 1920x720 source with a per-row marker
     * byte and a stride wider than the minimum (7800>7680) to prove rows are
     * read from the stride offset and packed at 7680 without tail overlap;
     * verifying every pixel keeps the maths honest (2.7M reads, instant). */
    static unsigned char source[7800 * 720], bmp[54 + 7680 * 720];

    for (int y = 0; y < 720; y++) {
        for (int x = 0; x < 1920; x++) {
            unsigned char *p = source + (size_t)y * 7800 + (size_t)x * 4;
            p[0] = (unsigned char)(x & 255);
            p[1] = (unsigned char)(y & 255);
            p[2] = (unsigned char)((x ^ y) & 255);
            p[3] = 0xDD;
        }
    }

    assert(present_export_snapshot(bmp, source, 1920, 720, 7800) == 1);
    assert(bmp[0] == 'B' && bmp[1] == 'M');

    for (int y = 0; y < 720; y++) {
        for (int x = 0; x < 1920; x++) {
            const unsigned char *src = source + (size_t)y * 7800 + (size_t)x * 4,
                                *dst = bmp + 54 + (size_t)y * 7680 + (size_t)x * 4;
            assert(dst[0] == src[0] && dst[1] == src[1] && dst[2] == src[2] && dst[3] == src[3]);
        }
    }

    /* Reject rules: geometry gate and stride floor must refuse, not read OOB. */
    memset(bmp, 0x41, sizeof(bmp));
    assert(present_export_snapshot(0, source, 1920, 720, 7800) == 0);
    assert(present_export_snapshot(bmp, 0, 1920, 720, 7800) == 0);
    assert(present_export_snapshot(bmp, source, 1920, 719, 7800) == 0);
    assert(present_export_snapshot(bmp, source, 1280, 720, 7800) == 0);
    assert(present_export_snapshot(bmp, source, 1920, 720, 7679) == 0); /* below floor */
    assert(bmp[54] == 0x41);                                            /* untouched on refusal */
    /* Headers stay consistent through repeated snapshots (no statics). */
    assert(present_export_snapshot(bmp, source, 1920, 720, 7800) == 1);
    assert(header_u32(bmp + 2) == 54u + 7680u * 720u);

    return 0;
}
