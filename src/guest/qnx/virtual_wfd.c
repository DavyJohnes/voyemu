/* Virtual OpenWF device topology. Core prototypes/enums come from the pinned registry.
 * Active image handles are retained; pixel presentation is a separate unverified gate. */
#include <WF/wfd.h>
#include <stddef.h>

#define DEV 1u
#define PORT 1u
#define PIPE 1u
#define MODE 1u
#define FRAMEBUFFER_LAYER 1
#define WIDTH 1920
#define HEIGHT 720
extern long write(int, const void *, unsigned long);
extern int open(const char *, int, ...);
#ifndef VWFD_HOST_TEST
extern int slogf(int, int, const char *, ...);
#endif
static int logfd = 2;
static unsigned budget = 0;
static int live, port_live, pipe_live;
static WFDErrorCode error;
static WFDint power = WFD_POWER_MODE_ON;
static WFDPortMode selected_mode = MODE;
static WFDRect src_rect = {0, 0, WIDTH, HEIGHT}, dst_rect = {0, 0, WIDTH, HEIGHT};
static WFDSource pending, active;
static WFDEGLImage images[8];

static void log_call(const char *s) {
    if (budget++ >= 120) {
        return;
    }

    unsigned long n = 0;

    while (s[n]) {
        n++;
    }

    write(logfd, "VWFD ", 5);
    write(logfd, s, n);
    write(logfd, "\n", 1);
#ifndef VWFD_HOST_TEST
    slogf(0, 6, "VWFD %s", s);
#endif
}

static int filter_match(const WFDint *f, WFDint key, WFDint value) {
    if (!f) {
        return 1;
    }

    for (int i = 0; i < 16; i += 2) {
        if (f[i] == WFD_NONE) {
            return 1;
        }

        if (f[i] != key) {
            error = WFD_ERROR_BAD_ATTRIBUTE;

            return 0;
        }

        if (f[i + 1] != value) {
            return 0;
        }
    }

    error = WFD_ERROR_ILLEGAL_ARGUMENT;

    return 0;
}

__attribute__((constructor)) static void init(void) {
    int f = open("/dev/console", 1);

    if (f >= 0) {
        logfd = f;
    }

    log_call("LOAD");
}

static int device(WFDDevice d) {
    if (live && d == DEV) {
        return 1;
    }

    error = WFD_ERROR_BAD_DEVICE;

    return 0;
}

static int port(WFDDevice d, WFDPort p) {
    if (device(d) && p == PORT && port_live) {
        return 1;
    }

    error = WFD_ERROR_BAD_HANDLE;

    return 0;
}

static int pipeline(WFDDevice d, WFDPipeline p) {
    if (device(d) && p == PIPE && pipe_live) {
        return 1;
    }

    error = WFD_ERROR_BAD_HANDLE;

    return 0;
}

static void unsupported(void) {
    error = WFD_ERROR_NOT_SUPPORTED;
}

WFDint wfdGetStrings(WFDDevice d, WFDStringID id, const char **out, WFDint count) {
    (void)d;
    const char *value = 0;

    switch (id) {
    case WFD_VENDOR:
        value = "Voyemu";
        break;
    case WFD_RENDERER:
        value = "Virtual topology";
        break;
    case WFD_VERSION:
        value = "1.0";
        break;
    case WFD_EXTENSIONS:
        return 0;
    default:
        error = WFD_ERROR_BAD_ATTRIBUTE;

        return 0;
    }

    if (out && count > 0) {
        out[0] = value;
    }

    return 1;
}

WFDboolean wfdIsExtensionSupported(WFDDevice d, const char *s) {
    (void)d;
    (void)s;
    log_call("extension-none");

    return WFD_FALSE;
}

WFDErrorCode wfdGetError(WFDDevice d) {
    (void)d;
    WFDErrorCode e = error;
    error = WFD_ERROR_NONE;

    return e;
}

WFDint wfdEnumerateDevices(WFDint *ids, WFDint n, const WFDint *filter) {
    log_call("enumerate-devices");

    if (!filter_match(filter, WFD_DEVICE_FILTER_PORT_ID, PORT)) {
        return 0;
    }

    if (ids && n > 0) {
        ids[0] = DEV;
    }

    return 1;
}

WFDDevice wfdCreateDevice(WFDint id, const WFDint *attrs) {
    log_call("create-device");

    if ((id != 0 && id != DEV) || (attrs && attrs[0] != WFD_NONE)) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return 0;
    }

    live = 1;
    error = WFD_ERROR_NONE;

    return DEV;
}

