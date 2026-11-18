/*
 * libqg_runtime.so — stub JNI library for emulator environment.
 * Replaces the OEM library that reads/writes NVRAM on real hardware.
 * Stores NVRAM key/value pairs in a flat file NVRAM_FILE that the first caller
 * copies from the read-only seed NVRAM_SEED of the system image.
 */

#include <jni.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <android/log.h>
#include <sys/file.h>
#include <sys/stat.h>

#define TAG "qg_runtime"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

#ifndef NVRAM_FILE
#define NVRAM_FILE "/data/user_de/0/com.voyemu/nvram"
#endif
#ifndef NVRAM_SEED
#define NVRAM_SEED "/system/etc/nvram"
#endif
#define MAX_VALUE 512
/* Signed 32-bit key, equals, 511 value bytes, LF and NUL. */
#define MAX_LINE (MAX_VALUE + 32)
/* A rewrite of one key rebuilds the whole file in memory; a larger file is
 * rejected instead of silently truncated. The value must stay short enough
 * for the OEM line format. */
#define MAX_FILE (2 * 1024 * 1024)

/*
 * Every OEM caller of this library is a system_app process of uid 1000, like the
 * Voyemu bootstrap package whose device-encrypted data directory holds the store.
 * The stock policy lets system_app create, rename and rewrite system_app_data_file
 * there and read the system_file seed, so the store needs no install step, label
 * or rule of its own. The first caller copies the seed to a temporary file and
 * publishes it with rename under an exclusive lock on the directory: no caller
 * sees a partial store, and an existing store is never replaced.
 */
static int seed_nvram_file(const char *path, const char *seed, int directory) {
    char temporary[1024];
    char chunk[8192];
    size_t total = 0;
    int result = -1;

    if (snprintf(temporary, sizeof(temporary), "%s.seed", path) >= (int)sizeof(temporary)) {
        return -1;
    }
    /* A seed copy left by a caller that died before rename is incomplete. */
    if (unlink(temporary) != 0 && errno != ENOENT) {
        LOGE("cannot remove stale %s: %s", temporary, strerror(errno));
        return -1;
    }
    int in = open(seed, O_RDONLY | O_CLOEXEC);

    if (in < 0) {
        LOGE("NVRAM seed %s unavailable: %s", seed, strerror(errno));
        return -1;
    }
    int out = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0660);

    if (out < 0) {
        LOGE("cannot create %s: %s", temporary, strerror(errno));
        close(in);
        return -1;
    }
    for (;;) {
        ssize_t got = read(in, chunk, sizeof(chunk));

        if (got < 0 && errno == EINTR) {
            continue;
        }
        if (got <= 0) {
            result = got == 0 ? 0 : -1;
            break;
        }
        total += (size_t)got;
        if (total > MAX_FILE) {
            LOGE("NVRAM seed %s exceeds %d bytes", seed, MAX_FILE);
            break;
        }
        for (ssize_t done = 0; done < got;) {
            ssize_t put = write(out, chunk + done, (size_t)(got - done));

            if (put < 0 && errno == EINTR) {
                continue;
            }
            if (put <= 0) {
                got = -1;
                break;
            }
            done += put;
        }
        if (got < 0) {
            break;
        }
    }
    /* The process umask must not narrow the mode shared by all uid 1000 callers. */
    if (result == 0 && (fchmod(out, 0660) != 0 || fsync(out) != 0)) {
        result = -1;
    }
    if (close(out) != 0) {
        result = -1;
    }
    close(in);
    if (result == 0 && rename(temporary, path) != 0) {
        LOGE("cannot publish %s: %s", path, strerror(errno));
        result = -1;
    }
    if (result == 0) {
        fsync(directory);
        LOGD("seeded %s from %s", path, seed);
    } else {
        unlink(temporary);
    }
    return result;
}

static int ensure_nvram_file() {
    const char *path = NVRAM_FILE;
    char directory[1024];

    if (access(path, F_OK) == 0) {
        return 0;
    }
    if (errno != ENOENT) {
        LOGE("%s unavailable: %s", path, strerror(errno));
        return -1;
    }
    const char *slash = strrchr(path, '/');

    if (!slash || slash == path || (size_t)(slash - path) >= sizeof(directory)) {
        return -1;
    }
    memcpy(directory, path, (size_t)(slash - path));
    directory[slash - path] = '\0';
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

    if (fd < 0) {
        LOGE("NVRAM directory %s unavailable: %s", directory, strerror(errno));
        return -1;
    }
    while (flock(fd, LOCK_EX) != 0) {
        if (errno != EINTR) {
            close(fd);
            return -1;
        }
    }
    /* Another caller may have published the store while this one waited. */
    int result = access(path, F_OK) == 0 ? 0 : seed_nvram_file(path, NVRAM_SEED, fd);

    close(fd);
    return result;
}

