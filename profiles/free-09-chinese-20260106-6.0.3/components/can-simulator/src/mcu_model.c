#include "mcu_model.h"
#include <string.h>

bool mcu_apply(struct mcu_model *m, const uint8_t f[10])
{
    uint8_t value;
    if (f[1] != 8) return false;
    if (f[0] == 0x68) {
        value = (f[7] >> 4) & 7;
        if (f[2] > 3 || f[3] || f[4] || f[5] != 0xf0 || f[6] != 0x2c ||
            (f[7] & 0x83) || (f[8] & 0xcf) || f[9] ||
            (value != 2 && value != 5)) return false;
        m->eps = f[2];
        m->retention = (f[7] >> 2) & 3;
        m->propulsion = (f[8] >> 4) & 3;
        m->forced = value;
        m->forced_known = true;
        return true;
    }
    if (f[0] == 0x6a) {
        if (f[2] > 1 || f[3] || f[4] || f[5] || f[6] ||
            (f[7] != 0x10 && f[7] != 0x20) || f[8] != 0x7c || f[9]) return false;
        m->daily_repeat = f[2];
        m->warning = f[7] == 0x20 ? 2 : 1;
        m->warning_known = true;
        return true;
    }
    return false;
}

bool mcu_forced_feedback(const struct mcu_model *m, uint8_t f[10])
{
    if (!m->forced_known) return false;
    memset(f, 0, 10);
    f[0] = 0x9a; f[1] = 8; f[8] = (m->forced - 1) << 2;
    return true;
}

bool mcu_warning_feedback(const struct mcu_model *m, uint8_t f[10])
{
    if (!m->warning_known) return false;
    memset(f, 0, 10);
    f[0] = 0x71; f[1] = 8; f[2] = m->warning == 2 ? 0x10 : 0;
    return true;
}
