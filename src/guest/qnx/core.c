#include "helper_dispatch.h"
/* Typed EGL/GLES calls forwarded to the existing host ANGLE renderer.
 * The selftest is a guest graphics gate, never an OEM cluster claim. */
typedef unsigned long usize;
typedef unsigned long long u64;
typedef unsigned u32;
typedef long long i64;
typedef void *EGLDisplay;
typedef void *EGLConfig;
typedef void *EGLContext;
typedef void *EGLSurface;
typedef unsigned EGLBoolean;
typedef int EGLint;
typedef unsigned char GLboolean;
typedef unsigned GLbitfield;
typedef char GLchar;
typedef long GLsizeiptr;
typedef long GLintptr;
typedef unsigned GLenum;
typedef unsigned GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
extern int posix_memalign(void **, usize, usize);
extern int mlock(const void *, usize);
extern int munlock(const void *, usize);
extern int mem_offset(const void *, int, usize, i64 *, usize *);
extern int *__get_errno_ptr(void);
extern int snprintf(char *, usize, const char *, ...);
extern long write(int, const void *, usize);
extern int usleep(unsigned);
extern void free(void *);
typedef struct {
    u32 magic, version, state, opcode, seq, in_len, out_len, fault;
    u64 args[16], result, tid, reserved[10];
    unsigned char data[3840];
} Packet;
_Static_assert(__builtin_offsetof(Packet, data) == 256 && sizeof(Packet) == 4096, "RPC layout");
#include "api_fair_lock.h"
static Packet *packet;
static ApiFairLock api_lock;
static u32 sequence;
static int transport_error;
typedef struct {
    u64 seconds, nanoseconds;
} QgfxCloseTime;
#ifndef QGFX_WRAPPER_TEST
extern int qgfx_close_clock(int, QgfxCloseTime *) __asm__("clock_gettime");
#else
extern int qgfx_close_clock(int, QgfxCloseTime *);
#endif
static u64 channel_physical;
static int channel_closed;
extern int getpid(void);
#ifdef QGFX_PASSION_ROOT_IMAGEFS
#include "passion_shared_memory.h"

static void qgfx_memory_release(void *p, usize bytes) {
    if (qsm_release(p, bytes)) {
        transport_error = 1;
    }
}
#endif

static int channel_identity(Packet *p) {
    QgfxCloseTime opened = {0};
    const int pid = getpid();

    if (pid <= 0 || qgfx_close_clock(2, &opened) || (!opened.seconds && !opened.nanoseconds)) {
        return 0;
    }

    p->reserved[8] = (u64)(unsigned)pid;
    p->reserved[9] = opened.seconds * 1000000000ULL + opened.nanoseconds;

    return 1;
}

static usize length(const char *s) {
    usize n = 0;

    while (s && s[n] && n < 3840) {
        n++;
    }

    return n;
}

static void copy(void *d, const void *s, usize n) {
    unsigned char *a = d;
    const unsigned char *b = s;

    while (n--) {
        *a++ = *b++;
    }
}

static void emit(const char *s) {
    write(2, s, length(s));
}

#ifdef QGFX_DIAGNOSTIC_TRANSPORT
/* Pinned QNX pthread_t is signed 32-bit. Diagnostic snapshots are not setter proofs. */
extern int qdiag_pthread_self(void) __asm__("pthread_self");
static unsigned qdiag_seen, qdiag_egl_count;
static const char *qdiag_last_file, *qdiag_last_function;
static unsigned qdiag_last_line;
static int qdiag_last_thread;

