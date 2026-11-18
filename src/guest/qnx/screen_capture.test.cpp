#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern "C" {
#include "screen_capture.c"
}
static int step, fail_step, displays_count = 16, bad_size, bad_format, bad_stride, bad_pointer,
                            bad_write, bad_finish;
static int context_destroyed, pixmap_destroyed, finished, captured, writes;
static size_t total;
static unsigned char pixels[720 * 7684];

static int call(void) {
    return ++step == fail_step ? -1 : 0;
}

int screen_create_context(void **c, int f) {
    assert(f == 8);

    if (call()) {
        return -1;
    }

    *c = (void *)1;

    return 0;
}

int screen_destroy_context(void *c) {
    assert(c == (void *)1);
    context_destroyed++;

    return call();
}

int screen_get_context_property_iv(void *c, int p, int *v) {
    assert(c == (void *)1 && p == 59);

    if (call()) {
        return -1;
    }

    *v = displays_count;

    return 0;
}

int screen_get_context_property_pv(void *c, int p, void **v) {
    assert(c == (void *)1 && p == 60);

    if (call()) {
        return -1;
    }

    for (int i = 0; i < displays_count; i++) {
        v[i] = (void *)(size_t)(10 + i);
    }

    return 0;
}

int screen_get_display_property_iv(void *d, int p, int *v) {
    assert(d == (void *)10 && p == 40);

    if (call()) {
        return -1;
    }

    v[0] = bad_size ? 1919 : 1920;
    v[1] = 720;

    return 0;
}

int screen_create_pixmap(void **p, void *c) {
    assert(c == (void *)1);

    if (call()) {
        return -1;
    }

    *p = (void *)2;

    return 0;
}

int screen_destroy_pixmap(void *p) {
    assert(p == (void *)2);
    pixmap_destroyed++;

    if (finished) {
        assert(total == 5529654 && captured);
    }

    return call();
}

int screen_set_pixmap_property_iv(void *p, int field, const int *v) {
    assert(p == (void *)2);
    assert((field == 5 && v[0] == 1920 && v[1] == 720) || (field == 14 && v[0] == 8) ||
           (field == 48 && v[0] == 10));

    return call();
}

int screen_create_pixmap_buffer(void *p) {
    assert(p == (void *)2);

    return call();
}

int screen_get_pixmap_property_pv(void *p, int field, void **v) {
    assert(p == (void *)2 && field == 37);

    if (call()) {
        return -1;
    }

    v[0] = (void *)3;

    return 0;
}

int screen_get_buffer_property_iv(void *b, int p, int *v) {
    assert(b == (void *)3);

    if (call()) {
        return -1;
    }

    if (p == 5) {
        v[0] = 1920;
        v[1] = 720;
    } else if (p == 14) {
        *v = bad_format ? 9 : 8;
    } else {
        assert(p == 44);
        *v = bad_stride ? 7679 : 7684;
    }

    return 0;
}

int screen_get_buffer_property_pv(void *b, int p, void **v) {
    assert(b == (void *)3 && p == 34);

    if (call()) {
        return -1;
    }

    *v = bad_pointer ? nullptr : pixels;

    return 0;
}

int screen_read_display(void *d, void *b, int n, const int *r, int f) {
    assert(d == (void *)10 && b == (void *)3 && !n && !r && !f);

    if (call()) {
        return -1;
    }

    captured = 1;

    return 0;
}

static unsigned word(const unsigned char *p) {
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}

static int write_chunk(void *s, const unsigned char *p, size_t n) {
    assert(s == (void *)4 && captured && !pixmap_destroyed);
    writes++;

    if (bad_write && writes == bad_write) {
        return -1;
    }

    if (writes == 1) {
        assert(n == 54 && p[0] == 'B' && p[1] == 'M' && word(p + 2) == 5529654 &&
               word(p + 10) == 54 && word(p + 18) == 1920 && word(p + 22) == (unsigned)-720 &&
               p[26] == 1 && p[28] == 32);
    } else {
        assert(n == 7680);
        assert(p == pixels + (size_t)(writes - 2) * 7684);
    }

    total += n;

    return 0;
}

static int finish(void *s, int complete) {
    assert(s == (void *)4 && !pixmap_destroyed);

    if (!complete) {
        assert(bad_write);

        return 0;
    }

    assert(writes == 721 && total == 5529654);

    if (bad_finish) {
        return -1;
    }

    finished = 1;

    return 0;
}

static void reset(void) {
    step = fail_step = bad_size = bad_format = bad_stride = bad_pointer = bad_write = bad_finish =
            0;
    context_destroyed = pixmap_destroyed = finished = captured = writes = 0;
    total = 0;
    displays_count = 16;
}

static int capture(void) {
    return voyemu_screen_capture(write_chunk, finish, (void *)4);
}

int main(void) {
    reset();
    assert(capture() == 0 && finished && context_destroyed == 1 && pixmap_destroyed == 1);
    int calls = step;

    for (int i = 1; i <= calls; i++) {
        reset();
        fail_step = i;
        assert(capture() != 0);
        assert(context_destroyed == (i > 1));
        assert(pixmap_destroyed == (i > 5));
    }

    reset();
    displays_count = 0;
    assert(capture() != 0 && !pixmap_destroyed && context_destroyed);
    reset();
    displays_count = 17;
    assert(capture() != 0 && !pixmap_destroyed && context_destroyed);
    reset();
    bad_size = 1;
    assert(capture() != 0 && !pixmap_destroyed);
    reset();
    bad_format = 1;
    assert(capture() != 0 && !captured && pixmap_destroyed);
    reset();
    bad_stride = 1;
    assert(capture() != 0 && !captured && pixmap_destroyed);
    reset();
    bad_pointer = 1;
    assert(capture() != 0 && !captured && pixmap_destroyed);
    reset();
    bad_write = 1;
    assert(capture() != 0 && !finished && pixmap_destroyed);
    reset();
    bad_write = 721;
    assert(capture() != 0 && !finished && pixmap_destroyed);
    reset();
    bad_finish = 1;
    assert(capture() != 0 && !finished && pixmap_destroyed);
    reset();
    assert(voyemu_screen_capture(nullptr, finish, (void *)4) != 0 && !step);
    assert(voyemu_screen_capture(write_chunk, nullptr, (void *)4) != 0 && !step);
    puts("Screen capture host mocks passed; no QNX process or real frame tested");

    return 0;
}
