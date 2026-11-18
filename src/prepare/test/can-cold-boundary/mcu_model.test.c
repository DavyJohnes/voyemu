#include "mcu_model.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    struct mcu_model m = {0}, before;
    uint8_t out[10];
    uint8_t ev[10] = {0x68,8,3,0,0,0xf0,0x2c,0x58,0x20,0};
    uint8_t pw[10] = {0x6a,8,1,0,0,0,0,0x20,0x7c,0};
    assert(!mcu_forced_feedback(&m, out) && !mcu_warning_feedback(&m, out));
    assert(mcu_apply(&m, ev));
    assert(m.forced == 5 && m.eps == 3 && m.retention == 2 && m.propulsion == 2);
    assert(mcu_forced_feedback(&m, out) && out[0] == 0x9a && out[8] == 16);
    assert(!mcu_warning_feedback(&m, out));
    assert(mcu_apply(&m, pw));
    assert(m.warning == 2 && m.daily_repeat == 1);
    assert(mcu_warning_feedback(&m, out) && out[0] == 0x71 && out[2] == 16);
    before = m; pw[8] = 0;
    assert(!mcu_apply(&m, pw) && !memcmp(&m, &before, sizeof(m)));
    ev[7] = 0x28;
    assert(mcu_apply(&m, ev) && m.forced == 2 && m.retention == 2);
    assert(mcu_forced_feedback(&m, out) && out[8] == 4);
    return 0;
}