static int *qdiag_transport_access(const char *file, unsigned line, const char *function) {
    int *ep = __get_errno_ptr(), saved = *ep, thread = qdiag_pthread_self();

    if (transport_error && !__atomic_exchange_n(&qdiag_seen, 1, __ATOMIC_RELAXED)) {
        const char *previous_file = __atomic_load_n(&qdiag_last_file, __ATOMIC_RELAXED);
        const char *previous_function = __atomic_load_n(&qdiag_last_function, __ATOMIC_RELAXED);
        Packet *p = packet;
        char text[1200];
        int n = snprintf(
                text, sizeof(text),
                "QG3 DIAG kind=last-access-before-first-observation previous=%s:%u:%s previous-thread=%d current=%s:%u:%s guest-thread=%d packet=%p op=%u seq=%u state=%u fault=%u out=%u result=0x%llx args=0x%llx,0x%llx,0x%llx,0x%llx\n",
                previous_file ? previous_file : "none",
                __atomic_load_n(&qdiag_last_line, __ATOMIC_RELAXED),
                previous_function ? previous_function : "none",
                __atomic_load_n(&qdiag_last_thread, __ATOMIC_RELAXED), file, line, function, thread,
                (void *)p, p ? p->opcode : 0, p ? p->seq : 0, p ? p->state : 0, p ? p->fault : 0,
                p ? p->out_len : 0, p ? p->result : 0, p ? p->args[0] : 0, p ? p->args[1] : 0,
                p ? p->args[2] : 0, p ? p->args[3] : 0);

        if (n > 0) {
            write(2, text, (usize)n < sizeof(text) ? (usize)n : sizeof(text) - 1);
        }
    }

    __atomic_store_n(&qdiag_last_file, file, __ATOMIC_RELAXED);
    __atomic_store_n(&qdiag_last_line, line, __ATOMIC_RELAXED);
    __atomic_store_n(&qdiag_last_function, function, __ATOMIC_RELAXED);
    __atomic_store_n(&qdiag_last_thread, thread, __ATOMIC_RELAXED);
    *ep = saved;

    return &transport_error;
}

