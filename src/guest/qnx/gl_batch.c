/* Caller owns api_lock. Copy inputs now; flush before any synchronous call. */
extern char *getenv(const char *);
static unsigned char gl_batch_data[3840];
static unsigned gl_batch_bytes, gl_batch_count;

static int flush_gl_batch(void) {
    if (!gl_batch_count) {
        return 1;
    }

    unsigned count = gl_batch_count, bytes = gl_batch_bytes;
    gl_batch_count = gl_batch_bytes = 0;
    u64 args[] = {count};
    u64 result = call_raw(360, args, 1, gl_batch_data, bytes);

    if (result != count) {
        transport_error = 1;

        return 0;
    }

    return 1;
}

static u64 call(u32 op, const u64 *args, unsigned n, const void *input, unsigned bytes) {
    if (!flush_gl_batch()) {
        return 0;
    }

    return call_raw(op, args, n, input, bytes);
}

static u64 send_void(u32 op, const u64 *args, unsigned n, const void *input, unsigned bytes) {
    const char *flag = getenv("QGFX_GL_BATCH");

    if (!flag || flag[0] != '1' || flag[1]) {
        return call(op, args, n, input, bytes);
    }

    if (n > 8 || bytes > 3840 || (bytes && !input)) {
        transport_error = 1;

        return 0;
    }

    unsigned extent = 16 + n * 8 + ((bytes + 7) & ~7u);

    if (extent > 3840) {
        if (!flush_gl_batch()) {
            return 0;
        }

        return call_raw(op, args, n, input, bytes);
    }

    if (gl_batch_count == 64 || gl_batch_bytes + extent > 3840) {
        if (!flush_gl_batch()) {
            return 0;
        }
    }

    unsigned char *p = gl_batch_data + gl_batch_bytes;

    for (unsigned i = 0; i < extent; i++) {
        p[i] = 0;
    }

    u32 header[] = {op, n, bytes, 0};
    copy(p, header, 16);

    if (n) {
        copy(p + 16, args, (usize)n * 8);
    }

    if (bytes) {
        copy(p + 16 + (u64)n * 8, input, bytes);
    }

    gl_batch_bytes += extent;
    gl_batch_count++;

    return 1;
}
