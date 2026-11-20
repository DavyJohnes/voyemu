#ifndef VOYEMU_DEV_MODEL_QUEUE_H
#define VOYEMU_DEV_MODEL_QUEUE_H
#include "mcu_model.h"
#include <stdbool.h>
#include <stdint.h>
#define MODEL_QUEUE_CAPACITY 32
#define VEHICLE_WARNING 665
#define VEHICLE_FORCED_EV 957
#define MODEL_OK 0
#define MODEL_NOOP 2
#define MODEL_INVALID -1
#define MODEL_STALE -2
#define MODEL_FATAL -3
struct semantic_request { int32_t state, value; uint64_t session, epoch; };
struct model_queue {
    struct mcu_model state, fixture;
    struct semantic_request queue[MODEL_QUEUE_CAPACITY];
    unsigned int head, count;
    uint64_t session, epoch, revision;
    uint8_t drive;
    int32_t acc;
    bool admission_closed;
};
void model_init(struct model_queue *model, const struct mcu_model *fixture, uint8_t drive, int32_t initial_acc, uint64_t session);
int model_submit(struct model_queue *model, int32_t state, int32_t value, uint64_t session, uint64_t epoch);
int model_step(struct model_queue *model, int32_t *changed_state);
int model_source_lost(struct model_queue *model);
int model_apply_raw(struct model_queue *model, const uint8_t frame[10], int32_t *changed_state);
int32_t model_get(const struct model_queue *model, int32_t state);
#endif
