#include "model_queue.h"
#include <assert.h>
#include <limits.h>
int main(void)
{
    struct mcu_model fixture = { .forced_known=true, .warning_known=true, .forced=2, .warning=2 };
    struct model_queue m;
    int32_t changed = -1;
    uint8_t raw[10] = {0x6a,8,1,0,0,0,0,0x10,0x7c,0};
    model_init(&m, &fixture, 2, 2, 42);
    { struct model_queue ordered; model_init(&ordered, &fixture, 2, 2, 44);
      assert(model_submit(&ordered, 0, 0, 44, 1) == MODEL_OK);
      assert(model_submit(&ordered, VEHICLE_WARNING, 1, 44, 1) == MODEL_OK);
      assert(model_step(&ordered, &changed) == 1 && ordered.acc == 0);
      assert(model_step(&ordered, &changed) == MODEL_STALE && ordered.state.warning == 2); }
    { struct model_queue restarted; model_init(&restarted, &fixture, 2, -1, 43);
      assert(restarted.acc == -1 && restarted.epoch == 1 && model_get(&restarted, VEHICLE_WARNING) == -1);
      assert(model_apply_raw(&restarted, raw, &changed) == 0); }
    assert(m.session == 42 && m.epoch == 1 && m.revision == 1 && m.acc == 2);
    assert(model_submit(&m, VEHICLE_WARNING, 1, 43, 1) == MODEL_STALE);
    assert(model_submit(&m, VEHICLE_WARNING, 1, 42, 0) == MODEL_STALE);
    assert(model_submit(&m, VEHICLE_WARNING, 1, 42, 1) == MODEL_OK);
    assert(model_get(&m, VEHICLE_WARNING) == 2 && m.revision == 1);
    assert(model_step(&m, &changed) == 1 && model_get(&m, VEHICLE_WARNING) == 1 && m.revision == 2);
    assert(model_submit(&m, 0, 2, 42, 1) == MODEL_OK);
    assert(model_step(&m, &changed) == MODEL_NOOP && m.epoch == 1 && m.revision == 2);
    assert(model_submit(&m, 0, 0, 42, 1) == MODEL_OK);
    assert(model_submit(&m, 0, 2, 42, 1) == MODEL_OK);
    assert(model_submit(&m, VEHICLE_FORCED_EV, 5, 42, 1) == MODEL_OK);
    assert(model_step(&m, &changed) == 1 && m.acc == 0);
    assert(model_step(&m, &changed) == 1 && m.acc == 2 && m.epoch == 2);
    assert(model_get(&m, VEHICLE_WARNING) == 2 && model_get(&m, VEHICLE_FORCED_EV) == 2);
    assert(model_step(&m, &changed) == MODEL_STALE && model_get(&m, VEHICLE_FORCED_EV) == 2);
    assert(model_apply_raw(&m, raw, &changed) == 1 && model_get(&m, VEHICLE_WARNING) == 1);
    assert(changed == VEHICLE_WARNING);
    raw[8]=0; assert(model_apply_raw(&m, raw, &changed) == 0);
    for (int i = 0; i < MODEL_QUEUE_CAPACITY; i++)
        assert(model_submit(&m, VEHICLE_WARNING, 2, 42, 2) == MODEL_OK);
    assert(model_submit(&m, VEHICLE_WARNING, 2, 42, 2) == MODEL_INVALID);
    while (model_step(&m, &changed) == 1) {}
    assert(model_get(&m, VEHICLE_WARNING) == 2);
    assert(model_source_lost(&m) == MODEL_OK && m.acc == -1 && m.epoch == 3);
    assert(model_get(&m, VEHICLE_WARNING) == -1);
    assert(model_submit(&m, VEHICLE_WARNING, 1, 42, 3) == MODEL_INVALID);
    assert(model_submit(&m, 0, 2, 42, 3) == MODEL_INVALID);
    assert(model_submit(&m, 0, 0, 42, 3) == MODEL_OK);
    assert(model_step(&m, &changed) == 1 && m.acc == 0);
    assert(model_submit(&m, 0, 2, 42, 3) == MODEL_OK);
    assert(model_step(&m, &changed) == 1 && m.acc == 2 && m.epoch == 4);
    assert(model_get(&m, VEHICLE_WARNING) == 2);
    m.revision = UINT64_MAX;
    assert(model_submit(&m, VEHICLE_WARNING, 1, 42, 4) == MODEL_OK);
    assert(model_step(&m, &changed) == MODEL_FATAL);
    return 0;
}
