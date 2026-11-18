#include "model_oracle.h"
#include <string.h>
void oracle_init(struct model_oracle *o, uint64_t session, const uint8_t boot_id[16], int first_open)
{
    memset(o, 0, sizeof(*o)); o->session = session; o->first_open = first_open != 0;
    memcpy(o->boot_id, boot_id, sizeof(o->boot_id));
}
uint64_t oracle_earliest(const struct model_oracle *o)
{
    if (!o->latest) return 0;
    return o->latest > ORACLE_CAPACITY ? o->latest - ORACLE_CAPACITY + 1 : 1;
}
int oracle_append(struct model_oracle *o, uint64_t epoch, uint64_t ns, int32_t kind, int32_t state, int32_t value)
{
    struct oracle_event *e;
    if (!o->session || !epoch || !ns || ns < o->last_ns || o->latest == UINT64_MAX ||
        kind < ORACLE_BOOT || kind > ORACLE_NOOP) return ORACLE_INVALID;
    o->latest++;
    e = &o->events[(o->latest-1) % ORACLE_CAPACITY];
    memset(e, 0, sizeof(*e));
    e->session=o->session; e->epoch=epoch; e->seq=o->latest; e->boottime_ns=ns;
    e->kind=kind; e->state=state; e->value=value;
    o->last_ns = ns;
    if (kind == ORACLE_BOOT || kind == ORACLE_STATE || kind == ORACLE_LOSS) o->state_ns = ns;
    return ORACLE_OK;
}
int oracle_append_frame(struct model_oracle *o, uint64_t epoch, uint64_t ns, int32_t kind, const uint8_t frame[10])
{
    int result;
    struct oracle_event *e;
    if (kind != ORACLE_RAW_READ && kind != ORACLE_EMIT) return ORACLE_INVALID;
    result = oracle_append(o, epoch, ns, kind, frame[0], 0);
    if (result != ORACLE_OK) return result;
    e = &o->events[(o->latest-1) % ORACLE_CAPACITY];
    memcpy(e->frame, frame, 10); e->frame_length = 10;
    return ORACLE_OK;
}
int oracle_read(const struct model_oracle *o, uint64_t session, uint64_t seq, struct oracle_event *event)
{
    uint64_t first = oracle_earliest(o);
    if (!session || !seq) return ORACLE_INVALID;
    if (session != o->session || (first && seq < first)) return ORACLE_GAP;
    if (seq > o->latest) return ORACLE_UNKNOWN;
    *event = o->events[(seq-1) % ORACLE_CAPACITY];
    return ORACLE_OK;
}
