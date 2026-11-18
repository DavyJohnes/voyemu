#ifndef VOYEMU_BOUNDARY_OBSERVER_H
#define VOYEMU_BOUNDARY_OBSERVER_H
#include "boundary_wire.h"
static int boundary_transfer(const struct boundary_request *r, struct can_boundary_control *c, int64_t end) {
    unsigned char req[BOUNDARY_REQUEST_SIZE]={0},reply[BOUNDARY_REPLY_SIZE];
    struct sockaddr_un addr={0};int fd,err=0; socklen_t length=sizeof(err);ssize_t n;
    boundary_put32(req,0,BOUNDARY_WIRE_MAGIC);boundary_put32(req,1,1);
    boundary_put32(req,2,r->op);boundary_put32(req,3,r->ttl_ms);
    boundary_put64(req,4,r->nonce);boundary_put64(req,6,r->session);boundary_put64(req,8,r->epoch);
    fd=socket(AF_UNIX,SOCK_SEQPACKET,0);if(fd<0)return -1;
    if(fcntl(fd,F_SETFL,O_NONBLOCK)<0)goto fail;
    addr.sun_family=AF_UNIX;memcpy(addr.sun_path,ENDPOINT,sizeof(ENDPOINT));
    if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0&&errno!=EINPROGRESS)goto fail;
    if(ready(fd,POLLOUT,end)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&length)||err)goto fail;
#ifdef MSG_NOSIGNAL
    n=send(fd,req,sizeof(req),MSG_NOSIGNAL);
#else
    n=send(fd,req,sizeof(req),0);
#endif
    if(n!=(ssize_t)sizeof(req)||ready(fd,POLLIN,end))goto fail;
    n=recv(fd,reply,sizeof(reply),MSG_TRUNC);close(fd);
    if(n!=sizeof(reply))return -1;
    return boundary_decode_reply(reply,c);