static void qdiag_egl_reply(u32 op, const u64 *args, unsigned count, u64 result) {
    if (op != 9 && op != 62 && op != 63 && op != 70) {
        return;
    }

    unsigned ordinal = __atomic_load_n(&qdiag_egl_count, __ATOMIC_RELAXED);

    while (ordinal < 64) {
        if (__atomic_compare_exchange_n(&qdiag_egl_count, &ordinal, ordinal + 1, 0,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            int *ep = __get_errno_ptr(), saved = *ep;
            char text[320];
            int n = snprintf(
                    text, sizeof(text),
                    "QG3 DIAG kind=egl-rpc-reply ordinal=%u guest-thread=%d op=%u result=0x%llx args=0x%llx,0x%llx,0x%llx,0x%llx\n",
                    ordinal + 1, qdiag_pthread_self(), op, result, count > 0 ? args[0] : 0,
                    count > 1 ? args[1] : 0, count > 2 ? args[2] : 0, count > 3 ? args[3] : 0);

            if (n > 0) {
                write(2, text, (usize)n < sizeof(text) ? (usize)n : sizeof(text) - 1);
            }

            *ep = saved;
            break;
        }
    }
}

/* Earlier qgfx_memory_release accesses remain outside this diagnostic macro.
 * Concurrent accesses can interleave metadata; the previous access is only a clue. */
#define transport_error (*qdiag_transport_access(__FILE__, __LINE__, __func__))
#define QDIAG_EGL_REPLY(op, args, count, result) qdiag_egl_reply(op, args, count, result)
#else
#define QDIAG_EGL_REPLY(op, args, count, result) ((void)0)
#endif

static int start_channel(void) {
#ifdef QGFX_WRAPPER_TEST
    static Packet fixture;
    packet = &fixture;

    return 1;
#else
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    if (packet) {
        return 1;
    }

    void *p = qsm_allocate(4096);

    if (!p) {
        return 0;
    }
#else
    if (packet) {
        return 1;
    }

    void *p = 0;
    int rc = posix_memalign(&p, 4096, 4096);

    if (rc || !p) {
        return 0;
    }
#endif
    i64 physical = -1;
    usize contiguous = 0;
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    if (mem_offset(p, -1, 4096, &physical, &contiguous) || contiguous < 4096 ||
        physical < 0x40000000LL || physical >= 0x31c100000LL || (physical & 4095)) {
#else
    if (mlock(p, 4096) || mem_offset(p, -1, 4096, &physical, &contiguous) || contiguous < 4096 ||
        physical < 0x40000000LL || physical >= 0x240000000LL || (physical & 4095)) {
#endif
        char text[128];
        snprintf(text, sizeof(text), "QG3 REJECT mapping errno=%d span=%lu\n", *__get_errno_ptr(),
                 contiguous);
        emit(text);
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        qgfx_memory_release(p, 4096);
#else
        munlock(p, 4096);
        free(p);
#endif

        return 0;
    }

    unsigned char *b = p;

    for (usize i = 0; i < 4096; i++) {
        b[i] = 0;
    }

    if (!channel_identity(p)) {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        qgfx_memory_release(p, 4096);
#else
        munlock(p, 4096);
        free(p);
#endif
        emit("QG3 REJECT incarnation clock\n");

        return 0;
    }

    packet = p;
    channel_physical = (u64)physical;
    packet->magic = 0x58464751;
    packet->version = 3;
    char owner[128];
    snprintf(owner, sizeof(owner), "QG3 OWNER pid=%d paddr=0x%llx\n", getpid(), physical);
    emit(owner);
    char text[128];
    snprintf(text, sizeof(text), "QG3 READY paddr=0x%llx length=4096 pid=%d incarnation=0x%llx\n",
             physical, getpid(), packet->reserved[9]);
    emit(text);

    return 1;
#endif
}

static void acquire(void) {
    api_fair_acquire(&api_lock);
}

static void release(void) {
    api_fair_release(&api_lock);
}

static u64 call_raw(u32 op, const u64 *args, unsigned n, const void *input, unsigned bytes) {
    if (channel_closed || !start_channel() || n > 16 || bytes > 3840) {
        transport_error = 1;

        return 0;
    }

#ifdef QGFX_WRAPPER_TEST
    extern u64 test_remote(Packet *, u32, const u64 *, unsigned, const void *, unsigned);
    u64 result = test_remote(packet, op, args, n, input, bytes);
    QDIAG_EGL_REPLY(op, args, n, result);

    return result;
#else
    packet->opcode = op;
    packet->seq = ++sequence;
    packet->in_len = bytes;
    packet->out_len = 0;
    packet->fault = 0;
    packet->result = 0;

    for (unsigned i = 0; i < 16; i++) {
        packet->args[i] = i < n ? args[i] : 0;
    }

    if (bytes) {
        copy(packet->data, input, bytes);
    }

    __atomic_store_n(&packet->state, 1, __ATOMIC_RELEASE);
    unsigned waits = 0;

    while (__atomic_load_n(&packet->state, __ATOMIC_ACQUIRE) != 2 && waits++ < 10000) {
        usleep(1000);
    }

    if (packet->state != 2 || packet->seq != sequence || packet->fault || packet->out_len > 3840) {
        transport_error = 1;

        return 0;
    }

    QDIAG_EGL_REPLY(op, args, n, packet->result);

    return packet->result;
#endif
}

#include "observer_rpc.c"

#include "gl_batch.c"
/* Default-priority export destructors may still issue RPCs; retire last. */
#ifndef QGFX_WRAPPER_TEST
__attribute__((destructor(101)))
#endif

static void close_channel(void) {
    acquire();

    if (!packet || channel_closed) {
        release();

        return;
    }

    if (!flush_gl_batch()) {
        emit("QG3 CLOSE fail=flush\n"); /* Retirement must not depend on flush success. */
    }

    channel_closed = 1;
    packet->reserved[0] =
            0x3145534f4c434751ULL; /* QGCLOSE1: optional v3 retirement acknowledgement. */
    __atomic_store_n(&packet->state, 3, __ATOMIC_RELEASE);
    QgfxCloseTime start = {0}, now = {0};
    unsigned waits = 0;

    if (!qgfx_close_clock(2, &start)) {
        const u64 begin = start.seconds * 1000000000ULL + start.nanoseconds;

        while (__atomic_load_n(&packet->state, __ATOMIC_ACQUIRE) != 4 && waits++ < 2000) {
            if (qgfx_close_clock(2, &now) ||
                now.seconds * 1000000000ULL + now.nanoseconds - begin >= 2000000000ULL) {
                break;
            }

            usleep(1000);
        }
    }

    char text[160];
    snprintf(text, sizeof(text), "QG3 CLOSE pid=%d paddr=0x%llx acknowledged=%u\n", getpid(),
             channel_physical, packet->state == 4);
    emit(text);
    release();
}

#include "uniform_prefetch.c"

static u64 fbits(float f) {
    union {
        float f;
        u32 i;
    } v = {.f = f};

    return v.i;
}

#include "bulk.c"
#include "texture_upload.c"

#include "input_wake.h"

static void unsupported(const char *name) {
    emit("QG3 UNSUPPORTED ");
    emit(name);
    emit("\n");
    transport_error = 1;
}

typedef struct {
    const void *pointer;
    unsigned size, type, normalized, stride, enabled, buffer;
} ClientArray;
static ClientArray client_arrays[64];
static unsigned client_array_buffer, client_element_buffer;
static unsigned unpack_alignment = 4, unpack_row_length, unpack_skip_rows, unpack_skip_pixels,
                unpack_buffer;

static usize pixel_extent(int width, int height, unsigned format, unsigned type) {
    if (width <= 0 || height <= 0 || unpack_buffer) {
        return 0;
    }

    usize bpp = 0;

    if (type == 0x8033 || type == 0x8034 || type == 0x8363) {
        bpp = 2;
    }
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    else if (type == 0x1401) {
        switch (format) {
        case 0x1908:
        case 0x80e1:
            bpp = 4;
            break;
        case 0x1907:
            bpp = 3;
            break;
        case 0x1906:
        case 0x1909:
        case 0x1903:
            bpp = 1;
            break;
        case 0x190a:
        case 0x8227:
            bpp = 2;
            break;
        default:
            break;
        }
    }
#else
    else if (type == 0x1401) {
        switch (format) {
        case 0x1908:
            bpp = 4;
            break;
        case 0x1907:
            bpp = 3;
            break;
        case 0x1906:
        case 0x1909:
        case 0x1903:
            bpp = 1;
            break;
        case 0x190a:
        case 0x8227:
            bpp = 2;
            break;
        default:
            break;
        }
    }
#endif

    if (!bpp) {
        return 0;
    }

    u64 row = unpack_row_length ? unpack_row_length : (unsigned)width;
    u64 stride = (row * bpp + unpack_alignment - 1) & ~(u64)(unpack_alignment - 1);
    u64 rows = unpack_skip_rows + (u64)height - 1;
    u64 last = (unpack_skip_pixels + (u64)width) * bpp;

    if (last > (u64)MAX_BULK || rows > ((u64)MAX_BULK - last) / stride) {
        return 0;
    }

    return (usize)(rows * stride + last);
}

#define SCALAR(name, ret, op, params, ...)                                                         \
    ret name params {                                                                              \
        acquire();                                                                                 \
        u64 args[] = {__VA_ARGS__};                                                                \
        if (op == 4 || op == 9 || op == 10)                                                        \
            uniform_clear();                                                                       \
        u64 r = call(op, args, sizeof(args) / 8, 0, 0);                                            \
        if (op == 4 && r)                                                                          \
            texture_upload_dispose();                                                              \
        release();                                                                                 \
        return (ret)r;                                                                             \
    }
#define VOIDCALL(name, op, params, ...)                                                            \
    void name params {                                                                             \
        acquire();                                                                                 \
        u64 args[] = {__VA_ARGS__};                                                                \
        if (op == 31 || op == 37)                                                                  \
            uniform_forget((u32)args[0]);                                                          \
        (void)send_void(op, args, sizeof(args) / 8, 0, 0);                                         \
        release();                                                                                 \
    }
SCALAR(eglGetDisplay, EGLDisplay, 1, (void *native), (u64)native)

EGLBoolean eglInitialize(EGLDisplay d, EGLint *major, EGLint *minor) {
    acquire();
    u64 a[] = {(u64)d};
    u64 r = call(2, a, 1, 0, 0);

    if (r && packet->out_len == 8) {
        if (major) {
            copy(major, packet->data, 4);
        }

        if (minor) {
            copy(minor, packet->data + 4, 4);
        }
    }

    release();

    return r;
}

EGLint eglGetError(void) {
    if (transport_error) {
        transport_error = 0;

        return 0x3002;
    }

    acquire();
    u64 r = call(3, 0, 0, 0, 0);
    release();

    return r;
}

SCALAR(eglTerminate, EGLBoolean, 4, (EGLDisplay d), (u64)d)
SCALAR(eglBindAPI, EGLBoolean, 5, (unsigned api), (u64)api)

static unsigned attrs_size(const EGLint *a) {
    if (!a) {
        return 0;
    }

    for (unsigned i = 0; i < 128; i += 2) {
        if (a[i] == 0x3038) {
            return (i + 1) * 4;
        }
    }

    return 0xffffffff;
}

EGLBoolean eglChooseConfig(EGLDisplay d, const EGLint *attrs, EGLConfig *configs, EGLint cap,
                           EGLint *count) {
    if (cap < 0 || cap > 256 || !count) {
        return 0;
    }

    acquire();
    u64 a[] = {(u64)d, (u64)cap};
    u64 r = call(6, a, 2, attrs, attrs_size(attrs));

    if (r && packet->out_len >= 4) {
        copy(count, packet->data, 4);
        int n = *count < cap ? *count : cap;

        if (configs && n > 0 && packet->out_len >= (u32)(4 + 8 * n)) {
            copy((void *)configs, packet->data + 4, (usize)n * 8);
        }
    }

    release();

    return r;
}

EGLContext eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attrs) {
    acquire();
    u64 a[] = {(u64)d, (u64)c, (u64)share};
    u64 r = call(7, a, 3, attrs, attrs_size(attrs));
    release();

    return (void *)r;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay d, EGLConfig c, const EGLint *attrs) {
    acquire();
    u64 a[] = {(u64)d, (u64)c};
    u64 r = call(8, a, 2, attrs, attrs_size(attrs));
    release();

    return (void *)r;
}

SCALAR(eglMakeCurrent, EGLBoolean, 9,
       (EGLDisplay d, EGLSurface draw, EGLSurface read, EGLContext ctx), (u64)d, (u64)draw,
       (u64)read, (u64)ctx)
SCALAR(eglDestroyContext, EGLBoolean, 10, (EGLDisplay d, EGLContext ctx), (u64)d, (u64)ctx)
EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface s);
static char *strings[8];

