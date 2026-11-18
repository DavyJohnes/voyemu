/* Compile the actual full frontend as C, including its lookup table. */
#define QGFX_PASSION_ROOT_IMAGEFS 1
#define QGFX_WRAPPER_TEST 1
#define QSM_TEST 1
#define QGFX_INITIALIZER
#define QBW_ENTRY_TEST 1
#define QGFX_CAPTURE_ENTRY_TEST 1
#define __asm__(name)
#define write qgfx_test_write
#define usleep qgfx_test_usleep
#include "passion_front_entry.c"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
static u64 expected_selector, remote_result;
static unsigned calls, last_op, last_count;
static u64 last_args[4];

/* Only the egl selectors have a fixed shape; every other call is recorded so that the tests below
   can assert on the opcode and the arguments of the last one. */
u64 test_remote(Packet *p, u32 op, const u64 *args, unsigned count, const void *input,
                unsigned bytes) {
    if (op == 70) {
        assert(p && count == 1 && args[0] == expected_selector && !input && !bytes);
    }

    last_op = op;
    last_count = count;

    for (unsigned i = 0; i < 4; i++) {
        last_args[i] = i < count ? args[i] : 0;
    }

    calls++;

    return remote_result;
}

long qgfx_test_write(int fd, const void *p, usize bytes) {
    (void)fd;
    (void)p;
    (void)bytes;
    assert(0);

    return -1;
}

int qgfx_test_usleep(unsigned delay) {
    (void)delay;
    assert(0);

    return -1;
}

int *__get_errno_ptr(void) {
    assert(0);

    return 0;
}

int mem_offset(const void *p, int fd, usize bytes, i64 *address, usize *span) {
    (void)p;
    (void)fd;
    (void)bytes;
    (void)address;
    (void)span;
    assert(0);

    return -1;
}

#define FAIL()                                                                                     \
    do {                                                                                           \
        assert(0);                                                                                 \
        return -1;                                                                                 \
    } while (0)

int qsm_open(const char *n, int f, unsigned m) {
    (void)n;
    (void)f;
    (void)m;
    FAIL();
}

int qsm_ctl(int f, int c, QsmU64 p, QsmU64 n) {
    (void)f;
    (void)c;
    (void)p;
    (void)n;
    FAIL();
}

void *qsm_map(void *p, QsmSize n, int prot, int f, int fd, long long o) {
    (void)p;
    (void)n;
    (void)prot;
    (void)f;
    (void)fd;
    (void)o;
    assert(0);

    return 0;
}

int qsm_close(int fd) {
    (void)fd;
    FAIL();
}

int qsm_unmap(void *p, QsmSize n) {
    (void)p;
    (void)n;
    FAIL();
}

#define STUB_IV(name)                                                                              \
    int name(void *p, int k, int *v) {                                                             \
        (void)p;                                                                                   \
        (void)k;                                                                                   \
        (void)v;                                                                                   \
        FAIL();                                                                                    \
    }
#define STUB_PV(name)                                                                              \
    int name(void *p, int k, void **v) {                                                           \
        (void)p;                                                                                   \
        (void)k;                                                                                   \
        (void)v;                                                                                   \
        FAIL();                                                                                    \
    }
STUB_IV(screen_get_window_property_iv)
STUB_IV(screen_get_buffer_property_iv)
STUB_IV(screen_get_context_property_iv)
STUB_IV(screen_get_display_property_iv)
STUB_PV(screen_get_window_property_pv)
STUB_PV(screen_get_buffer_property_pv)
STUB_PV(screen_get_context_property_pv)
STUB_PV(screen_get_pixmap_property_pv)

int screen_set_window_property_iv(void *p, int k, const int *v) {
    (void)p;
    (void)k;
    (void)v;
    FAIL();
}

int screen_set_pixmap_property_iv(void *p, int k, const int *v) {
    (void)p;
    (void)k;
    (void)v;
    FAIL();
}

int screen_create_context(void **p, int f) {
    (void)p;
    (void)f;
    FAIL();
}

int screen_destroy_context(void *p) {
    (void)p;
    FAIL();
}

int screen_create_pixmap(void **p, void *c) {
    (void)p;
    (void)c;
    FAIL();
}

int screen_create_pixmap_buffer(void *p) {
    (void)p;
    FAIL();
}

int screen_destroy_window_buffers(void *p) {
    (void)p;
    FAIL();
}

int screen_post_window(void *p, void *b, int n, const int *r, int f) {
    (void)p;
    (void)b;
    (void)n;
    (void)r;
    (void)f;
    FAIL();
}

char **qbw_environ;

char *qbw_getenv(const char *n) {
    (void)n;
    assert(0);

    return 0;
}

int qbw_getpid(void) {
    FAIL();
}

int qbw_getppid(void) {
    FAIL();
}

int *qbw_errno(void) {
    assert(0);

    return 0;
}

int qbw_clock(int c, QbwTimespec *t) {
    (void)c;
    (void)t;
    FAIL();
}

int qbw_nanosleep(const QbwTimespec *a, QbwTimespec *b) {
    (void)a;
    (void)b;
    FAIL();
}

int qbw_spawn(int *p, const char *n, const void *a, const void *b, char *const *v, char *const *e) {
    (void)p;
    (void)n;
    (void)a;
    (void)b;
    (void)v;
    (void)e;
    FAIL();
}