fail:close(fd);return -1;
}
static const char *boundary_stage(unsigned stage) {
    switch(stage){case CAN_BOUNDARY_NONE:return "none";case CAN_BOUNDARY_QUIESCING:return "quiescing";
    case CAN_BOUNDARY_FROZEN:return "frozen";case CAN_BOUNDARY_ABORTED:return "aborted";
    case CAN_BOUNDARY_EXPIRED:return "expired";default:return "invalid";}
}
static int boundary_boot(char text[37]) {
    char data[37];int fd=open("/proc/sys/kernel/random/boot_id",O_RDONLY|O_CLOEXEC|O_NOFOLLOW);
    ssize_t n;if(fd<0)return -1;n=read(fd,data,sizeof(data));close(fd);if(n!=37||data[36]!='\n')return -1;
    for(int i=0;i<36;i++){int dash=i==8||i==13||i==18||i==23;
        if(dash?data[i]!='-':!((data[i]>='0'&&data[i]<='9')||(data[i]>='a'&&data[i]<='f')))return -1;}
    memcpy(text,data,36);text[36]=0;return 0;
}
static int boundary_valid(int status,const struct can_boundary_control *c) {
    if(status || c->magic!=CAN_BOUNDARY_MAGIC || c->version!=1 || c->operation!=CAN_BOUNDARY_WITNESS ||
       c->flags!=1 || c->invalid || !c->session || !c->epoch || !c->open_seq || !c->commit_seq ||
       c->mode<1 || c->mode>5 || c->first_mode<1 || c->first_mode>5 || !c->first_ns || c->first_ns>c->now_ns ||
       c->stage>CAN_BOUNDARY_EXPIRED || c->queued>32 || c->inflight>c->raw_seq ||
       c->fence_generated_seq>c->generated_seq) return 0;
    if(c->stage==CAN_BOUNDARY_FROZEN && (c->mode!=2 || !c->nonce || !c->fence_seq ||
       c->fence_seq!=c->commit_seq || !c->fence_ns || c->fence_ns>c->now_ns ||
       c->fence_ns>=c->deadline_ns || c->now_ns>=c->deadline_ns || c->queued || c->inflight || c->semantic_pending)) return 0;
    return 1;
}
static void boundary_emit(int status,const struct can_boundary_control *c,const char *boot,int first) {
    int valid=boundary_valid(status,c);
    int frozen=valid&&c->stage==CAN_BOUNDARY_FROZEN&&c->mode==2&&c->queued==0&&c->inflight==0&&c->semantic_pending==0&&c->fence_seq&&c->fence_ns;
    printf("{\"schema\":1,\"producer\":\"closed_ecu_boundary\",\"status\":%d,\"boot_uuid\":\"%s\",\"first_open\":%s,\"valid\":%s,\"all_writers_quiesced\":%s,\"stage\":\"%s\",\"mode\":%u,\"first_mode\":%u",status,boot,first?"true":"false",valid?"true":"false",frozen?"true":"false",boundary_stage(c->stage),c->mode,(unsigned)c->first_mode);
#define DECIMAL(name,field) printf(",\"" name "\":\"%" PRIu64 "\"",(uint64_t)c->field)
    DECIMAL("session",session);DECIMAL("epoch",epoch);DECIMAL("nonce",nonce);
    DECIMAL("kernel_open_seq",open_seq);DECIMAL("commit_seq",commit_seq);DECIMAL("on_transition_count",on_count);
    DECIMAL("first_commit_boottime_ns",first_ns);DECIMAL("fence_seq",fence_seq);DECIMAL("fence_boottime_ns",fence_ns);
    DECIMAL("expires_boottime_ns",deadline_ns);DECIMAL("capture_boottime_ns",now_ns);
    DECIMAL("raw_admission_seq",raw_seq);DECIMAL("raw_queued",queued);DECIMAL("raw_inflight_seq",inflight);
    DECIMAL("loss_count",loss);DECIMAL("generated_rx_seq",generated_seq);DECIMAL("fence_generated_rx_seq",fence_generated_seq);
    printf(",\"semantic_queue\":\"%u\"}\n",c->semantic_pending);fflush(stdout);
#undef DECIMAL
}
static int boundary_program(int argc,char **argv) {
    struct boundary_request r={0};struct can_boundary_control c={0};
    uint64_t value;int64_t start,end;int status=-1,first=0;char boot[37];unsigned char old[148];unsigned seen=0;can_u32 operation;
    if(argc<2)return -99;
    if(!strcmp(argv[1],"boundary"))r.op=BOUNDARY_INSPECT;
    else if(!strcmp(argv[1],"prepare-restart"))r.op=BOUNDARY_PREPARE;
    else if(!strcmp(argv[1],"restart-status"))r.op=BOUNDARY_STATUS;
    else if(!strcmp(argv[1],"abort-restart"))r.op=BOUNDARY_ABORT_REQUEST;
    else return -99;
    for(int i=2;i<argc;i+=2){
        if(i+1>=argc||number(argv[i+1],&value)||(argv[i+1][0]=='0'&&argv[i+1][1]))return error("boundary_arguments");
        unsigned bit;
        if(!strcmp(argv[i],"--session")){bit=1;r.session=value;}
        else if(!strcmp(argv[i],"--epoch")){bit=2;r.epoch=value;}
        else if(!strcmp(argv[i],"--nonce")){bit=4;r.nonce=value;}
        else if(!strcmp(argv[i],"--ttl-ms")&&value<=180000){bit=8;r.ttl_ms=value;}
        else return error("boundary_arguments");
        if(seen&bit)return error("boundary_arguments");seen|=bit;
    }
    if((r.op==BOUNDARY_INSPECT&&argc!=2)||(r.op!=BOUNDARY_INSPECT&&(!r.session||!r.epoch||!r.nonce))||
       (r.op==BOUNDARY_PREPARE?!r.ttl_ms:r.ttl_ms!=0))return error("boundary_arguments");
    if(geteuid()!=0)return error("root_required");
    start=ms();if(start<0||boundary_boot(boot))return error("clock_or_boot_failure");end=start+10000;alarm(12);
    /* Bind the old protocol's actual first-open witness; never invent it. */
    if(exchange(1,0,0,old,end)||!valid(old)||(int32_t)word(old,1)!=0)return error("boundary_source_unavailable");
    first=word(old,6)==1;
    operation=r.op;
    status=boundary_transfer(&r,&c,end);
    if(operation==BOUNDARY_PREPARE){
        struct boundary_request lookup=r;lookup.op=BOUNDARY_STATUS;lookup.ttl_ms=0;
        while(!status&&c.stage==CAN_BOUNDARY_QUIESCING&&c.mode==2){
            int64_t now=ms();struct timespec wait={0,20000000};
            if(now<0||now>=end)break;nanosleep(&wait,0);
            status=boundary_transfer(&lookup,&c,end);
        }
    }
    boundary_emit(status,&c,boot,first);
    if(!boundary_valid(status,&c))return 1;
    if(operation==BOUNDARY_PREPARE)return c.stage==CAN_BOUNDARY_FROZEN&&c.mode==2&&!c.queued&&!c.inflight&&!c.semantic_pending?0:1;
    if(operation==BOUNDARY_ABORT_REQUEST)return c.stage==CAN_BOUNDARY_ABORTED?0:1;
    return 0;
}
#endif
