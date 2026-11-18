#ifndef VOYEMU_BOUNDARY_CLIENT_H
#define VOYEMU_BOUNDARY_CLIENT_H
#include "can_boundary.h"
#include "model_queue.h"
#include <string.h>
#include <sys/ioctl.h>
#ifndef CAN_BOUNDARY_IOCTL
#define CAN_BOUNDARY_IOCTL ioctl
#endif
static inline void boundary_command(struct can_boundary_control *c, can_u32 operation, const struct model_queue *m) {
    memset(c,0,sizeof(*c)); c->magic=CAN_BOUNDARY_MAGIC; c->version=CAN_BOUNDARY_VERSION;
    c->operation=operation;
    if (m) { c->session=m->session; c->epoch=m->epoch; c->mode=m->state.forced_known ? m->state.forced : 0; }
}
static inline int boundary_exchange(int fd, struct can_boundary_control *c) {
    can_u32 op=c->operation;
    if (CAN_BOUNDARY_IOCTL(fd,3,c) || c->magic!=CAN_BOUNDARY_MAGIC || c->version!=CAN_BOUNDARY_VERSION ||
        c->operation!=op || c->flags!=1 || c->invalid || !c->open_seq || !c->commit_seq ||
        c->mode<1 || c->mode>5 || !c->session || !c->epoch || !c->now_ns) return -1;
    return 0;
}
static inline int boundary_model_init(int fd, const struct model_queue *m) {
    struct can_boundary_control c;
    boundary_command(&c,CAN_BOUNDARY_INIT,m);
    return boundary_exchange(fd,&c) || c.session!=m->session || c.epoch!=m->epoch || c.mode!=m->state.forced ? -1 : 0;
}
static inline int boundary_model_commit(int fd, const struct model_queue *m, can_u64 raw) {
    struct can_boundary_control c;
    boundary_command(&c,raw ? CAN_BOUNDARY_COMMIT_RAW : CAN_BOUNDARY_COMMIT,m);
    c.raw_seq=raw;
    return boundary_exchange(fd,&c) || c.session!=m->session || c.epoch!=m->epoch || c.mode!=m->state.forced ? -1 : 0;
}
/* No candidate state is observable before the kernel commits its witness. */
static inline int boundary_model_step(int fd, struct model_queue *m, int32_t *changed) {
    struct model_queue next=*m;
    int result=model_step(&next,changed);
    if (result==MODEL_FATAL || (result==1 && boundary_model_commit(fd,&next,0))) return MODEL_FATAL;
    *m=next; return result;
}
static inline int boundary_model_raw(int fd, struct model_queue *m, const uint8_t frame[10], int32_t *changed) {
    struct model_queue next=*m;
    struct can_boundary_control c;
    int result=model_apply_raw(&next,frame,changed);
    boundary_command(&c,CAN_BOUNDARY_WITNESS,0);
    if (result==MODEL_FATAL || boundary_exchange(fd,&c) || !c.inflight || c.session!=m->session ||
        c.epoch!=m->epoch || boundary_model_commit(fd,&next,c.inflight)) return MODEL_FATAL;
    *m=next; return result;
}
static inline int boundary_model_reset_queue(int fd, const struct model_queue *m) {
    struct can_boundary_control c;
    boundary_command(&c,CAN_BOUNDARY_RESET_QUEUE,m);
    return boundary_exchange(fd,&c) || c.session!=m->session || c.epoch!=m->epoch || c.queued || c.inflight ? -1 : 0;
}
static inline int boundary_model_lost(int fd, struct model_queue *m) {
    struct model_queue next=*m;
    if (model_source_lost(&next) || boundary_model_commit(fd,&next,0)) return -1;
    *m=next; return 0;
}
#endif
