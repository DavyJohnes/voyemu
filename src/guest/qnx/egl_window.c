/* Real EGL queries and a CPU presentation boundary using the OEM Screen API. */
extern int screen_get_window_property_iv(void *, int, int *);
extern int screen_set_window_property_iv(void *, int, const int *);
extern int screen_get_window_property_pv(void *, int, void **);
extern int screen_create_window_buffers(void *, int);
extern int screen_destroy_window_buffers(void *);
extern int screen_get_buffer_property_pv(void *, int, void **);
extern int screen_get_buffer_property_iv(void *, int, int *);
extern int screen_post_window(void *, void *, int, const int *, int);
extern void *dlopen(const char *, int);
extern void *dlsym(void *, const char *);
#ifdef QGFX_KANZI_UI
#include "cluster_ui.c"
#endif
/* The guest keeps its display library in the IFS, which it mounts at /proc/boot, while the OEM root
 * it mounts at / carries no libscreen.so.1; one hardcoded path therefore turns a resolvable symbol
 * into a refusal (fail=real-symbol with errno 2). The compile-time path stays first and the IFS
 * copy is the fallback, and the candidate that answered is printed, so the loader's choice is read
 * from the guest instead of assumed. */
static const char *screen_library_paths[] = {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        "/lib64/libscreen.so.1",
#endif
        "/proc/boot/lib64/libscreen.so.1", 0};

int screen_create_window_buffers(void *window, int count) {
    static int (*native_create)(void *, int);

    if (!native_create) {
        for (unsigned at = 0; screen_library_paths[at] && !native_create; ++at) {
            char note[160];
            void *library = dlopen(screen_library_paths[at], 2);

            if (!library) {
                snprintf(note, sizeof(note), "QG3 SCREEN open-failed path=%s errno=%d\n",
                         screen_library_paths[at], *__get_errno_ptr());
                emit(note);
                continue;
            }

            void *symbol = dlsym(library, "screen_create_window_buffers");

            if (!symbol || symbol == (void *)screen_create_window_buffers) {
                snprintf(note, sizeof(note), "QG3 SCREEN self-symbol path=%s\n",
                         screen_library_paths[at]);
                emit(note);
                continue;
            }

            native_create = (int (*)(void *, int))symbol;
            snprintf(note, sizeof(note), "QG3 SCREEN library=%s\n", screen_library_paths[at]);
            emit(note);
        }
    }

    if (!native_create || native_create == screen_create_window_buffers) {
        emit("QG3 SCREEN fail=real-symbol\n");

        return -1;
    }

    int original = -1, usage = 6;
    int get_rc = screen_get_window_property_iv(window, 0x30, &original),
        get_error = *__get_errno_ptr();
    int set_rc = screen_set_window_property_iv(window, 0x30, &usage),
        set_error = *__get_errno_ptr();
    char detail[192];
    snprintf(detail, sizeof(detail),
             "QG3 SCREEN usage get-rc=%d get-errno=%d old=0x%x set-rc=%d set-errno=%d\n", get_rc,
             get_error, original, set_rc, set_error);
    emit(detail);

    if (set_rc) {
        return -1;
    }

    int rc = native_create(window, count);
    char text[160];
    snprintf(text, sizeof(text),
             "QG3 SCREEN buffers=%d original-usage=0x%x cpu-usage=6 rc=%d errno=%d\n", count,
             original, rc, *__get_errno_ptr());
    emit(text);

    return rc;
}

static int first_render_buffer(void *window, void **buffer) {
    int count = 0;

    *buffer = 0;

    if (screen_get_window_property_iv(window, 53, &count) || count < 1 || count > 16) {
        return -1;
    }

    void *buffers[16] = {0};

    if (screen_get_window_property_pv(window, 0x25, buffers) || !buffers[0]) {
        return -1;
    }

    *buffer = buffers[0];

    return 0;
}

#include "cached_readback.c"

/* QGFX_PRESENT_EXPORT can relay the real presented GL frame
 * (BGRA, already posted to the Screen window) as one ordered Screen-stream BMP
 * artifact per exported present. This is the honest read-back: pixels that were
 * actually post()-ed, no fabricated scene content. Header/env/sequence logic is
 * unit-tested in present_export.h logic via present_export.test.c. */
static char *qgfx_present_getenv(const char *name) {
    extern char *getenv(const char *);

    return getenv(name);
}

#include "present_export.h"
static unsigned present_export_checked, present_export_active;