static const char *query_string(unsigned op, u64 display, unsigned key) {
    unsigned slot = key % 8;
    const char *out = 0;

    if (!strings[slot]) {
        strings[slot] = (char *)malloc(65536);
    }

    if (!strings[slot]) {
        transport_error = 1;

        return 0;
    }

    u64 blob = download_begin(65536);

    if (!blob) {
        bulk_free();
        transport_error = 1;

        return 0;
    }

    u64 a[] = {display, key, blob};
    u64 r = call(op, op == 12 ? a : a + 1, op == 12 ? 3 : 2, 0, 0);

    if (r && packet && packet->out_len) {
        emit("QG3 FAIL code=HOST_STRING_BULK_UNSUPPORTED\n");
        transport_error = 1;
    }

    if (r && r <= 65536 && !transport_error && packet && packet->out_len == 0 && bulk_memory &&
        r <= bulk_size) {
        download_end(strings[slot], r);

        if (!transport_error && strings[slot][r - 1] == 0) {
            out = strings[slot];
        } else {
            transport_error = 1;
        }
    } else {
        bulk_free();

        if (r) {
            transport_error = 1;
        }
    }

    drop_blob(blob);

    return transport_error ? 0 : out;
}

const char *eglQueryString(EGLDisplay d, EGLint key) {
    acquire();
    const char *out = query_string(12, (u64)d, (unsigned)key);
    release();

    return out;
}