static int read_nvram_entry(const char *key, char *out, int outlen) {
    if (ensure_nvram_file() != 0) {
        out[0] = '\0';
        return -1;
    }
    /* Shared lock: a concurrent rewrite holds the exclusive lock for its
     * whole truncate+write, so a reader must not see the file mid-way. */
    int fd = open(NVRAM_FILE, O_RDONLY);

    if (fd < 0) {
        out[0] = '\0';
        return -1;
    }
    while (flock(fd, LOCK_SH) != 0) {
        if (errno != EINTR) {
            close(fd);
            out[0] = '\0';
            return -1;
        }
    }
    FILE *f = fdopen(fd, "r");

    if (!f) {
        close(fd);
        out[0] = '\0';
        return -1;
    }
    char line[MAX_LINE];
    char searchkey[96];
    snprintf(searchkey, sizeof(searchkey), "%s=", key);
    int found = 0;
    while (!feof(f) && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);

        if (len > 0 && line[len - 1] != '\n') {
            /* The physical line exceeds the buffer: the tail belongs to the
             * same entry, not to a new key, so consume and discard it. */
            int ch;

            while ((ch = fgetc(f)) != EOF && ch != '\n') {
            }
            if (ch == EOF && ferror(f)) {
                break;
            }
        }
        if (strncmp(line, searchkey, strlen(searchkey)) == 0) {
            char *val = line + strlen(searchkey);
            int vlen = strlen(val);
            /* strip trailing newline */
            if (vlen > 0 && val[vlen-1] == '\n') val[vlen-1] = '\0';
            if (vlen > 1 && val[vlen-2] == '\r') val[vlen-2] = '\0';
            strncpy(out, val, outlen - 1);
            out[outlen - 1] = '\0';
            found = 1;
            break;
        }
    }
    if (ferror(f)) {
        /* A read error is not the end of the file: report the key as absent. */
        LOGE("read error on %s", NVRAM_FILE);
        found = -1;
    }
    fclose(f);
    if (found <= 0) out[0] = '\0';
    return found;
}

/* Appends one formatted line to *cursor; fails (returns -1) instead of
 * letting the pointer run past the buffer when the text does not fit. */
static int append_line(char **cursor, size_t left, const char *fmt, ...) {
    va_list ap;

    va_start(ap, fmt);
    int need = vsnprintf(*cursor, left, fmt, ap);
    va_end(ap);

    if (need < 0 || (size_t)need >= left) {
        return -1;
    }
    *cursor += need;
    return 0;
}

