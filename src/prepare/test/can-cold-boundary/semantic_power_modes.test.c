#include "model_queue.h"
#include <assert.h>
#include <limits.h>
#include <string.h>
#ifdef NDEBUG
#error Native model assertions must remain enabled
#endif

int main(void)
{
    const struct mcu_model fixture = { .forced_known=true, .warning_known=true, .forced=2, .warning=2 };
    const int32_t modes[] = {1, 2, 3, 4, 5};
    /* OEM power-mode radios emit 1..4; forced-EV native control additionally uses 5.
     * This semantic setter does not broaden the independent 0x68 native wire codec. */
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        struct model_queue model;
        int32_t changed = -1;
        uint8_t feedback[10];
        model_init(&model, &fixture, 2, 2, 42);
        assert(model_submit(&model, VEHICLE_FORCED_EV, modes[i], 42, 1) == MODEL_OK);
        assert(model_get(&model, VEHICLE_FORCED_EV) == 2);
        assert(model_step(&model, &changed) == 1 && changed == VEHICLE_FORCED_EV);
        assert(model_get(&model, VEHICLE_FORCED_EV) == modes[i] && model.drive == 2);
        assert(mcu_forced_feedback(&model.state, feedback));
        assert(feedback[0] == 0x9a && feedback[1] == 8 && feedback[8] == (modes[i] - 1) << 2);
        assert(model_submit(&model, 0, 0, 42, 1) == MODEL_OK);
        assert(model_step(&model, &changed) == 1);
        assert(model_get(&model, VEHICLE_FORCED_EV) == -1);
        assert(model_submit(&model, VEHICLE_FORCED_EV, modes[i], 42, 1) == MODEL_INVALID);
        assert(model_submit(&model, 0, 2, 42, 1) == MODEL_OK);
        assert(model_step(&model, &changed) == 1 && model.epoch == 2);
        assert(model_get(&model, VEHICLE_FORCED_EV) == 2);
    }
    const int32_t invalid[] = {INT_MIN, -1, 0, 6, INT_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        struct model_queue model;
        model_init(&model, &fixture, 2, 2, 42);
        assert(model_submit(&model, VEHICLE_FORCED_EV, invalid[i], 42, 1) == MODEL_INVALID);
        assert(model.count == 0 && model.revision == 1 && model_get(&model, VEHICLE_FORCED_EV) == 2);
    }
    for (unsigned value = 1; value <= 4; ++value) {
        struct mcu_model model = fixture, before = model;
        uint8_t wire[10] = {0x68, 8, 3, 0, 0, 0xf0, 0x2c, 0x08, 0x20, 0};
        if (value == 2) continue;
        wire[7] |= value << 4;
        assert(!mcu_apply(&model, wire) && !memcmp(&model, &before, sizeof(model)));
    }
    return 0;
}
