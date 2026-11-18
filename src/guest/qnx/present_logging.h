/* Private diagnostics: no per-frame formatting or writes unless explicitly enabled. */
#ifndef QGFX_PRESENT_LOGGING_H
#define QGFX_PRESENT_LOGGING_H
extern char *getenv(const char *);
typedef struct {
    unsigned trace_checked, trace, success_logged;
} PresentLogState;

static int present_trace_enabled(PresentLogState *state) {
    if (!state->trace_checked) {
        const char *flag = getenv("QGFX_PRESENT_TRACE");
        state->trace = flag && flag[0] == '1' && !flag[1];
        state->trace_checked = 1;
    }

    return state->trace;
}

static const char *present_summary_level(PresentLogState *state, int posted) {
    if (posted) {
        return "ERROR";
    }

    if (!state->success_logged) {
        state->success_logged = 1;

        return "INFO";
    }

    return state->trace ? "DEBUG" : 0;
}

#endif
