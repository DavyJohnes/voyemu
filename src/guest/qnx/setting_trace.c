/* Scoped observers around original mset callbacks/writes; forward each call exactly once. */
extern int strcmp(const char *, const char *);
typedef int (*SettingMessage)(unsigned, unsigned, void *, unsigned, void *);
typedef int (*SettingWork)(unsigned, void *);
typedef struct {
    SettingMessage message;
    SettingWork work;
} SettingCallbacks;
static SettingCallbacks setting_original, setting_proxy;
static unsigned setting_registered;

static int setting_trace_enabled(void) {
    const char *f = getenv("QGFX_SETTING_OBSERVE");

    return f && f[0] == '1' && !f[1];
}

static int setting_message(unsigned message, unsigned source, void *data, unsigned bytes,
                           void *context) {
    unsigned words[2] = {0, 0};

    if (data && bytes == 8) {
        copy(words, data, 8);
    }

    if ((message == 23 || message == 24) && data && bytes == 8 && words[0] == 3 && words[1] <= 1) {
        char text[192];
        snprintf(text, sizeof(text),
                 "QG3 SETTING received-msg=%u source=%u bytes=%u value=%u pid=%d\n", message,
                 source, bytes, words[1], getpid());
        emit(text);
    }

    return setting_original.message(message, source, data, bytes, context);
}

static int setting_work(unsigned state, void *context) {
    char text[128];

    snprintf(text, sizeof(text), "QG3 SETTING work-state=%u pid=%d\n", state, getpid());
    emit(text);

    return setting_original.work(state, context);
}

int Mcdif_init(const char *name, const SettingCallbacks *callbacks, void *context) {
    void *library = dlopen("/lib64/libmcdif.so.0", 2);

    int (*native)(const char *, const SettingCallbacks *,
                  void *) = library ? dlsym(library, "Mcdif_init") : 0;

    if (!native || native == Mcdif_init) {
        return -1;
    }

    if (!setting_trace_enabled() || !name || strcmp(name, "mset") || !callbacks ||
        setting_registered) {
        return native(name, callbacks, context);
    }

    setting_original = *callbacks;
    setting_proxy.message = callbacks->message ? setting_message : 0;
    setting_proxy.work = callbacks->work ? setting_work : 0;
    setting_registered = 1;
    int rc = native(name, &setting_proxy, context);
    char text[128];
    snprintf(text, sizeof(text), "QG3 SETTING original-init-rc=%d pid=%d\n", rc, getpid());
    emit(text);

    return rc;
}

int libsettingpers_setSettingValue(unsigned item, void *data, unsigned bytes) {
    void *library = dlopen("/lib64/libsettingpers.so.0", 2);

    int (*native)(unsigned, void *,
                  unsigned) = library ? dlsym(library, "libsettingpers_setSettingValue") : 0;

    if (!native || native == libsettingpers_setSettingValue) {
        return -1;
    }

    unsigned value = 0, selected = setting_trace_enabled() && item == 3 && data && bytes >= 1 &&
                                   bytes <= sizeof(value);

    if (selected) {
        copy(&value, data, bytes);
    }

    int rc = native(item, data, bytes);

    if (selected) {
        char text[192];
        snprintf(text, sizeof(text),
                 "QG3 SETTING write-item=%u bytes=%u value=%u original-rc=%d pid=%d\n", item, bytes,
                 value, rc, getpid());
        emit(text);
    }

    return rc;
}

int libsettingpers_initialize(void *callbacks, const char *name) {
    void *library = dlopen("/lib64/libsettingpers.so.0", 2);

    int (*native)(void *, const char *) = library ? dlsym(library, "libsettingpers_initialize") : 0;

    if (!native || native == libsettingpers_initialize) {
        return -1;
    }

    unsigned selected = setting_trace_enabled();

    if (selected) {
        char text[128];
        snprintf(text, sizeof(text), "QG3 SETTING pers-init=enter pid=%d\n", getpid());
        emit(text);
    }

    int rc = native(callbacks, name);

    if (selected) {
        char text[128];
        snprintf(text, sizeof(text), "QG3 SETTING pers-init=exit original-rc=%d pid=%d\n", rc,
                 getpid());
        emit(text);
    }

    return rc;
}
