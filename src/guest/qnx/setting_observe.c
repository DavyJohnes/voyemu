/* Read the original persistent-settings getter; never initialize or change state. */
static void observe_setting_state(unsigned poll) {
    static unsigned have, last_value, last_bytes;
    static int last_rc;
    const char *flag = getenv("QGFX_SETTING_OBSERVE");

    if (!flag || flag[0] != '1' || flag[1]) {
        return;
    }

    void *library = dlopen("/lib64/libsettingpers.so.0", 2);
    unsigned (*size)(unsigned) =
            library ? (unsigned (*)(unsigned))dlsym(library, "libsettingpers_getSettingSize") : 0;
    int (*get)(unsigned, void *, unsigned) =
            library ? (int (*)(unsigned, void *, unsigned))dlsym(library,
                                                                 "libsettingpers_getSettingValue")
                    : 0;
    unsigned bytes = size ? size(3) : 0, value = 0;
    int rc = -1;

    if (get && bytes >= 1 && bytes <= sizeof(value)) {
        rc = get(3, &value, bytes);
    }

    if (!have || value != last_value || bytes != last_bytes || rc != last_rc || poll % 20 == 0) {
        char text[160];
        snprintf(text, sizeof(text),
                 "QG3 SETTING language-value=%u bytes=%u read-rc=%d pid=%d poll=%u\n", value, bytes,
                 rc, getpid(), poll);
        emit(text);
        have = 1;
        last_value = value;
        last_bytes = bytes;
        last_rc = rc;
    }
}