static int write_nvram_entry(const char *key, const char *value) {
    if (ensure_nvram_file() != 0) return -1;
    /* Rewrite in place: creating a new file in /data is SELinux-blocked. */
    if (strlen(value) >= MAX_VALUE) {
        LOGE("nvram value for key %s exceeds %d bytes; rejected", key, MAX_VALUE - 1);
        return -1;
    }
    if (strpbrk(value, "\r\n")) {
        LOGE("nvram value for key %s contains a line separator; rejected", key);
        return -1;
    }
    /* The whole read-modify-write is one exclusive-lock transaction on the
     * file itself: concurrent writers — other threads or other OEM
     * processes holding this file open — serialize here, and readers take
     * the shared lock, so nobody observes or creates a torn rewrite.
     * Without it two writers both succeed and the later rewrite silently
     * drops the earlier key change. */
    int fd = open(NVRAM_FILE, O_RDWR);

    if (fd < 0) return -1;
    while (flock(fd, LOCK_EX) != 0) {
        if (errno != EINTR) {
            close(fd);
            return -1;
        }
    }
    FILE *f = fdopen(fd, "r+");

    if (!f) {
        close(fd);
        return -1;
    }
    /* Heap, not stack: a JNI caller thread may have as little as 1 MiB of
     * stack. buffer holds up to MAX_FILE file bytes plus an appended LF and
     * a NUL; rebuilt holds that plus one freshly formatted key line. */
    char *buffer = malloc(MAX_FILE + 2);
    char *rebuilt_start = malloc(MAX_FILE + 2 * MAX_LINE);
    if (!buffer || !rebuilt_start) {
        LOGE("out of memory rewriting %s", NVRAM_FILE);
        free(buffer);
        free(rebuilt_start);
        fclose(f);
        return -1;
    }
    const size_t rebuilt_capacity = MAX_FILE + 2 * MAX_LINE;
    /* Read the whole file or refuse: a partial read followed by the rewrite
     * below would silently drop everything past the buffer. A short fread
     * may also mean an I/O error, not the end of the file: ferror is checked
     * before the buffer is trusted or any further stream operation runs. */
    size_t n = fread(buffer, 1, MAX_FILE + 1, f);
    int partial = (n == MAX_FILE + 1);

    if (ferror(f) || partial) {
        if (ferror(f)) {
            LOGE("read error on %s; write rejected", NVRAM_FILE);
        } else {
            LOGE("%s exceeds %d bytes; write rejected to avoid truncation",
                 NVRAM_FILE, MAX_FILE);
        }
        free(buffer);
        free(rebuilt_start);
        fclose(f);
        return -1;
    }
    if (n > 0 && buffer[n - 1] != '\n') {
        buffer[n] = '\n';
        n++;
    }
    buffer[n] = '\0';

    char searchkey[96];
    snprintf(searchkey, sizeof(searchkey), "%s=", key);
    /* Fast path: the file is typically small; rebuild it by lines. */
    char *start = buffer;
    char *end = start + n;
    char *rebuilt = rebuilt_start;
    int replaced = 0;
    int overflow = 0;
    char *line = start;
    while (line < end) {
        char *nl = memchr(line, '\n', end - line);
        char *next = nl ? nl + 1 : end;
        if (nl) *nl = '\0';
        if (strncmp(line, searchkey, strlen(searchkey)) == 0) {
            /* Duplicate keys are collapsed: the first occurrence is rewritten
             * with the new value, later ones are dropped. */
            if (!replaced) {
                replaced = 1;
                overflow |= append_line(&rebuilt,
                        rebuilt_capacity - (rebuilt - rebuilt_start),
                        "%s=%s\n", key, value) != 0;
            }
        } else if (line[0]) {
            overflow |= append_line(&rebuilt,
                    rebuilt_capacity - (rebuilt - rebuilt_start),
                    "%s\n", line) != 0;
        }
        line = next;
    }
    if (!replaced) {
        overflow |= append_line(&rebuilt, rebuilt_capacity - (rebuilt - rebuilt_start),
                 "%s=%s\n", key, value) != 0;
    }

    /* Refuse before touching the file: a result above MAX_FILE would make
     * the very next write reject its own input. */
    size_t rebuilt_size = (size_t)(rebuilt - rebuilt_start);
    if (overflow || rebuilt_size > MAX_FILE) {
        LOGE("%s rewrite for key %s exceeds %d bytes; rejected", NVRAM_FILE, key, MAX_FILE);
        free(buffer);
        free(rebuilt_start);
        fclose(f);
        return -1;
    }

    if (fseek(f, 0, SEEK_SET) != 0 || ftruncate(fd, 0) != 0) {
        LOGE("cannot seek or truncate %s", NVRAM_FILE);
        free(buffer);
        free(rebuilt_start);
        fclose(f);
        return -1;
    }
    size_t written = fwrite(rebuilt_start, 1, rebuilt_size, f);
    /* Flush to disk before closing: an unflushed config store survives
     * process death but not power loss. A short write is already fatal for
     * the caller (-1); the original bytes are consumed by the rebuild, so
     * no in-place rollback is possible here. */
    int sync_failed = (fflush(f) != 0 || fsync(fd) != 0);
    int close_failed = (fclose(f) != 0);
    free(buffer);
    free(rebuilt_start);
    if (written != rebuilt_size || close_failed || sync_failed) {
        LOGE("short write to %s", NVRAM_FILE);
        return -1;
    }
    LOGD("wrote nvram key %s", key);
    return 0;
}

static int read_nvram_key(int key, char *out, int outlen) {
    char name[32];
    snprintf(name, sizeof(name), "%d", key);
    return read_nvram_entry(name, out, outlen);
}

static int write_nvram_key(int key, const char *value) {
    char name[32];
    snprintf(name, sizeof(name), "%d", key);
    return write_nvram_entry(name, value);
}

/* JNI methods — registered statically for com.qinggan.os.NvramNative */

JNIEXPORT jstring JNICALL Java_com_qinggan_os_NvramNative_native_1nvramread(JNIEnv *env, jclass cls,
                                                                            jint key) {
    char value[MAX_VALUE];
    read_nvram_key(key, value, sizeof(value));
    LOGD("nvramread(%d)", key);
    return (*env)->NewStringUTF(env, value);
}