GLenum glGetError(void) {
    if (transport_error) {
        transport_error = 0;

        return 0x502;
    }

    acquire();
    u64 r = call(20, 0, 0, 0, 0);
    release();

    return r;
}

const unsigned char *glGetString(GLenum key) {
    acquire();
    const char *out = query_string(21, 0, key);
    release();

    return (const unsigned char *)out;
}

VOIDCALL(glClearColor, 22, (GLfloat r, GLfloat g, GLfloat b, GLfloat a), fbits(r), fbits(g),
         fbits(b), fbits(a))
VOIDCALL(glClear, 23, (unsigned bits), (u64)bits)

void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type,
                  void *pixels) {
    if (w <= 0 || h <= 0 || (u64)w * h * 4 > (u64)MAX_BULK || format != 0x1908 || type != 0x1401 ||
        !pixels) {
        transport_error = 1;

        return;
    }

    acquire();
    usize size = (usize)w * h * 4;
    u64 blob = size > 3840 ? download_begin(size) : 0;
    u64 a[] = {(u64)(i64)x, (u64)(i64)y, w, h, format, type, blob};
    u64 r = call(24, a, 7, 0, 0);

    if (blob) {
        if (r == size) {
            download_end(pixels, size);
        } else {
            transport_error = 1;
            bulk_free();
        }

        drop_blob(blob);
    } else if (packet && packet->out_len == size) {
        copy(pixels, packet->data, size);
    } else {
        transport_error = 1;
    }

    release();
}

