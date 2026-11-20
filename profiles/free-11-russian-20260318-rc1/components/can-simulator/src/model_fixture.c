#include "model_fixture.h"
#include <string.h>

bool model_fixture_parse(const char *bytes, size_t size, struct mcu_model *m, uint8_t *drive, int32_t *initial_acc)
{
    static const char prefix[] = "voyemu-sim-v1 ";
    struct mcu_model parsed = {0};
    unsigned v[8];
    size_t offset = sizeof(prefix) - 1;
    unsigned i;
    if (size != offset + 16 || memcmp(bytes, prefix, offset)) return false;
    for (i = 0; i < 8; ++i) {
        char digit = bytes[offset + 2*i];
        char delimiter = bytes[offset + 2*i + 1];
        if (digit < '0' || digit > '7' || delimiter != (i == 7 ? '\n' : ' ')) return false;
        v[i] = (unsigned)(digit - '0');
    }
    if (v[0] < 1 || v[0] > 7 || v[1] < 1 || v[1] > 2 ||
        v[2] < 1 || v[2] > 7 || v[3] > 3 || v[4] > 3 || v[5] > 3 || v[6] > 1 || (v[7] != 0 && v[7] != 2))
        return false;
    parsed.forced = v[0]; parsed.forced_known = true;
    parsed.warning = v[1]; parsed.warning_known = true;
    parsed.eps = v[3]; parsed.retention = v[4]; parsed.propulsion = v[5];
    parsed.daily_repeat = v[6];
    *m = parsed; *drive = v[2]; *initial_acc = v[7];
    return true;
}