WFDErrorCode wfdDestroyDevice(WFDDevice d) {
    if (!device(d)) {
        return error;
    }

    live = port_live = pipe_live = 0;
    pending = active = 0;

    for (int i = 0; i < 8; i++) {
        images[i] = 0;
    }

    return WFD_ERROR_NONE;
}

void wfdDeviceCommit(WFDDevice d, WFDCommitType type, WFDHandle handle) {
    log_call("commit");

    if (!device(d)) {
        return;
    }

    if (type != WFD_COMMIT_ENTIRE_DEVICE && type != WFD_COMMIT_ENTIRE_PORT &&
        type != WFD_COMMIT_PIPELINE) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return;
    }

    if ((type == WFD_COMMIT_ENTIRE_PORT && handle != PORT) ||
        (type == WFD_COMMIT_PIPELINE && handle != PIPE)) {
        error = WFD_ERROR_BAD_HANDLE;

        return;
    }

    /* Commit the staged topology and image reference; no fabricated framebuffer data. */
    active = pending;
}

WFDint wfdGetDeviceAttribi(WFDDevice d, WFDDeviceAttrib a) {
    if (!device(d)) {
        return 0;
    }

    if (a == WFD_DEVICE_ID) {
        return DEV;
    }

    error = WFD_ERROR_BAD_ATTRIBUTE;

    return 0;
}

void wfdSetDeviceAttribi(WFDDevice d, WFDDeviceAttrib a, WFDint v) {
    (void)d;
    (void)a;
    (void)v;
    error = WFD_ERROR_BAD_ATTRIBUTE;
}

WFDEvent wfdCreateEvent(WFDDevice d, const WFDint *a) {
    (void)a;

    return device(d) ? 1 : 0;
}

void wfdDestroyEvent(WFDDevice d, WFDEvent e) {
    (void)e;
    (void)device(d);
}

WFDint wfdGetEventAttribi(WFDDevice d, WFDEvent e, WFDEventAttrib a) {
    (void)e;

    if (!device(d)) {
        return 0;
    }

    if (a == WFD_EVENT_TYPE) {
        return WFD_EVENT_NONE;
    }

    error = WFD_ERROR_BAD_ATTRIBUTE;

    return 0;
}

void wfdDeviceEventAsync(WFDDevice d, WFDEvent e, WFDEGLDisplay display, WFDEGLSync sync) {
    (void)d;
    (void)e;
    (void)display;
    (void)sync;
    unsupported();
}

WFDEventType wfdDeviceEventWait(WFDDevice d, WFDEvent e, WFDtime t) {
    (void)d;
    (void)e;
    (void)t;

    return WFD_EVENT_NONE;
}

void wfdDeviceEventFilter(WFDDevice d, WFDEvent e, const WFDEventType *f) {
    (void)d;
    (void)e;
    (void)f;
}

WFDint wfdEnumeratePorts(WFDDevice d, WFDint *ids, WFDint n, const WFDint *filter) {
    log_call("enumerate-ports");

    if (!device(d) || !filter_match(filter, WFD_PORT_ID, PORT)) {
        return 0;
    }

    if (ids && n > 0) {
        ids[0] = PORT;
    }

    return 1;
}

WFDPort wfdCreatePort(WFDDevice d, WFDint id, const WFDint *attrs) {
    log_call("create-port");

    if (!device(d) || id != PORT || (attrs && attrs[0] != WFD_NONE)) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return 0;
    }

    port_live = 1;

    return PORT;
}

void wfdDestroyPort(WFDDevice d, WFDPort p) {
    if (port(d, p)) {
        port_live = 0;
    }
}

WFDint wfdGetPortModes(WFDDevice d, WFDPort p, WFDPortMode *m, WFDint n) {
    log_call("port-modes");

    if (!port(d, p)) {
        return 0;
    }

    if (m && n > 0) {
        m[0] = MODE;
    }

    return 1;
}