SCALAR(glCreateShader, GLuint, 25, (GLenum type), (u64)type)

static usize shader_length(const char *s) {
    usize n = 0;

    if (!s) {
        return MAX_BULK + 1;
    }

    while (n <= MAX_BULK && s[n]) {
        n++;
    }

    return n;
}

void glShaderSource(GLuint shader, GLsizei count, const char *const *text, const GLint *sizes) {
    if (count < 0 || count > 1024 || (count && !text)) {
        transport_error = 1;

        return;
    }

    usize total = 0;

    for (int i = 0; i < count; i++) {
        usize n = sizes && sizes[i] >= 0 ? (usize)sizes[i] : shader_length(text[i]);

        if (!text[i] || n > MAX_BULK - total) {
            transport_error = 1;

            return;
        }

        total += n;
    }

    unsigned char *input = total ? malloc(total) : 0;

    if (total && !input) {
        transport_error = 1;

        return;
    }

    usize offset = 0;

    for (int i = 0; i < count; i++) {
        usize n = sizes && sizes[i] >= 0 ? (usize)sizes[i] : shader_length(text[i]);
        copy(input + offset, text[i], n);
        offset += n;
    }

    acquire();
    u64 blob = total > 3840 ? upload(input, total) : 0;

    if (total > 3840 && !blob) {
        release();
        free(input);

        return;
    }

    u64 a[] = {shader, blob, total};
    (void)call(blob ? 353 : 26, a, 3, blob ? 0 : input, blob ? 0 : total);
    drop_blob(blob);
    release();
    free(input);
}

VOIDCALL(glCompileShader, 27, (GLuint shader), (u64)shader)

void glGetShaderiv(GLuint shader, GLenum key, GLint *value) {
    acquire();
    u64 a[] = {shader, key};
    (void)call(28, a, 2, 0, 0);

    if (value && packet && packet->out_len == 4) {
        copy(value, packet->data, 4);
    } else {
        transport_error = 1;
    }

    release();
}

GLuint glCreateProgram(void) {
    acquire();
    u64 r = call(29, 0, 0, 0, 0);
    release();

    return r;
}

VOIDCALL(glAttachShader, 30, (GLuint program, GLuint shader), (u64)program, (u64)shader)
VOIDCALL(glLinkProgram, 31, (GLuint program), (u64)program)

void glGetProgramiv(GLuint program, GLenum key, GLint *value) {
    acquire();

    if (key == 0x8b82 && uniform_enabled()) {
        uniform_program_info(program, value);
        release();

        return;
    }

    u64 a[] = {program, key};
    (void)call(32, a, 2, 0, 0);

    if (value && packet && packet->out_len == 4) {
        copy(value, packet->data, 4);
    } else {
        transport_error = 1;
    }

    release();
}