/* Both A11 read entry points address the emulator's same keyed text store. */
JNIEXPORT jstring JNICALL Java_com_qinggan_os_NvramNative_native_1nvramread64(JNIEnv *env,
                                                                              jclass cls,
                                                                              jint key) {
    return Java_com_qinggan_os_NvramNative_native_1nvramread(env, cls, key);
}

JNIEXPORT jint JNICALL Java_com_qinggan_os_NvramNative_native_1nvramwrite(JNIEnv *env, jclass cls,
                                                                          jstring str, jint key) {
    const char *value = (*env)->GetStringUTFChars(env, str, NULL);
    if (!value) {
        return -1;
    }
    int ret = write_nvram_key(key, value);
    LOGD("nvramwrite(%d) = %d", key, ret);
    (*env)->ReleaseStringUTFChars(env, str, value);
    return ret;
}

JNIEXPORT jstring JNICALL Java_com_qinggan_os_NvramNative_ioctl_1return_1string(JNIEnv *env,
                                                                                jclass cls, jint i1,
                                                                                jint i2, jint i3) {
    /* Return empty string for unsupported ioctl */
    LOGD("ioctl_return_string(%d, %d, %d) = empty", i1, i2, i3);
    return (*env)->NewStringUTF(env, "");
}

JNIEXPORT jint JNICALL Java_com_qinggan_os_NvramNative_ioctl_1setArray(JNIEnv *env, jclass cls,
                                                                       jint i1, jint i2,
                                                                       jbyteArray arr) {
    /* No-op success */
    LOGD("ioctl_setArray(%d, %d, ...) = 0", i1, i2);
    return 0;
}

/* Virtual backlight registers share the existing cross-process NVRAM store.
 * Their textual namespace cannot collide with the public integer NVRAM keys.
 * Backlight gating is separate from qg.power_manager's LCD supply/blackout
 * state; these JNI calls never switch the Android display power service. */
static int backlight_get(const char *key, int initial, int maximum) {
    char value[MAX_VALUE];
    if (ensure_nvram_file() != 0) {
        return -ENOENT;
    }
    int found = read_nvram_entry(key, value, sizeof(value));
    if (found < 0) {
        return -EIO;
    }
    if (!found) {
        return initial;
    }
    char *end;
    errno = 0;
    long parsed = strtol(value, &end, 10);
    if (errno || end == value || *end || parsed < 0 || parsed > maximum) {
        return -EINVAL;
    }
    return (int)parsed;
}

static int backlight_put(const char *key, int value, int maximum) {
    if (value < 0 || value > maximum) {
        return -EINVAL;
    }
    char text[16];
    snprintf(text, sizeof(text), "%d", value);
    return write_nvram_entry(key, text) == 0 ? 0 : -EIO;
}

/* OEM screen constants are 0..3 for SCREEN_1..4 and 4 for SCREEN_DASHBOARD. */
static int backlight_screen_key(char *key, size_t size, int screen, const char *field) {
    if (screen < 0 || screen > 4) {
        return -EINVAL;
    }
    snprintf(key, size, "voyemu.backlight.screen.%d.%s", screen, field);
    return 0;
}

static int backlight_screen_get(int screen, const char *field, int initial, int maximum) {
    char key[96];
    if (backlight_screen_key(key, sizeof(key), screen, field) != 0) {
        return -EINVAL;
    }
    return backlight_get(key, initial, maximum);
}

static int backlight_screen_put(int screen, const char *field, int value, int maximum) {
    char key[96];
    if (backlight_screen_key(key, sizeof(key), screen, field) != 0) {
        return -EINVAL;
    }
    return backlight_put(key, value, maximum);
}

#define BACKLIGHT_JNI(name) JNIEXPORT jint JNICALL Java_com_qinggan_os_BackLightNative_##name

BACKLIGHT_JNI(native_1set_1mode)(JNIEnv *env, jclass cls, jint mode, jint screen) {
    (void)env;
    (void)cls;
    /* The OEM Java wrapper maps its manual mode 0 to native mode 1. */
    return backlight_screen_put(screen, "mode", mode, 1);
}

BACKLIGHT_JNI(native_1get_1mode)(JNIEnv *env, jclass cls, jint screen) {
    (void)env;
    (void)cls;
    return backlight_screen_get(screen, "mode", 1, 1);
}

BACKLIGHT_JNI(native_1set_1backlight_1for_1screen)(JNIEnv *env, jclass cls, jint value,
                                                   jint screen) {
    (void)env;
    (void)cls;
    return backlight_screen_put(screen, "brightness", value, 255);
}

BACKLIGHT_JNI(native_1get_1backlight_1for_1screen)(JNIEnv *env, jclass cls, jint screen) {
    (void)env;
    (void)cls;
    return backlight_screen_get(screen, "brightness", 255, 255);
}