WFDint wfdGetPortModeAttribi(WFDDevice d, WFDPort p, WFDPortMode m, WFDPortModeAttrib a) {
    if (!port(d, p) || m != MODE) {
        error = WFD_ERROR_BAD_HANDLE;

        return 0;
    }

    switch (a) {
    case WFD_PORT_MODE_WIDTH:
        return WIDTH;
    case WFD_PORT_MODE_HEIGHT:
        return HEIGHT;
    case WFD_PORT_MODE_REFRESH_RATE:
        return 60;
    case WFD_PORT_MODE_FLIP_MIRROR_SUPPORT:
        return 0;
    case WFD_PORT_MODE_ROTATION_SUPPORT:
        return WFD_ROTATION_SUPPORT_NONE;
    case WFD_PORT_MODE_INTERLACED:
        return 0;
    default:
        error = WFD_ERROR_BAD_ATTRIBUTE;

        return 0;
    }
}

WFDfloat wfdGetPortModeAttribf(WFDDevice d, WFDPort p, WFDPortMode m, WFDPortModeAttrib a) {
    return (WFDfloat)wfdGetPortModeAttribi(d, p, m, a);
}

void wfdSetPortMode(WFDDevice d, WFDPort p, WFDPortMode m) {
    if (port(d, p)) {
        if (m != MODE) {
            error = WFD_ERROR_BAD_HANDLE;
        } else {
            selected_mode = m;
        }
    }
}

WFDPortMode wfdGetCurrentPortMode(WFDDevice d, WFDPort p) {
    return port(d, p) ? selected_mode : 0;
}

WFDint wfdGetPortAttribi(WFDDevice d, WFDPort p, WFDPortConfigAttrib a) {
    if (!port(d, p)) {
        return 0;
    }

    switch (a) {
    case WFD_PORT_ID:
        return PORT;
    case WFD_PORT_TYPE:
        return WFD_PORT_TYPE_INTERNAL;
    case WFD_PORT_DETACHABLE:
        return 0;
    case WFD_PORT_ATTACHED:
        return 1;
    case WFD_PORT_FILL_PORT_AREA:
        return 1;
    case WFD_PORT_FLIP:
    case WFD_PORT_MIRROR:
    case WFD_PORT_ROTATION:
        return 0;
    case WFD_PORT_POWER_MODE:
        return power;
    case WFD_PORT_PIPELINE_ID_COUNT:
        return 1;
    case WFD_PORT_PARTIAL_REFRESH_SUPPORT:
        return WFD_PARTIAL_REFRESH_NONE;
    case WFD_PORT_PARTIAL_REFRESH_ENABLE:
    case WFD_PORT_PROTECTION_ENABLE:
        return 0;
    default:
        error = WFD_ERROR_BAD_ATTRIBUTE;

        return 0;
    }
}

WFDfloat wfdGetPortAttribf(WFDDevice d, WFDPort p, WFDPortConfigAttrib a) {
    if (a == WFD_PORT_GAMMA) {
        return 1.0f;
    }

    return (WFDfloat)wfdGetPortAttribi(d, p, a);
}

void wfdGetPortAttribiv(WFDDevice d, WFDPort p, WFDPortConfigAttrib a, WFDint n, WFDint *out) {
    if (!port(d, p) || !out || n < 1) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return;
    }

    if (a == WFD_PORT_NATIVE_RESOLUTION) {
        if (n < 2) {
            error = WFD_ERROR_ILLEGAL_ARGUMENT;

            return;
        }

        out[0] = WIDTH;
        out[1] = HEIGHT;
    } else if (a == WFD_PORT_BINDABLE_PIPELINE_IDS) {
        out[0] = PIPE;
    } else if (a == WFD_PORT_BACKGROUND_COLOR) {
        if (n < 3) {
            error = WFD_ERROR_ILLEGAL_ARGUMENT;

            return;
        }

        out[0] = out[1] = out[2] = 0;
    } else {
        out[0] = wfdGetPortAttribi(d, p, a);

        for (int i = 1; i < n; i++) {
            out[i] = 0;
        }
    }
}

void wfdGetPortAttribfv(WFDDevice d, WFDPort p, WFDPortConfigAttrib a, WFDint n, WFDfloat *out) {
    if (!port(d, p) || !out || n < 1) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return;
    }

    if (a == WFD_PORT_PHYSICAL_SIZE || a == WFD_PORT_GAMMA_RANGE) {
        if (n < 2) {
            error = WFD_ERROR_ILLEGAL_ARGUMENT;

            return;
        }

        out[0] = out[1] = a == WFD_PORT_GAMMA_RANGE ? 1.0f : 0.16f;
    } else {
        out[0] = wfdGetPortAttribf(d, p, a);

        for (int i = 1; i < n; i++) {
            out[i] = 0;
        }
    }
}