VOIDCALL(glUseProgram, 33, (GLuint program), (u64)program)
VOIDCALL(glViewport, 34, (GLint x, GLint y, GLsizei w, GLsizei h), (u64)(i64)x, (u64)(i64)y, w, h)
static int prepare_client_arrays(usize vertices);

void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    if (first < 0 || count < 0) {
        transport_error = 1;

        return;
    }

    acquire();

    if (prepare_client_arrays((usize)first + count)) {
        u64 a[] = {mode, first, count};
        (void)send_void(35, a, 3, 0, 0);
    }

    release();
}

VOIDCALL(glDeleteShader, 36, (GLuint shader), (u64)shader)
VOIDCALL(glDeleteProgram, 37, (GLuint program), (u64)program)
#ifdef QGFX_SELFTEST
__attribute__((constructor)) static void test(void) {
    QGFX_HELPER_DISPATCH();
    emit("QG3 TEST BEGIN\n");
    EGLDisplay d = eglGetDisplay(0);
    int major = 0, minor = 0;

    if (!d || !eglInitialize(d, &major, &minor)) {
        emit("QG3 FAIL initialize\n");

        return;
    }

    eglBindAPI(0x30a0);
    int attrs[] = {0x3033, 1, 0x3040, 0x40, 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3038};
    EGLConfig config = 0;
    int count = 0;

    if (!eglChooseConfig(d, attrs, &config, 1, &count) || !count) {
        emit("QG3 FAIL config\n");

        return;
    }

    int context_attrs[] = {0x3098, 3, 0x3038};
    EGLContext ctx = eglCreateContext(d, config, 0, context_attrs);
    int surface_attrs[] = {0x3057, 16, 0x3056, 16, 0x3038};
    EGLSurface surface = eglCreatePbufferSurface(d, config, surface_attrs);

    if (!ctx || !surface || !eglMakeCurrent(d, surface, surface, ctx)) {
        emit("QG3 FAIL context\n");

        return;
    }

    const char *vertex =
            "#version 300 es\nconst vec2 p[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3));void main(){gl_Position=vec4(p[gl_VertexID],0,1);}";
    const char *fragment =
            "#version 300 es\nprecision highp float;out vec4 color;void main(){color=vec4(0,1,0,1);}";
    GLuint vs = glCreateShader(0x8b31), fs = glCreateShader(0x8b30);
    int status = 0;
    glShaderSource(vs, 1, &vertex, 0);
    glCompileShader(vs);
    glGetShaderiv(vs, 0x8b81, &status);

    if (!status) {
        emit("QG3 FAIL vertex\n");

        return;
    }

    glShaderSource(fs, 1, &fragment, 0);
    glCompileShader(fs);
    glGetShaderiv(fs, 0x8b81, &status);

    if (!status) {
        emit("QG3 FAIL fragment\n");

        return;
    }

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glGetProgramiv(program, 0x8b82, &status);

    if (!status) {
        emit("QG3 FAIL link\n");

        return;
    }

    glUseProgram(program);
    glViewport(0, 0, 16, 16);
    glDrawArrays(4, 0, 3);
    unsigned char pixels[1024];

    for (unsigned i = 0; i < 1024; i++) {
        pixels[i] = 99;
    }

    glReadPixels(0, 0, 16, 16, 0x1908, 0x1401, pixels);
    unsigned error = glGetError();
    int passed = error == 0;

    for (unsigned i = 0; i < 1024; i++) {
        if (pixels[i] != (i % 4 == 1 || i % 4 == 3 ? 255 : 0)) {
            passed = 0;
        }
    }

    char line[160];
    snprintf(line, sizeof(line), "QG3 TEST readback=%d error=0x%x egl=%d.%d calls=%u\n", passed,
             error, major, minor, sequence);
    emit(line);
    glUseProgram(0);
    glDeleteProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    eglMakeCurrent(d, 0, 0, 0);
    eglDestroySurface(d, surface);
    eglDestroyContext(d, ctx);
    eglTerminate(d);
    close_channel();
    emit("QG3 TEST DONE\n");
}
#endif
