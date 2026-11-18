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
typedef struct {
    u32 magic, version, state, opcode, seq, in_len, out_len, fault;
    u64 args[16], result, tid, reserved[10];
    unsigned char data[3840];
} Packet;
_Static_assert(__builtin_offsetof(Packet, data) == 256 && sizeof(Packet) == 4096, "RPC layout");
static Packet *packet;
static unsigned char api_lock;
static u32 sequence;
static int transport_error;

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

static int start_channel(void) {
#ifdef QGFX_WRAPPER_TEST
    static Packet fixture;
    packet = &fixture;

    return 1;
#else
    if (packet) {
        return 1;
    }

    void *p = 0;
    int rc = posix_memalign(&p, 4096, 4096);

    if (rc || !p) {
        return 0;
    }

    unsigned char *b = p;

    for (usize i = 0; i < 4096; i++) {
        b[i] = 0;
    }

    i64 physical = -1;
    usize contiguous = 0;

    if (mlock(p, 4096) || mem_offset(p, -1, 4096, &physical, &contiguous) || contiguous < 4096 ||
        physical < 0x40000000LL || physical >= 0x240000000LL || (physical & 4095)) {
        char text[128];
        snprintf(text, sizeof(text), "QG3 FAIL mapping errno=%d span=%lu\n", *__get_errno_ptr(),
                 contiguous);
        emit(text);

        return 0;
    }

    packet = p;
    packet->magic = 0x58464751;
    packet->version = 3;
    char text[128];
    snprintf(text, sizeof(text), "QG3 READY paddr=0x%llx length=4096\n", physical);
    emit(text);

    return 1;
#endif
}

static void acquire(void) {
    while (__atomic_test_and_set(&api_lock, __ATOMIC_ACQUIRE)) {
        usleep(100);
    }
}

static void release(void) {
    __atomic_clear(&api_lock, __ATOMIC_RELEASE);
}

static u64 call_raw(u32 op, const u64 *args, unsigned n, const void *input, unsigned bytes) {
    if (!start_channel() || n > 16 || bytes > 3840) {
        transport_error = 1;

        return 0;
    }

#ifdef QGFX_WRAPPER_TEST
    extern u64 test_remote(Packet *, u32, const u64 *, unsigned, const void *, unsigned);

    return test_remote(packet, op, args, n, input, bytes);
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
        usleep(waits <= 8 ? 100 : 1000);
    }

    if (packet->state != 2 || packet->seq != sequence || packet->fault || packet->out_len > 3840) {
        transport_error = 1;

        return 0;
    }
    return packet->result;
#endif
}

#include "observer_rpc.c"