void wfdSetPortAttribi(WFDDevice d, WFDPort p, WFDPortConfigAttrib a, WFDint v) {
    if (!port(d, p)) {
        return;
    }

    if (a == WFD_PORT_POWER_MODE) {
        power = v;
    } else if ((a == WFD_PORT_FLIP || a == WFD_PORT_MIRROR || a == WFD_PORT_ROTATION) && v == 0) {
    } else {
        error = WFD_ERROR_BAD_ATTRIBUTE;
    }
}

void wfdSetPortAttribf(WFDDevice d, WFDPort p, WFDPortConfigAttrib a, WFDfloat v) {
    if (port(d, p) && !(a == WFD_PORT_GAMMA && v == 1.0f)) {
        error = WFD_ERROR_BAD_ATTRIBUTE;
    }
}

void wfdSetPortAttribiv(WFDDevice d, WFDPort p, WFDPortConfigAttrib a, WFDint n, const WFDint *v) {
    if (port(d, p) && !(a == WFD_PORT_BACKGROUND_COLOR && n == 3 && v)) {
        unsupported();
    }
}

void wfdSetPortAttribfv(WFDDevice d, WFDPort p, WFDPortConfigAttrib a, WFDint n,
                        const WFDfloat *v) {
    (void)n;
    (void)v;

    if (port(d, p)) {
        unsupported();
    }
}

void wfdBindPipelineToPort(WFDDevice d, WFDPort p, WFDPipeline q) {
    if (port(d, p)) {
        (void)pipeline(d, q);
    }
}

WFDint wfdGetDisplayDataFormats(WFDDevice d, WFDPort p, WFDDisplayDataFormat *f, WFDint n) {
    (void)f;
    (void)n;

    if (!port(d, p)) {
        return 0;
    }

    return 0;
}

WFDint wfdGetDisplayData(WFDDevice d, WFDPort p, WFDDisplayDataFormat f, WFDuint8 *data, WFDint n) {
    (void)f;
    (void)data;
    (void)n;

    if (port(d, p)) {
        unsupported();
    }

    return 0;
}

WFDint wfdEnumeratePipelines(WFDDevice d, WFDint *ids, WFDint n, const WFDint *f) {
    log_call("enumerate-pipelines");

    if (!device(d) || !filter_match(f, WFD_PIPELINE_PORTID, PORT)) {
        return 0;
    }

    if (ids && n > 0) {
        ids[0] = PIPE;
    }

    return 1;
}

WFDPipeline wfdCreatePipeline(WFDDevice d, WFDint id, const WFDint *attrs) {
    log_call("create-pipeline");

    if (!device(d) || id != PIPE || (attrs && attrs[0] != WFD_NONE)) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return 0;
    }

    pipe_live = 1;

    return PIPE;
}

void wfdDestroyPipeline(WFDDevice d, WFDPipeline p) {
    if (pipeline(d, p)) {
        pipe_live = 0;
        pending = active = 0;
    }
}

WFDSource wfdCreateSourceFromImage(WFDDevice d, WFDPipeline p, WFDEGLImage image,
                                   const WFDint *attrs) {
    log_call("create-source");

    if (!pipeline(d, p) || !image || (attrs && attrs[0] != WFD_NONE)) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return 0;
    }

    for (int i = 0; i < 8; i++) {
        if (!images[i]) {
            images[i] = image;

            return (WFDSource)(0x100 + i);
        }
    }

    error = WFD_ERROR_OUT_OF_MEMORY;

    return 0;
}

WFDSource wfdCreateSourceFromStream(WFDDevice d, WFDPipeline p, WFDNativeStreamType s,
                                    const WFDint *a) {
    (void)d;
    (void)p;
    (void)s;
    (void)a;
    unsupported();

    return 0;
}

void wfdDestroySource(WFDDevice d, WFDSource s) {
    if (!device(d)) {
        return;
    }

    if (s < 0x100 || s >= 0x108 || !images[s - 0x100]) {
        error = WFD_ERROR_BAD_HANDLE;

        return;
    }

    images[s - 0x100] = 0;

    if (pending == s) {
        pending = 0;
    }

    if (active == s) {
        active = 0;
    }
}

WFDMask wfdCreateMaskFromImage(WFDDevice d, WFDPipeline p, WFDEGLImage i, const WFDint *a) {
    (void)d;
    (void)p;
    (void)i;
    (void)a;
    unsupported();

    return 0;
}