static int present_export_on(void) {
#ifdef QGFX_QINGGAN09
    /* The A9 Screen carrier owns the composed frame; individual windows must not claim it. */
    const int default_enabled = 0;
#else
    const int default_enabled = 1;
#endif

    if (!present_export_checked) {
        present_export_checked = 1;
        present_export_active = present_export_enabled_value(
                present_export_getenv("QGFX_PRESENT_EXPORT"), default_enabled);
    }

    return present_export_active;
}

/* Relay one snapshotted presented frame. Serialises through the channel lock
 * (upload/call/drop take it internally); a[2]==0 on the wire keeps the present
 * exporter out of the carrier's strict screen-sequence namespace. A diagnostic
 * failure inside the export must not poison the OEM transport: transport_error
 * is saved first and restored only after every cleanup call, so cleanup
 * failures surface only their own relay failure (return 0) and OEM paths stay
 * exactly as clean as they arrived. Registration hygiene is retained on
 * every failure path: a nonzero op40 registration is dropped by op42 with the
 * SAME id; op42 never sees id 0. Non-Passion builds keep the original
 * upload/drop relay untouched (their scratch is ordinary malloc). */
static int export_present_frame(unsigned char *bmp) {
    if (!present_export_on() || !bmp) {
        return 0;
    }

    /* transport_error is the qdiag accessor macro yielding the slot pointer. */
    int saved_error = transport_error;
#ifdef QGFX_PASSION_ROOT_IMAGEFS
    /* Single-copy relay: the scratch was already filled by
     * present_export_snapshot; upload()'s second full-extent copy is gone.
     * Register the scratch mapping itself through the same op40/op41
     * descriptor walk upload() uses, then relay op354 (a[2]==0, outside the
     * carrier's strict sequence namespace) and drop the registration. */
    acquire();
    usize size = 54 + (usize)7680 * 720;
    u64 ra[] = {size, 0};
    u64 id = call(40, ra, 2, 0, 0);

    if (id) {
        usize offset = 0;

        while (offset < size) {
            i64 physical = -1;
            usize span = 0;

            if (mem_offset(bmp + offset, -1, size - offset, &physical, &span) || !span ||
                span > size - offset || physical < 0x40000000LL ||
                (u64)physical + span > 0x31c100000ULL) {
                break;
            }

            u64 descriptors[2] = {(u64)physical, (u64)span};
            u64 da[] = {id, 1};

            if (!call(41, da, 2, descriptors, 16)) {
                break;
            }

            offset += span;
        }

        if (offset < size) {
            /* registration failure: drop the nonzero registration with its own
             * id; never zero. Cleanup result cannot poison the OEM slot. */
            u64 drop[] = {id};
            (void)call(42, drop, 1, 0, 0);
            transport_error = saved_error;
            release();

            return 0;
        }
    }

    if (!id) {
        transport_error = saved_error;
        release();

        return 0;
    }

    u64 args[] = {(u64)id, 54 + 7680u * 720u, 0};
    (void)call(354, args, 3, 0, 0);
    /* restoration order: cleanup first, THEN restore the saved OEM error, so
     * even a failed op42 cannot poison the OEM transport state. */
    u64 da2[] = {id};
    (void)call(42, da2, 1, 0, 0);
    transport_error = saved_error;
    release();

    return 1;
#else
    /* Non-Passion build: exactly the original upload/drop relay path. */
    (void)bmp;
    acquire();
    u64 id = upload(bmp, 54 + 7680 * 720);

    if (!id) {
        transport_error = saved_error;
        release();

        return 0;
    }

    u64 args[] = {(u64)id, 54 + 7680u * 720u, 0};
    (void)call(354, args, 3, 0, 0);
    transport_error = saved_error;
    drop_blob(id);
    release();

    return 1;
#endif
}

#include "screen_pixels.c"

#include "present_logging.h"
typedef struct {
    void *remote, *window;
    int width, height;
    ReadbackCache readback;
    PresentLogState present_log;
} WindowSurface;
static WindowSurface window_surfaces[16];

EGLBoolean eglGetConfigs(EGLDisplay d, EGLConfig *configs, EGLint cap, EGLint *count) {
    if (cap < 0 || cap > 256 || !count) {
        return 0;
    }

    acquire();
    u64 a[] = {(u64)d, (u64)cap};
    u64 r = call(60, a, 2, 0, 0);

    if (r && packet->out_len >= 4) {
        copy(count, packet->data, 4);
        int n = *count < cap ? *count : cap;

        if (configs && n > 0 && packet->out_len >= (u32)(4 + n * 8)) {
            copy((void *)configs, packet->data + 4, (usize)n * 8);
        }
    }

    release();

    return r;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay d, EGLConfig cfg, EGLint attr, EGLint *value) {
    acquire();
    u64 a[] = {(u64)d, (u64)cfg, attr};
    u64 r = call(61, a, 3, 0, 0);

    if (r && value && packet->out_len == 4) {
        copy(value, packet->data, 4);
    }

    release();

    return r;
}

