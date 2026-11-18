#include "ivc_probe.c"

#include "factory_display_client.c"

static void factory_display_once(void) {
    static unsigned done;
    const char *flag = getenv("QGFX_FACTORY_DISPLAY");

    if (done || !factory_display_enabled(flag)) {
        return;
    }

    done = 1;
    factory_display_send();
}

/* Export only the fixed OEM screenshot artifact after the screenshot main exits. */
extern char *getenv(const char *);
extern int open(const char *, int, ...), close(int);
extern long read(int, void *, usize), lseek(int, long, int);
#include "screen_stream.c"

static int export_enabled(const char *s) {
    return s && s[0] == '1' && !s[1];
}

__attribute__((destructor)) static void export_screen_file(void) {
    /* Deliver the OEM factory trigger even when the continuous stream already exported
     * frames: later continuous frames then carry the switched-on OEM scene. */
    factory_display_once();

    if (export_enabled(getenv("QGFX_SOURCE_CAPTURE")) ||
        !export_enabled(getenv("QGFX_EXPORT_SCREEN"))) {
        return;
    }

    if (screen_stream_exported) {
        return;
    }

    int fd = open("/log/oem-screen.bmp", 0);

    if (fd < 0) {
        emit("QG3 EXPORT fail=open\n");

        return;
    }

    long size = lseek(fd, 0, 2);

    if (size < 54 || size > 1920L * 720 * 4 + 4096 || lseek(fd, 0, 0) != 0) {
        close(fd);
        emit("QG3 EXPORT fail=extent\n");

        return;
    }

    void *data = malloc((usize)size);

    if (!data) {
        close(fd);

        return;
    }

    usize at = 0;

    while (at < (usize)size) {
        long n = read(fd, (unsigned char *)data + at, (usize)size - at);

        if (n <= 0) {
            break;
        }

        at += (usize)n;
    }

    close(fd);

    if (at != (usize)size || ((unsigned char *)data)[0] != 'B' ||
        ((unsigned char *)data)[1] != 'M') {
        free(data);
        emit("QG3 EXPORT fail=read\n");

        return;
    }

    acquire();
    u64 id = upload(data, (usize)size);
    u64 args[] = {id, (u64)size};
    u64 ok = id ? call(354, args, 2, 0, 0) : 0;
    drop_blob(id);
    release();
    free(data);
    emit(ok ? "QG3 EXPORT screen-file=ok\n" : "QG3 EXPORT fail=transport\n");
}
