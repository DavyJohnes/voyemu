#define CAN_BOUNDARY_IMPLEMENTATION
#define CAN_BOUNDARY_IOCTL fake_ioctl
static int fake_ioctl(int fd,unsigned long command,void *arg);
#include "boundary_daemon.h"
#include <assert.h>
#include <stdio.h>
static struct can_boundary_state kernel;
static can_u64 now=10, raw, queued;
static int fail_next, wrong_reply;
static int fake_ioctl(int fd,unsigned long command,void *arg) {
    struct can_boundary_control *c=arg;int result=-1;(void)fd;
    if(fail_next){fail_next=0;return -1;}
    assert(command==3&&sizeof(*c)==176);now++;
    switch(c->operation){
    case CAN_BOUNDARY_INIT: result=can_boundary_init(&kernel,c->session,c->epoch,c->mode,now);break;
    case CAN_BOUNDARY_COMMIT: result=raw?-1:can_boundary_commit(&kernel,c->session,c->epoch,c->mode,now);break;
    case CAN_BOUNDARY_COMMIT_RAW:
        result=!raw||c->raw_seq!=raw?-1:can_boundary_commit(&kernel,c->session,c->epoch,c->mode,now);
        if(!result)raw=0;break;
    case CAN_BOUNDARY_WITNESS:
        result=c->nonce&&(c->nonce!=kernel.nonce||c->session!=kernel.nonce_session||c->epoch!=kernel.nonce_epoch)?-1:can_boundary_tick(&kernel,now);break;
    case CAN_BOUNDARY_BEGIN:result=can_boundary_begin(&kernel,c->session,c->epoch,c->nonce,c->deadline_ns,now);break;
    case CAN_BOUNDARY_FINISH:result=can_boundary_finish(&kernel,c->session,c->epoch,c->nonce,queued+c->semantic_pending,raw,now);break;
    case CAN_BOUNDARY_RESET_QUEUE:
        result=can_boundary_tick(&kernel,now);
        if(!result&&(c->session!=kernel.session||c->epoch!=kernel.epoch||can_boundary_is_frozen(&kernel)))result=-1;
        if(!result)queued=raw=0;
        break;
    case CAN_BOUNDARY_ABORT:
        result=c->session!=kernel.nonce_session||c->epoch!=kernel.nonce_epoch?-1:can_boundary_abort(&kernel,c->nonce,now);
        if(!result)queued=raw=0;break;
    }
    if(result)return -1;
    c->flags=1;c->session=kernel.session;c->epoch=kernel.epoch;c->mode=kernel.mode;
    c->open_seq=kernel.open_seq;c->commit_seq=kernel.commit_seq;c->on_count=kernel.on_count;
    c->first_mode=kernel.first_mode;c->first_ns=kernel.first_ns;c->stage=kernel.stage;
    c->nonce=kernel.nonce;c->deadline_ns=kernel.deadline_ns;c->fence_seq=kernel.fence_seq;c->fence_ns=kernel.fence_ns;
    c->queued=queued;c->inflight=raw;c->invalid=kernel.invalid;c->now_ns=now;
    if(wrong_reply){c->mode=3;wrong_reply=0;}
    return 0;
}
int main(void) {
    struct mcu_model fixture={0};struct model_queue m,before;int32_t changed;
    struct can_boundary_control c;struct boundary_request r;
    uint8_t on[10]={0x68,8,0,0,0,0xf0,0x2c,0x50,0,0},ignored[10]={0,8};
    fixture.warning=1;fixture.warning_known=1;fixture.forced=2;fixture.forced_known=1;
    model_init(&m,&fixture,1,2,42);assert(!can_boundary_open(&kernel,now));assert(!boundary_model_init(1,&m));
    assert(!model_submit(&m,957,5,m.session,m.epoch));before=m;fail_next=1;
    assert(boundary_model_step(1,&m,&changed)==MODEL_FATAL&&!memcmp(&m,&before,sizeof(m)));
    assert(boundary_model_step(1,&m,&changed)==1&&model_get(&m,957)==5&&kernel.on_count==1);
    assert(!model_submit(&m,957,2,m.session,m.epoch));assert(boundary_model_step(1,&m,&changed)==1);
    raw=7;assert(boundary_model_raw(1,&m,on,&changed)==1&&raw==0&&kernel.on_count==2&&model_get(&m,957)==5);
    raw=8;assert(boundary_model_raw(1,&m,ignored,&changed)==0&&raw==0&&kernel.on_count==2);
    before=m;assert(boundary_model_raw(1,&m,on,&changed)==MODEL_FATAL&&!memcmp(&m,&before,sizeof(m)));
    assert(!model_submit(&m,957,2,m.session,m.epoch));assert(boundary_model_step(1,&m,&changed)==1);
    r=(struct boundary_request){BOUNDARY_PREPARE,1000,99,m.session,m.epoch};queued=1;
    assert(!boundary_handle(1,&m,&r,now,&c)&&c.stage==CAN_BOUNDARY_QUIESCING&&m.admission_closed);
    assert(model_submit(&m,957,5,m.session,m.epoch)==MODEL_INVALID);
    assert(!boundary_refresh(1,&m)&&kernel.stage==CAN_BOUNDARY_QUIESCING);
    queued=0;raw=9;assert(!boundary_refresh(1,&m)&&kernel.stage==CAN_BOUNDARY_QUIESCING);
    assert(boundary_model_raw(1,&m,ignored,&changed)==0);assert(!boundary_refresh(1,&m)&&kernel.stage==CAN_BOUNDARY_FROZEN);
    assert(kernel.mode==2&&kernel.on_count==2&&kernel.fence_ns);
    assert(boundary_model_reset_queue(1,&m)<0);
    before=m;before.state.forced=5;assert(boundary_model_commit(1,&before,0)<0&&model_get(&m,957)==2);
    r.op=BOUNDARY_ABORT_REQUEST;r.ttl_ms=0;
    assert(!boundary_handle(1,&m,&r,now,&c)&&c.stage==CAN_BOUNDARY_ABORTED&&m.epoch==2&&!m.admission_closed);
    assert(!boundary_handle(1,&m,&r,now,&c)&&m.epoch==2); /* Lost abort lookup/repeat cannot advance again. */
    r.op=BOUNDARY_STATUS;assert(!boundary_handle(1,&m,&r,now,&c)&&c.stage==CAN_BOUNDARY_ABORTED&&c.epoch==2);
    assert(!model_submit(&m,957,5,m.session,m.epoch));wrong_reply=1;before=m;
    assert(boundary_model_step(1,&m,&changed)==MODEL_FATAL&&!memcmp(&m,&before,sizeof(m)));
    memset(&kernel,0,sizeof(kernel));
    model_init(&m,&fixture,1,2,43);
    assert(!can_boundary_open(&kernel,now)&&!boundary_model_init(1,&m));
    assert(!model_submit(&m,957,5,m.session,m.epoch));
    assert(boundary_model_step(1,&m,&changed)==1&&model_get(&m,957)==5&&kernel.on_count==1);
    assert(!model_submit(&m,0,0,m.session,m.epoch));
    assert(boundary_model_step(1,&m,&changed)==1&&changed==0&&m.acc==0&&m.epoch==1);
    assert(model_get(&m,957)==-1&&kernel.mode==5&&kernel.on_count==1);
    queued=2;raw=10;before=m;++before.epoch;
    assert(boundary_model_reset_queue(1,&before)<0&&queued==2&&raw==10);
    assert(!boundary_model_reset_queue(1,&m)&&!queued&&!raw);
    assert(!model_submit(&m,0,2,m.session,m.epoch));
    assert(boundary_model_step(1,&m,&changed)==1&&changed==0&&m.acc==2&&m.epoch==2);
    assert(kernel.epoch==2&&kernel.mode==2&&model_get(&m,957)==2&&kernel.on_count==1);
    assert(!boundary_model_reset_queue(1,&m));
    assert(!boundary_refresh(1,&m));
    puts("boundary client commit/freeze/abort and Forced EV sleep/wake passed");return 0;
}
