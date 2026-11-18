#ifndef VOYEMU_STARTUP_GUARD_H
#define VOYEMU_STARTUP_GUARD_H
#include <string.h>
static inline int startup_ready(const char *value) { return value && strcmp(value, "1") == 0; }
static inline int startup_early(int absent, const char *value) { return absent == 1 || (absent == 0 && value && strcmp(value, "0") == 0); }
static inline int startup_fixture_allowed(int witness, int before_absent, const char *before, int after_absent, const char *after) {
    return witness == 1 && startup_early(before_absent, before) && startup_early(after_absent, after);
}
#endif
