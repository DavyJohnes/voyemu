#ifndef VOYEMU_BOUNDARY_DAEMON_H
#define VOYEMU_BOUNDARY_DAEMON_H
#include "boundary_client.h"
#include "boundary_wire.h"
/* Called only on the single model owner loop. */
static int boundary_refresh(int device, struct model_queue *model) {
    struct can_boundary_control c;
    boundary_command(&c,CAN_BOUNDARY_WITNESS,0);
    if (boundary_exchange(device,&c) || c.session!=model->session || c.epoch!=model->epoch) return -1;
    model->admission_closed=c.stage==CAN_BOUNDARY_QUIESCING || c.stage==CAN_BOUNDARY_FROZEN || c.stage==CAN_BOUNDARY_EXPIRED;
    if (c.stage==CAN_BOUNDARY_QUIESCING && !model->count && !c.queued && !c.inflight) {
        if (c.mode!=2) return 0; /* Remains explicitly pending, never fabricates OFF. */
        boundary_command(&c,CAN_BOUNDARY_FINISH,model);
        /* Read the immutable token separately; command() resets the response. */
        {
            struct can_boundary_control current;
            boundary_command(&current,CAN_BOUNDARY_WITNESS,0);
            if (boundary_exchange(device,&current)) return -1;
            c.nonce=current.nonce;
        }
        c.semantic_pending=model->count;
        if (boundary_exchange(device,&c)) {
            boundary_command(&c,CAN_BOUNDARY_WITNESS,0);
            if (boundary_exchange(device,&c) || c.stage!=CAN_BOUNDARY_EXPIRED) return -1;
            model->admission_closed=1;
        }
    }
    return 0;
}
static int boundary_handle(int device, struct model_queue *model,
                           const struct boundary_request *r, can_u64 now,
                           struct can_boundary_control *reply) {
    struct can_boundary_control c;
    int result=0;
    boundary_command(&c,CAN_BOUNDARY_WITNESS,0);
    if (r->op==BOUNDARY_STATUS || r->op==BOUNDARY_ABORT_REQUEST) {
        c.nonce=r->nonce;c.session=r->session;c.epoch=r->epoch;
    }
    if (boundary_exchange(device,&c)) return -1;
    if (r->op==BOUNDARY_PREPARE && (r->session!=model->session || r->epoch!=model->epoch)) return CAN_BOUNDARY_STALE;
    if (r->op==BOUNDARY_PREPARE) {
        if (model->count) return CAN_BOUNDARY_BUSY;
        if (c.nonce==r->nonce) {
            if (c.stage!=CAN_BOUNDARY_QUIESCING && c.stage!=CAN_BOUNDARY_FROZEN) return CAN_BOUNDARY_STALE;
        } else {
            if (!now || (can_u64)r->ttl_ms*1000000ULL>~(can_u64)0-now) return CAN_BOUNDARY_INVALID;
            boundary_command(&c,CAN_BOUNDARY_BEGIN,model);c.nonce=r->nonce;
            c.deadline_ns=now+(can_u64)r->ttl_ms*1000000ULL;
            if (boundary_exchange(device,&c)) return -1;
        }
        model->admission_closed=1;
    } else if (r->op==BOUNDARY_ABORT_REQUEST && c.stage!=CAN_BOUNDARY_ABORTED) {
        struct model_queue restored=*model;
        if (restored.epoch==UINT64_MAX || restored.revision==UINT64_MAX) return MODEL_FATAL;
        restored.head=0; restored.count=0; restored.admission_closed=0;
        ++restored.epoch; ++restored.revision;
        boundary_command(&c,CAN_BOUNDARY_ABORT,model);c.nonce=r->nonce;c.session=r->session;c.epoch=r->epoch;
        if (boundary_exchange(device,&c) || boundary_model_commit(device,&restored,0)) return -1;
        *model=restored;
    } else if (r->op==BOUNDARY_STATUS && c.nonce!=r->nonce) return CAN_BOUNDARY_STALE;
    if (boundary_refresh(device,model)) return -1;
    boundary_command(&c,CAN_BOUNDARY_WITNESS,0);
    if (boundary_exchange(device,&c)) result=-1;
    c.semantic_pending=model->count;
    *reply=c;
    return result;
}
#endif
