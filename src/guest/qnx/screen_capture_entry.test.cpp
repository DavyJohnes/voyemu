#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#define QGFX_CAPTURE_ENTRY_TEST
extern "C" {
#include "screen_capture_entry.c"
}
static jmp_buf exited;
static const char *flag;
static int calls, capture_calls, removed, opened, closed, flushed, exit_code, fail_open, fail_write,
        fail_flush, fail_close, fail_capture, partial;
static size_t written;

const char *capture_environment(const char *name) {
    assert(!strcmp(name, "QGFX_SOURCE_CAPTURE"));

    return flag;
}

VoyemuCaptureFile *capture_file_open(const char *path, const char *mode) {
    calls++;
    opened++;
    assert(!strcmp(path, "/log/oem-screen.bmp") && !strcmp(mode, "wb") && removed);

    return fail_open ? nullptr : (VoyemuCaptureFile *)1;
}

size_t capture_file_write(const void *p, size_t size, size_t count, VoyemuCaptureFile *file) {
    calls++;
    assert(p && size == 1 && count && file == (VoyemuCaptureFile *)1 && !closed);

    if (fail_write) {
        return 0;
    }

    size_t n = partial && count > 3 ? 3 : count;
    written += n;

    return n;
}

int capture_file_flush(VoyemuCaptureFile *file) {
    calls++;
    flushed++;
    assert(file == (VoyemuCaptureFile *)1 && written == 5529654 && !closed);

    return fail_flush ? -1 : 0;
}

int capture_file_close(VoyemuCaptureFile *file) {
    calls++;
    closed++;
    assert(file == (VoyemuCaptureFile *)1 && closed == 1);

    return fail_close ? -1 : 0;
}

int capture_file_remove(const char *path) {
    calls++;
    removed++;
    assert(!strcmp(path, "/log/oem-screen.bmp"));

    return 0;
}

void capture_process_exit(int code) {
    exit_code = code;
    longjmp(exited, 1);
}

int voyemu_screen_capture(VoyemuCaptureWrite write, VoyemuCaptureFinish finish, void *sink) {
    capture_calls++;

    if (fail_capture) {
        return -1;
    }

    static unsigned char frame[5529654];

    if (write(sink, frame, sizeof(frame))) {
        finish(sink, 0);
        assert(closed && removed >= 2);

        return -1;
    }

    int rc = finish(sink, 1);
    assert(closed);

    if (rc) {
        assert(removed >= 2);
    }

    return rc;
}

static void reset(void) {
    calls = capture_calls = removed = opened = closed = flushed = fail_open = fail_write =
            fail_flush = fail_close = fail_capture = partial = 0;
    exit_code = -1;
    written = 0;
    flag = "1";
}

static void run(int expected) {
    if (!setjmp(exited)) {
        source_capture_start();
        assert(0);
    }

    assert(exit_code == expected);
}

int main(void) {
    for (unsigned i = 0; i < 5; i++) {
        reset();
        const char *values[] = {nullptr, "", "0", "11", "true"};
        flag = values[i];
        source_capture_start();
        assert(!calls && !capture_calls && exit_code == -1);
    }

    reset();
    run(0);
    assert(capture_calls == 1 && opened == 1 && flushed == 1 && closed == 1 && written == 5529654);
    reset();
    partial = 1;
    run(0);
    assert(written == 5529654 && closed == 1);
    reset();
    fail_open = 1;
    run(1);
    assert(!capture_calls && !closed);
    reset();
    fail_capture = 1;
    run(1);
    assert(closed == 1 && !flushed && removed >= 2);
    reset();
    fail_write = 1;
    run(1);
    assert(closed == 1 && !flushed && removed >= 2);
    reset();
    fail_flush = 1;
    run(1);
    assert(closed == 1 && removed >= 2);
    reset();
    fail_close = 1;
    run(1);
    assert(closed == 1 && removed >= 2);
    puts("Source capture entry opt-in/stdio mocks passed; no QNX constructor execution tested");

    return 0;
}
