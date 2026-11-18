#include "model_queue.h"
#include <string.h>

void model_init(struct model_queue *m, const struct mcu_model *fixture, uint8_t drive, int32_t initial_acc, uint64_t session)
{
    memset(m, 0, sizeof(*m));
    m->state = *fixture; m->fixture = *fixture; m->drive = drive;
    m->session = session; m->epoch = 1; m->revision = 1; m->acc = initial_acc;
}

int model_submit(struct model_queue *m, int32_t state, int32_t value, uint64_t session, uint64_t epoch)
{
    unsigned int tail;
    if (session != m->session || epoch != m->epoch) return MODEL_STALE;
    if (m->admission_closed) return MODEL_INVALID;
    if ((state == VEHICLE_WARNING || state == VEHICLE_FORCED_EV) && m->acc != 2) return MODEL_INVALID;
    if (state == 0 && m->acc == -1 && value != 0) return MODEL_INVALID;
    if ((state == VEHICLE_WARNING && value != 1 && value != 2) ||
        (state == VEHICLE_FORCED_EV && (value < 1 || value > 5)) ||
        (state == 0 && value != 0 && value != 2) ||
        (state != 0 && state != VEHICLE_WARNING && state != VEHICLE_FORCED_EV) ||
        m->count == MODEL_QUEUE_CAPACITY) return MODEL_INVALID;
    tail = (m->head + m->count) % MODEL_QUEUE_CAPACITY;
    m->queue[tail] = (struct semantic_request){ state, value, session, epoch };
    m->count++;
    return MODEL_OK;
}

/* Return 1 for applied, 0 for empty, MODEL_STALE for invalidated, MODEL_FATAL on overflow. */
int model_step(struct model_queue *m, int32_t *changed_state)
{
    struct semantic_request q;
    if (!m->count) return 0;
    q = m->queue[m->head];
    m->head = (m->head + 1) % MODEL_QUEUE_CAPACITY;
    m->count--;
    if (q.session != m->session || q.epoch != m->epoch) return MODEL_STALE;
    if (q.state != 0 && m->acc != 2) return MODEL_STALE;
    if (q.state == 0 && q.value == m->acc) return MODEL_NOOP;
    if (m->revision == UINT64_MAX || (q.state == 0 && q.value == 2 && m->epoch == UINT64_MAX))
        return MODEL_FATAL;
    if (q.state == VEHICLE_WARNING) {
        m->state.warning = q.value; m->state.warning_known = true;
    } else if (q.state == VEHICLE_FORCED_EV) {
        m->state.forced = q.value; m->state.forced_known = true;
    } else {
        m->acc = q.value;
        if (q.value == 2) {
            m->state.warning = m->fixture.warning; m->state.warning_known = m->fixture.warning_known;
            m->state.forced = m->fixture.forced; m->state.forced_known = m->fixture.forced_known;
            m->epoch++;
        }
    }
    m->revision++;
    *changed_state = q.state;
    return 1;
}

int model_source_lost(struct model_queue *m)
{
    if (m->revision == UINT64_MAX || m->epoch == UINT64_MAX) return MODEL_FATAL;
    m->head = 0; m->count = 0; m->acc = -1; m->epoch++; m->revision++;
    return MODEL_OK;
}

int model_apply_raw(struct model_queue *m, const uint8_t frame[10], int32_t *changed_state)
{
    struct mcu_model next = m->state;
    if (m->acc != 2 || !mcu_apply(&next, frame)) return 0;
    if (m->revision == UINT64_MAX) return MODEL_FATAL;
    m->state = next; m->revision++;
    *changed_state = frame[0] == 0x68 ? VEHICLE_FORCED_EV : VEHICLE_WARNING;
    return 1;
}

int32_t model_get(const struct model_queue *m, int32_t state)
{
    if (m->acc != 2) return -1;
    if (state == VEHICLE_WARNING && m->state.warning_known) return m->state.warning;
    if (state == VEHICLE_FORCED_EV && m->state.forced_known) return m->state.forced;
    return -1;
}