EGLContext eglGetCurrentContext(void) {
    acquire();
    u64 r = call(62, 0, 0, 0, 0);
    release();

    return (void *)r;
}

EGLDisplay eglGetCurrentDisplay(void) {
    acquire();
    u64 r = call(63, 0, 0, 0, 0);
    release();

    return (void *)r;
}

#ifdef QGFX_PASSION_ROOT_IMAGEFS
EGLSurface eglGetCurrentSurface(EGLint which) {
    acquire();
    u64 a[] = {(u64)(i64)which};
    u64 r = call(70, a, 1, 0, 0);
    release();

    return (void *)r;
}
#endif

unsigned eglQueryAPI(void) {
    acquire();
    u64 r = call(64, 0, 0, 0, 0);
    release();

    return r;
}

EGLBoolean eglQueryContext(EGLDisplay d, EGLContext ctx, EGLint attr, EGLint *value) {
    acquire();
    u64 a[] = {(u64)d, (u64)ctx, attr};
    u64 r = call(65, a, 3, 0, 0);

    if (r && value && packet->out_len == 4) {
        copy(value, packet->data, 4);
    }

    release();

    return r;
}

EGLBoolean eglQuerySurface(EGLDisplay d, EGLSurface surf, EGLint attr, EGLint *value) {
    acquire();
    u64 a[] = {(u64)d, (u64)surf, attr};
    u64 r = call(66, a, 3, 0, 0);

    if (r && value && packet->out_len == 4) {
        copy(value, packet->data, 4);
    }

    release();

    return r;
}

SCALAR(eglSurfaceAttrib, EGLBoolean, 67, (EGLDisplay d, EGLSurface surf, EGLint attr, EGLint value),
       (u64)d, (u64)surf, (u64)attr, (u64)value)
SCALAR(eglSwapInterval, EGLBoolean, 68, (EGLDisplay d, EGLint interval), (u64)d, (u64)interval)

EGLSurface eglCreateWindowSurface(EGLDisplay d, EGLConfig cfg, void *window, const EGLint *attrs) {
    (void)attrs;
    int size[2] = {0, 0}, usage = 6, format = 8;

    if (!window || screen_get_window_property_iv(window, 5, size) || size[0] <= 0 || size[1] <= 0 ||
        size[0] > 4096 || size[1] > 2160) {
        transport_error = 1;

        return 0;
    }

    /* Replace the buffers at the backend ownership boundary before first use. */
    void *buffer = 0;

    if (!first_render_buffer(window, &buffer) && buffer) {
        screen_destroy_window_buffers(window);
    }

    if (screen_set_window_property_iv(window, 0x30, &usage) ||
        screen_set_window_property_iv(window, 0xe, &format) ||
        screen_create_window_buffers(window, 2)) {
        transport_error = 1;

        return 0;
    }

    int pbuffer[] = {0x3057, size[0], 0x3056, size[1], 0x3038};
    void *surface = eglCreatePbufferSurface(d, cfg, pbuffer);

    if (!surface) {
        return 0;
    }

    for (unsigned i = 0; i < 16; i++) {
        if (!window_surfaces[i].remote) {
            window_surfaces[i] = (WindowSurface){
                    .remote = surface, .window = window, .width = size[0], .height = size[1]};

            return surface;
        }
    }

    eglDestroySurface(d, surface);
    transport_error = 1;

    return 0;
}

#include "ign_observe.c"
#include "rpc_refresh.c"
#include "theme_relay.c"

#include "mcd_trace.c"

#include "dlt_tail.c"
#include "mcu_input_relay.c"
#ifdef QGFX_QINGGAN11_MCU
#include "native_mcu_qinggan11.c"
#else
#include "native_mcu_copy.c"
#endif
#include "wheel_menu_relay.c"

#include "mcu_fifo_setup.c"
#include "theme_worker.c"

#include "language_relay.c"
#include "manager_observe.c"
#include "mcd_observer_worker.c"