WFDMask wfdCreateMaskFromStream(WFDDevice d, WFDPipeline p, WFDNativeStreamType s,
                                const WFDint *a) {
    (void)d;
    (void)p;
    (void)s;
    (void)a;
    unsupported();

    return 0;
}

void wfdDestroyMask(WFDDevice d, WFDMask m) {
    (void)d;
    (void)m;
    unsupported();
}

void wfdBindSourceToPipeline(WFDDevice d, WFDPipeline p, WFDSource s, WFDTransition t,
                             const WFDRect *r) {
    (void)r;
    log_call("bind-source");

    if (!pipeline(d, p)) {
        return;
    }

    if (t != WFD_TRANSITION_IMMEDIATE && t != WFD_TRANSITION_AT_VSYNC) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return;
    }

    if (s && (s < 0x100 || s >= 0x108 || !images[s - 0x100])) {
        error = WFD_ERROR_BAD_HANDLE;

        return;
    }

    pending = s;
}

void wfdBindMaskToPipeline(WFDDevice d, WFDPipeline p, WFDMask m, WFDTransition t) {
    (void)d;
    (void)p;
    (void)m;
    (void)t;
    unsupported();
}

WFDint wfdGetPipelineAttribi(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a) {
    /* OEM Screen's default framebuffer chooser skips layer zero (0x1ffe8..0x1ffec). */
    if (!pipeline(d, p)) {
        return 0;
    }

    switch (a) {
    case WFD_PIPELINE_ID:
        return PIPE;
    case WFD_PIPELINE_PORTID:
        return PORT;
    case WFD_PIPELINE_LAYER:
        return FRAMEBUFFER_LAYER;
    case WFD_PIPELINE_SHAREABLE:
    case WFD_PIPELINE_DIRECT_REFRESH:
    case WFD_PIPELINE_FLIP:
    case WFD_PIPELINE_MIRROR:
    case WFD_PIPELINE_ROTATION:
    case WFD_PIPELINE_TRANSPARENCY_ENABLE:
        return 0;
    case WFD_PIPELINE_ROTATION_SUPPORT:
        return WFD_ROTATION_SUPPORT_NONE;
    case WFD_PIPELINE_SCALE_FILTER:
        return WFD_SCALE_FILTER_NONE;
    default:
        error = WFD_ERROR_BAD_ATTRIBUTE;

        return 0;
    }
}

WFDfloat wfdGetPipelineAttribf(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a) {
    if (a == WFD_PIPELINE_GLOBAL_ALPHA) {
        return 1.0f;
    }

    return (WFDfloat)wfdGetPipelineAttribi(d, p, a);
}

void wfdGetPipelineAttribiv(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a, WFDint n,
                            WFDint *v) {
    if (!pipeline(d, p) || !v || n < 1) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return;
    }

    if (a == WFD_PIPELINE_MAX_SOURCE_SIZE) {
        if (n < 2) {
            error = WFD_ERROR_ILLEGAL_ARGUMENT;

            return;
        }

        v[0] = WIDTH;
        v[1] = HEIGHT;
    } else if (a == WFD_PIPELINE_SOURCE_RECTANGLE || a == WFD_PIPELINE_DESTINATION_RECTANGLE) {
        if (n < 4) {
            error = WFD_ERROR_ILLEGAL_ARGUMENT;

            return;
        }

        const WFDRect *r = a == WFD_PIPELINE_SOURCE_RECTANGLE ? &src_rect : &dst_rect;
        v[0] = r->offsetX;
        v[1] = r->offsetY;
        v[2] = r->width;
        v[3] = r->height;
    } else {
        v[0] = wfdGetPipelineAttribi(d, p, a);

        for (int i = 1; i < n; i++) {
            v[i] = 0;
        }
    }
}

void wfdGetPipelineAttribfv(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a, WFDint n,
                            WFDfloat *v) {
    if (!pipeline(d, p) || !v || n < 1) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return;
    }

    if (a == WFD_PIPELINE_SCALE_RANGE) {
        if (n < 2) {
            error = WFD_ERROR_ILLEGAL_ARGUMENT;

            return;
        }

        v[0] = v[1] = 1.0f;
    } else {
        v[0] = wfdGetPipelineAttribf(d, p, a);

        for (int i = 1; i < n; i++) {
            v[i] = 0;
        }
    }
}

