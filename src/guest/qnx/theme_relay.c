/* NOLINTBEGIN(modernize-use-nullptr): C source without nullptr, also included by C++ tests. */
#include "original_symbol.h"
#ifdef QGFX_QINGGAN11_611
/* Exact611 uses the two original requests; their failure is not atomic. */
static u64 qinggan11_theme_ack, qinggan11_theme_attempted;
static unsigned qinggan11_theme_outcome, qinggan11_theme_after_mode, qinggan11_theme_after_skin;

static int qinggan11_theme_skin_valid(unsigned skin) {
    return skin == 1 || skin == 1001 || skin == 3001 || skin == 3002 || skin == 3003;
}

static void relay_theme(void) {
    const char *flag = getenv("QGFX_THEME_RELAY");

    if (!flag || flag[0] != '1' || flag[1]) {
        return;
    }

    static void *library, *entries[5];
    const char *path = "/lib64/libenginecore.so.0";
    unsigned (*get_mode)(void) = (unsigned (*)(void))original_symbol(
            &library, &entries[0], path, "_ZN12CSettingInfo12getTheme_iviEv");
    unsigned (*get_skin)(void) = (unsigned (*)(void))original_symbol(
            &library, &entries[1], path, "_ZN12CSettingInfo12getThemeskinEv");
    void (*set_mode)(unsigned) = (void (*)(unsigned))original_symbol(
            &library, &entries[2], path, "_ZN12CSettingInfo8setThemeEj");
    void (*set_skin)(unsigned) = (void (*)(unsigned))original_symbol(
            &library, &entries[3], path, "_ZN12CSettingInfo12setThemeskinEj");
    int (*issue)(unsigned, void *, unsigned, unsigned) =
            (int (*)(unsigned, void *, unsigned, unsigned))original_symbol(
                    &library, &entries[4], path, "_Z18engnCore_issue2mcdjPvjj");

    if (!get_mode || !get_skin || !set_mode || !set_skin || !issue) {
        emit("QG3 THEME611 unavailable=original-setting-api\n");

        return;
    }

    unsigned mode_before = get_mode(), skin_before = get_skin();
    acquire();
    u64 args[] = {qinggan11_theme_ack,
                  mode_before,
                  uniform_hits,
                  uniform_missing_table,
                  uniform_unknown_name,
                  uniform_evictions,
                  0x6110001,
                  skin_before,
                  qinggan11_theme_attempted,
                  qinggan11_theme_outcome,
                  0,
                  0,
                  qinggan11_theme_after_mode,
                  qinggan11_theme_after_skin};
    u64 seq = call(355, args, 14, 0, 0);
    unsigned command[2] = {0, 0};
    int valid = packet && !packet->fault && packet->out_len == sizeof(command);

    if (valid) {
        copy(command, packet->data, sizeof(command));
    }

    release();

    if (!seq || seq <= qinggan11_theme_ack || seq >= 0x100000000ULL ||
        seq == qinggan11_theme_attempted || !valid) {
        return;
    }

    const unsigned mode = command[0], skin = command[1];

    if (!qinggan11_theme_skin_valid(skin) || mode != (skin + 999) / 1000) {
        emit("QG3 THEME611 fail=mode-skin-contract\n");

        return;
    }

    qinggan11_theme_attempted = seq;
    qinggan11_theme_outcome = 1;
    set_mode(mode);
    unsigned request[] = {4, mode};
    int mode_rc = issue(24, request, 8, 8), skin_rc = -1;

    if (!mode_rc) {
        set_skin(skin);
        request[0] = 13;
        request[1] = skin;
        skin_rc = issue(24, request, 8, 8);
        qinggan11_theme_outcome = skin_rc ? 2 : 3;
    }

    qinggan11_theme_after_mode = get_mode();
    qinggan11_theme_after_skin = get_skin();

    if (!mode_rc && !skin_rc && qinggan11_theme_after_mode == mode &&
        qinggan11_theme_after_skin == skin) {
        const char *entry = getenv("QGFX_THEME_REQUEST_ENTRY");
        int entry_ok = 1;

        if (entry && entry[0] == '1' && !entry[1]) {
            void *main_program = dlopen(0, 2);
            void (*change)(void) = main_program
                                           ? (void (*)(void))dlsym(main_program,
                                                                   "_ZN7CCommon14changethemeReqEv")
                                           : 0;

            if (change) {
                change();
            } else {
                entry_ok = 0;
            }
        }

        if (entry_ok) {
            qinggan11_theme_outcome = 4;
            qinggan11_theme_ack = seq;
        } else {
            qinggan11_theme_outcome = 5;
        }
    }

    char line[224];
    snprintf(
            line, sizeof(line),
            "QG3 THEME611 seq=%llu mode=%u skin=%u before-mode=%u before-skin=%u mode-rc=%d skin-rc=%d after-mode=%u after-skin=%u outcome=%u pid=%d\n",
            seq, mode, skin, mode_before, skin_before, mode_rc, skin_rc, qinggan11_theme_after_mode,
            qinggan11_theme_after_skin, qinggan11_theme_outcome, getpid());
    emit(line);
}
#else
/* Use the original setting request used by CIvc_modu; never write UI properties. */
static void relay_theme(void) {
    static u64 acknowledged;
    static unsigned observations;
    const char *flag = getenv("QGFX_THEME_RELAY");

    if (!flag || flag[0] != '1' || flag[1]) {
        return;
    }

    static void *library, *entries[3];
    const char *path = "/lib64/libenginecore.so.0";
    unsigned (*get_theme)(void) = (unsigned (*)(void))original_symbol(
            &library, &entries[0], path, "_ZN12CSettingInfo8getThemeEv");
    unsigned current = get_theme ? get_theme() : 0;
    acquire();
    u64 args[] = {acknowledged,         current,          uniform_hits, uniform_missing_table,
                  uniform_unknown_name, uniform_evictions};
    u64 seq = call(355, args, 6, 0, 0);
    unsigned mode = packet && packet->out_len == 1 ? packet->data[0] : 0;
    release();

    if (get_theme && observations++ < 64) {
        char state[128];
        snprintf(state, sizeof(state), "QG3 THEME state-current=%u ack=%llu pid=%d\n", get_theme(),
                 acknowledged, getpid());
        emit(state);
    }

    if (!seq || seq <= acknowledged || mode < 1 || mode > 6) {
        return;
    }

    int (*issue)(unsigned, void *, unsigned, unsigned) =
            (int (*)(unsigned, void *, unsigned, unsigned))original_symbol(
                    &library, &entries[1], path, "_Z18engnCore_issue2mcdjPvjj");
    void (*set_theme)(unsigned) = (void (*)(unsigned))original_symbol(
            &library, &entries[2], path, "_ZN12CSettingInfo8setThemeEj");

    if (!issue || !set_theme) {
        emit("QG3 THEME unavailable=original-setting-api\n");

        return;
    }

    unsigned before = get_theme ? get_theme() : 0;
    set_theme(mode);
    unsigned request[] = {4, mode};
    int rc = issue(24, request, 8, 8);
    char line[192];
    snprintf(
            line, sizeof(line),
            "QG3 THEME seq=%llu mode=%u current-before=%u current-after=%u native-setting-rc=%d pid=%d\n",
            seq, mode, before, get_theme ? get_theme() : 0, rc, getpid());
    emit(line);

    if (rc == 0) {
        const char *request_flag = getenv("QGFX_THEME_REQUEST_ENTRY");

        if (request_flag && request_flag[0] == '1' && !request_flag[1]) {
            void *main_program = dlopen(0, 2);
            void (*request_change)(void) =
                    main_program
                            ? (void (*)(void))dlsym(main_program, "_ZN7CCommon14changethemeReqEv")
                            : 0;

            if (request_change) {
                request_change();
                emit("QG3 THEME original-request-entry=called\n");
            } else {
                emit("QG3 THEME original-request-entry=unavailable\n");
            }
        }

        acknowledged = seq;
    }
}
#endif
/* NOLINTEND(modernize-use-nullptr) */