int qbw_waitpid(int p, int *s, int f) {
    (void)p;
    (void)s;
    (void)f;
    FAIL();
}

int qbw_kill(int p, int s) {
    (void)p;
    (void)s;
    FAIL();
}

long qbw_write(int f, const void *p, unsigned long n) {
    (void)f;
    (void)p;
    (void)n;
    FAIL();
}

void qbw_exit(int status) {
    (void)status;
    abort();
}

VoyemuCaptureFile *capture_file_open(const char *p, const char *m) {
    (void)p;
    (void)m;
    assert(0);

    return 0;
}

size_t capture_file_write(const void *p, size_t a, size_t b, VoyemuCaptureFile *f) {
    (void)p;
    (void)a;
    (void)b;
    (void)f;
    assert(0);

    return 0;
}

int capture_file_flush(VoyemuCaptureFile *f) {
    (void)f;
    FAIL();
}

int capture_file_close(VoyemuCaptureFile *f) {
    (void)f;
    FAIL();
}

int capture_file_remove(const char *p) {
    (void)p;
    FAIL();
}

const char *capture_environment(const char *p) {
    (void)p;
    assert(0);

    return 0;
}

void capture_process_exit(int status) {
    (void)status;
    abort();
}

void *mcu_file_open(const char *p, const char *m) {
    (void)p;
    (void)m;
    assert(0);

    return 0;
}

usize mcu_file_write(const void *p, usize a, usize b, void *f) {
    (void)p;
    (void)a;
    (void)b;
    (void)f;
    assert(0);

    return 0;
}

int mcu_file_close(void *f) {
    (void)f;
    FAIL();
}

int qpw_spawn(int *p, const char *n, const void *a, const void *b, char *const *v, char *const *e) {
    (void)p;
    (void)n;
    (void)a;
    (void)b;
    (void)v;
    (void)e;
    FAIL();
}

int qpw_wait(int p, int *s, int f) {
    (void)p;
    (void)s;
    (void)f;
    FAIL();
}

int qpw_kill(int p, int s) {
    (void)p;
    (void)s;
    FAIL();
}

int qpw_sleep(unsigned u) {
    (void)u;
    FAIL();
}

int *qpw_errno(void) {
    assert(0);

    return 0;
}

int qpw_pipe(int *p) {
    (void)p;
    FAIL();
}

int qpw_close(int p) {
    (void)p;
    FAIL();
}

long qpw_read(int p, void *b, unsigned long n) {
    (void)p;
    (void)b;
    (void)n;
    FAIL();
}

int qpw_poll(QpwPoll *p, unsigned n, int t) {
    (void)p;
    (void)n;
    (void)t;
    FAIL();
}

int qpw_actions_init(QpwFileActions *a) {
    (void)a;
    FAIL();
}

int qpw_actions_destroy(QpwFileActions *a) {
    (void)a;
    FAIL();
}

int qpw_actions_dup(QpwFileActions *a, int f, int d) {
    (void)a;
    (void)f;
    (void)d;
    FAIL();
}

int qpw_actions_close(QpwFileActions *a, int f) {
    (void)a;
    (void)f;
    FAIL();
}

int qgfx_passion_surface_test(void) {
    assert(eglGetProcAddress("eglGetCurrentSurface") == (Proc)eglGetCurrentSurface);
    assert(eglGetProcAddress("eglGetDisplay") == (Proc)eglGetDisplay);
    assert(eglGetProcAddress("unknown-valid-name") == 0);
    EGLSurface (*current)(EGLint) =
            (EGLSurface (*)(EGLint))eglGetProcAddress("eglGetCurrentSurface");
    expected_selector = 0x3059;
    remote_result = 0x123456789abcdef0ULL;
    assert((u64)current(0x3059) == remote_result && calls == 1);
    expected_selector = 0x305a;
    remote_result = 0;
    assert(!current(0x305a) && calls == 2);
    expected_selector = (u64)(i64)-1;
    remote_result = 0;
    assert(!current(-1) && calls == 3);
    /* Qt sets every vec2 uniform through the vector form, and the wire carries vec2 uniforms only
       in the scalar form: untranslated, the call would bypass the shim and the host would keep the
       zero default. */
    assert(eglGetProcAddress("glUniform2fv") == (Proc)glUniform2fv &&
           eglGetProcAddress("glUniform2f") == (Proc)glUniform2f);
    float pair[4] = {0.5f, -0.25f, 1.0f, 2.0f};
    calls = 0;
    glUniform2fv(7, 2, pair);
    assert(calls == 2 && last_op == 188 && last_count == 3 && (i64)last_args[0] == 8 &&
           last_args[1] == fbits(1.0f) && last_args[2] == fbits(2.0f));
    glUniform2fv(9, 0, pair);
    glUniform2fv(9, -1, pair);
    assert(calls == 2);
    transport_error = 0;
    glUniform2fv(0x7fffffff, 2, pair);
    assert(calls == 2 && transport_error);
    transport_error = 0;
    glUniform2fv(5, 1, pair + 2);
    assert(calls == 3 && last_op == 188 && last_count == 3 && (i64)last_args[0] == 5 &&
           last_args[1] == fbits(1.0f) && last_args[2] == fbits(2.0f));

    return 0;
}