BACKLIGHT_JNI(native_1set_1backlight)(JNIEnv *env, jclass cls, jint value) {
    return Java_com_qinggan_os_BackLightNative_native_1set_1backlight_1for_1screen(env, cls, value,
                                                                                   0);
}

BACKLIGHT_JNI(native_1get_1backlight)(JNIEnv *env, jclass cls) {
    return Java_com_qinggan_os_BackLightNative_native_1get_1backlight_1for_1screen(env, cls, 0);
}

BACKLIGHT_JNI(native_1close_1backlight_1for_1screen)(JNIEnv *env, jclass cls, jint screen) {
    (void)env;
    (void)cls;
    /* The OEM wrapper interprets raw state 0 as enabled and raw state 1 as disabled. */
    return backlight_screen_put(screen, "gate", 1, 1);
}

BACKLIGHT_JNI(native_1open_1backlight_1for_1screen)(JNIEnv *env, jclass cls, jint screen) {
    (void)env;
    (void)cls;
    return backlight_screen_put(screen, "gate", 0, 1);
}

BACKLIGHT_JNI(native_1get_1screen_1state_1for_1screen)(JNIEnv *env, jclass cls, jint screen) {
    (void)env;
    (void)cls;
    return backlight_screen_get(screen, "gate", 0, 1);
}

BACKLIGHT_JNI(native_1close_1backlight)(JNIEnv *env, jclass cls) {
    return Java_com_qinggan_os_BackLightNative_native_1close_1backlight_1for_1screen(env, cls, 0);
}

BACKLIGHT_JNI(native_1open_1backlight)(JNIEnv *env, jclass cls) {
    return Java_com_qinggan_os_BackLightNative_native_1open_1backlight_1for_1screen(env, cls, 0);
}

BACKLIGHT_JNI(native_1get_1screen_1state)(JNIEnv *env, jclass cls) {
    return Java_com_qinggan_os_BackLightNative_native_1get_1screen_1state_1for_1screen(env, cls, 0);
}

BACKLIGHT_JNI(native_1close_1brightness)(JNIEnv *env, jclass cls) {
    return Java_com_qinggan_os_BackLightNative_native_1close_1backlight(env, cls);
}

BACKLIGHT_JNI(native_1open_1brightness)(JNIEnv *env, jclass cls) {
    return Java_com_qinggan_os_BackLightNative_native_1open_1backlight(env, cls);
}

BACKLIGHT_JNI(native_1set_1keypad_1backlight)(JNIEnv *env, jclass cls, jint value) {
    (void)env;
    (void)cls;
    return backlight_put("voyemu.backlight.key.brightness", value, 255);
}

BACKLIGHT_JNI(native_1get_1key_1backlight)(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return backlight_get("voyemu.backlight.key.brightness", 255, 255);
}

BACKLIGHT_JNI(native_1set_1key_1state)(JNIEnv *env, jclass cls, jint value) {
    (void)env;
    (void)cls;
    return backlight_put("voyemu.backlight.key.enabled", value, 1);
}

BACKLIGHT_JNI(native_1get_1key_1state)(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return backlight_get("voyemu.backlight.key.enabled", 1, 1);
}

BACKLIGHT_JNI(native_1backlight_1set_1stm8)(JNIEnv *env, jclass cls, jint value) {
    (void)env;
    (void)cls;
    return backlight_put("voyemu.backlight.stm8.brightness", value, 255);
}

BACKLIGHT_JNI(native_1backlight_1get_1stm8)(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return backlight_get("voyemu.backlight.stm8.brightness", 255, 255);
}

BACKLIGHT_JNI(native_1set_1stm8_1state)(JNIEnv *env, jclass cls, jint value) {
    (void)env;
    (void)cls;
    return backlight_put("voyemu.backlight.stm8.state", value, 1);
}

BACKLIGHT_JNI(native_1get_1stm8_1state)(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return backlight_get("voyemu.backlight.stm8.state", 0, 1);
}

BACKLIGHT_JNI(native_1slidebar_1set_1state)(JNIEnv *env, jclass cls, jint value) {
    (void)env;
    (void)cls;
    return backlight_put("voyemu.backlight.slidebar.state", value, 1);
}

JNIEXPORT jstring JNICALL
Java_com_qinggan_os_BackLightNative_native_1get_1backlight_1ver(JNIEnv *env, jclass cls) {
    (void)cls;
    return (*env)->NewStringUTF(env, "voyemu-backlight-v1");
}

#undef BACKLIGHT_JNI