EGLBoolean eglSwapBuffers(EGLDisplay d, EGLSurface surface) {
    start_theme_worker();
    start_language_worker();
    refresh_native_mcu();
    observe_ign();
    WindowSurface *w = 0;

    for (unsigned i = 0; i < 16; i++) {
        if (window_surfaces[i].remote == surface) {
            w = &window_surfaces[i];
        }
    }

    acquire();
    u64 a[] = {(u64)d, (u64)surface};
    u64 ok = call(69, a, 2, 0, 0);
    int trace = ok && w ? present_trace_enabled(&w->present_log) : 0;
    release();

    if (!ok || !w) {
        return ok;
    }

    if (trace) {
        emit("QG3 PRESENT begin-buffer level=DEBUG\n");
    }

    void *buffer = 0, *pixels = 0;
    int stride = 0;

    if (first_render_buffer(w->window, &buffer) || !buffer ||
        screen_get_buffer_property_pv(buffer, 0x22, &pixels) || !pixels ||
        screen_get_buffer_property_iv(buffer, 0x2c, &stride) || stride < w->width * 4) {
        emit("QG3 PRESENT fail=buffer-property level=ERROR\n");
        transport_error = 1;

        return 0;
    }

    if (trace) {
        emit("QG3 PRESENT buffer-ready level=DEBUG\n");
    }

    acquire();
    int copied = cache_read(&w->readback, w->width, w->height);
    release();

    if (!copied) {
        return 0;
    }

    void *temporary = w->readback.memory;

    /* OEM Screen's 32-bit software path uses BGRA bytes, with alpha at byte3. */
    if (!screen_copy_rgba(pixels, stride, temporary, w->width, w->height)) {
        transport_error = 1;

        return 0;
    }

    /* Snapshot the presented pixels while the client still owns the buffer,
     * before screen_post_window hands it back to the compositor.
     * Lazy one-shot scratch: NOTHING is reserved before the first post-
     * create_buffers swap (that static 5 529 774 B BSS held from dlopen was
     * the 3/3 ENOMEM(12) correlate at screen_create_window_buffers#2); the
     * first exporting swap allocates the scratch once through the qsm anon
     * pool and every later frame reuses the same mapping. If allocation
     * fails, THIS frame simply is not exported; present is untouched. */
    static unsigned char *present_export_bmp;
    static int present_export_staged;

    if (!present_export_bmp && present_export_on()) {
#ifdef QGFX_PASSION_ROOT_IMAGEFS
        present_export_bmp = (unsigned char *)qsm_allocate(54 + (usize)7680 * 720);
#else
        extern void *malloc(usize);
        present_export_bmp = (unsigned char *)malloc(54 + (usize)7680 * 720);
#endif
    }

    present_export_staged = present_export_bmp && present_export_on() &&
                            present_export_snapshot(present_export_bmp, pixels, w->width, w->height,
                                                    stride);

    if (trace) {
        emit("QG3 PRESENT before-post level=DEBUG\n");
    }

    int rect[] = {0, 0, w->width, w->height};
    int posted = screen_post_window(w->window, buffer, 1, rect, 0);
    const char *level = present_summary_level(&w->present_log, posted);

    if (level) {
        const char *suffix = level[0] == 'I'   ? ""
                             : level[0] == 'E' ? " level=ERROR"
                                               : " level=DEBUG";
        char line[128];
        snprintf(line, sizeof(line), "QG3 PRESENT size=%dx%d stride=%d post-rc=%d errno=%d%s\n",
                 w->width, w->height, stride, posted, *__get_errno_ptr(), suffix);
        emit(line);
    }

    if (posted) {
        transport_error = 1;

        return 0;
    }

    (void)(present_export_staged ? export_present_frame(present_export_bmp) : 0);
#ifdef QGFX_KANZI_UI
    kanzi_snapshot(packet, w->width, w->height);
#endif

    return 1;
}

EGLBoolean eglCopyBuffers(EGLDisplay d, EGLSurface s, void *pixmap) {
    (void)d;
    (void)s;
    (void)pixmap;
    unsupported("eglCopyBuffers");

    return 0;
}

EGLBoolean eglDestroySurface(EGLDisplay d, EGLSurface surface) {
    acquire();
    u64 args[] = {(u64)d, (u64)surface};
    u64 ok = call(11, args, 2, 0, 0);

    if (ok) {
        for (unsigned i = 0; i < 16; i++) {
            if (window_surfaces[i].remote == surface) {
                cache_dispose(&window_surfaces[i].readback);
                window_surfaces[i] = (WindowSurface){0};
            }
        }
    }

    release();

    return ok;
}