void wfdSetPipelineAttribi(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a, WFDint v) {
    if (!pipeline(d, p)) {
        return;
    }

    if (a == WFD_PIPELINE_SCALE_FILTER && v == WFD_SCALE_FILTER_NONE) {
        return;
    }

    if ((a == WFD_PIPELINE_FLIP || a == WFD_PIPELINE_MIRROR || a == WFD_PIPELINE_ROTATION ||
         a == WFD_PIPELINE_TRANSPARENCY_ENABLE) &&
        v == 0) {
        return;
    }

    error = WFD_ERROR_BAD_ATTRIBUTE;
}

void wfdSetPipelineAttribf(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a, WFDfloat v) {
    if (pipeline(d, p) && !(a == WFD_PIPELINE_GLOBAL_ALPHA && v == 1.0f)) {
        error = WFD_ERROR_BAD_ATTRIBUTE;
    }
}

void wfdSetPipelineAttribiv(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a, WFDint n,
                            const WFDint *v) {
    if (!pipeline(d, p) || !v || n != 4) {
        error = WFD_ERROR_ILLEGAL_ARGUMENT;

        return;
    }

    if (a != WFD_PIPELINE_SOURCE_RECTANGLE && a != WFD_PIPELINE_DESTINATION_RECTANGLE) {
        error = WFD_ERROR_BAD_ATTRIBUTE;

        return;
    }

    WFDRect *r = a == WFD_PIPELINE_SOURCE_RECTANGLE ? &src_rect : &dst_rect;
    r->offsetX = v[0];
    r->offsetY = v[1];
    r->width = v[2];
    r->height = v[3];
}

void wfdSetPipelineAttribfv(WFDDevice d, WFDPipeline p, WFDPipelineConfigAttrib a, WFDint n,
                            const WFDfloat *v) {
    (void)d;
    (void)p;
    (void)a;
    (void)n;
    (void)v;
    unsupported();
}

WFDint wfdGetPipelineTransparency(WFDDevice d, WFDPipeline p, WFDbitfield *v, WFDint n) {
    if (!pipeline(d, p)) {
        return 0;
    }

    if (v && n > 0) {
        v[0] = WFD_TRANSPARENCY_NONE;
    }

    return 1;
}

void wfdSetPipelineTSColor(WFDDevice d, WFDPipeline p, WFDTSColorFormat f, WFDint n,
                           const void *c) {
    (void)d;
    (void)p;
    (void)f;
    (void)n;
    (void)c;
    unsupported();
}

WFDint wfdGetPipelineLayerOrder(WFDDevice d, WFDPort p, WFDPipeline q) {
    return port(d, p) && pipeline(d, q) ? 0 : -1;
}

/* Host contract tests can inspect the virtual scanout reference without pretending pixels exist. */
WFDEGLImage voyemu_active_image(void) {
    return active >= 0x100 && active < 0x108 ? images[active - 0x100] : 0;
}

/* The OEM redirector resolves these QNX symbols eagerly even when their
 * extensions aren't advertised. Unsupported entry points fail explicitly. */
WFDHandle wfdCreateDestinationFromImageQNX(void) {
    unsupported();

    return 0;
}

WFDHandle wfdCreateDestinationFromImagesQNX(void) {
    unsupported();

    return 0;
}

WFDHandle wfdCreateDestinationFromStreamQNX(void) {
    unsupported();

    return 0;
}

WFDEGLImage *wfdCreateWFDEGLImagesQNX(void) {
    unsupported();

    return 0;
}

WFDint wfdAcquireDestinationImageQNX(void) {
    unsupported();

    return WFD_ERROR_NOT_SUPPORTED;
}

WFDint wfdBindDestinationToPortQNX(void) {
    unsupported();

    return WFD_ERROR_NOT_SUPPORTED;
}

WFDint wfdDestroyDestinationQNX(void) {
    unsupported();

    return WFD_ERROR_NOT_SUPPORTED;
}

WFDint wfdDestroyWFDEGLImagesQNX(void) {
    unsupported();

    return WFD_ERROR_NOT_SUPPORTED;
}

WFDint wfdReadPixelsFromSourceQNX(void) {
    unsupported();

    return WFD_ERROR_NOT_SUPPORTED;
}

WFDint wfdReleaseDestinationImageQNX(void) {
    unsupported();

    return WFD_ERROR_NOT_SUPPORTED;
}

WFDint wfdWaitForVSyncQNX(void) {
    unsupported();

    return WFD_ERROR_NOT_SUPPORTED;
}
