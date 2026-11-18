#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#define MAGIC 0x4f425331u
#define ENDPOINT "/dev/socket/voyemu_can_observe"
static uint32_t word(const unsigned char *p, int n) { uint32_t x; memcpy(&x,p+4*n,4); return ntohl(x); }
static uint64_t pair(const unsigned char *p, int n) { return (uint64_t)word(p,n)<<32 | word(p,n+1); }
static void put(unsigned char *p,int n,uint32_t x) { x=htonl(x); memcpy(p+4*n,&x,4); }
static int64_t ms(void) { struct timespec t; if(clock_gettime(CLOCK_MONOTONIC,&t)) return -1; return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000; }
static int ready(int fd, short events, int64_t end) { struct pollfd p={fd,events,0}; for (;;) { int64_t now=ms(); if(now<0 || now>=end) return -1; int64_t left=end-now; int n=poll(&p,1,left>1000?1000:(int)left); if(n<0 && errno==EINTR) continue; return n==1 && (p.revents&events) ? 0 : -1; } }
static int exchange(unsigned op,uint64_t session,uint64_t cursor,unsigned char *reply,int64_t deadline) {
 unsigned char req[24]={0}; struct sockaddr_un addr={0}; int fd,err=0; socklen_t elen=sizeof(err); ssize_t n; int64_t now=ms(); if(now<0 || now>=deadline) return -1; int64_t end=now+1000; if(end>deadline) end=deadline;
 put(req,0,MAGIC); put(req,1,op); put(req,2,session>>32); put(req,3,session); put(req,4,cursor>>32); put(req,5,cursor);
 fd=socket(AF_UNIX,SOCK_SEQPACKET,0); if(fd<0) return -1; if(fcntl(fd,F_SETFL,O_NONBLOCK)<0) goto fail;
 addr.sun_family=AF_UNIX; memcpy(addr.sun_path,ENDPOINT,sizeof(ENDPOINT));
 if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))<0 && errno!=EINPROGRESS) goto fail;
 if(ready(fd,POLLOUT,end)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&elen)||err) goto fail;
 #ifdef MSG_NOSIGNAL
 n=send(fd,req,sizeof(req),MSG_NOSIGNAL);
#else
 n=send(fd,req,sizeof(req),0);
#endif
 if(n!=(ssize_t)sizeof(req)) goto fail;
 if(ready(fd,POLLIN,end)) goto fail;
 n=recv(fd,reply,148,MSG_TRUNC); close(fd); return n==148 ? 0 : -1;
fail: close(fd); return -1;
}
static int number(const char *s,uint64_t *v) { char *e; unsigned long long x; if(!*s||*s=='-'||*s=='+') return -1; for(const char *p=s;*p;p++) if(*p<'0'||*p>'9') return -1; errno=0; x=strtoull(s,&e,10); if(errno||*e) return -1; *v=x; return 0; }
static void uuid(const unsigned char *p,char *s) { snprintf(s,37,"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",p[8],p[9],p[10],p[11],p[12],p[13],p[14],p[15],p[16],p[17],p[18],p[19],p[20],p[21],p[22],p[23]); }
static int valid(const unsigned char *p) { uint64_t earliest=pair(p,13),latest=pair(p,15); unsigned nonzero=0; for(int i=8;i<24;i++) nonzero|=p[i]; return word(p,0)==MAGIC && nonzero && pair(p,7) && pair(p,11) && word(p,35)<=10 && ((latest==0&&earliest==0)||(earliest>0&&earliest<=latest)); }
static void emit(const char *type,const unsigned char *p) { char id[37]; uuid(p,id); printf("{\"type\":\"%s\",\"status\":%d,\"boot_uuid\":\"%s\",\"first_open\":%u,\"session\":\"%" PRIu64 "\",\"epoch\":\"%" PRIu64 "\",\"capture_boottime_ns\":\"%" PRIu64 "\",\"earliest\":\"%" PRIu64 "\",\"latest\":\"%" PRIu64 "\",\"seq\":\"%" PRIu64 "\",\"event_epoch\":\"%" PRIu64 "\",\"event_boottime_ns\":\"%" PRIu64 "\",\"kind\":%u,\"state\":%d,\"value\":%d,\"acc\":%d,\"warning\":%d,\"forced_ev\":%d,\"drive\":%d,\"state_boottime_ns\":\"%" PRIu64 "\",\"frame_hex\":\"",type,(int32_t)word(p,1),id,word(p,6),pair(p,7),pair(p,9),pair(p,11),pair(p,13),pair(p,15),pair(p,17),pair(p,19),pair(p,21),word(p,23),(int32_t)word(p,24),(int32_t)word(p,25),(int32_t)word(p,26),(int32_t)word(p,27),(int32_t)word(p,28),(int32_t)word(p,29),pair(p,30)); for(unsigned i=0;i<word(p,35);i++) printf("%02x",p[128+i]); printf("\",\"flags\":%u}\n",word(p,36)); fflush(stdout); }
static int error(const char *s) { printf("{\"type\":\"error\",\"reason\":\"%s\",\"evidence\":\"partial\"}\n",s); fflush(stdout); return 1; }
#include "boundary_observer.h"
int main(int argc,char **argv) {
 uint64_t session=0,cursor=0,limit=256,seconds=10; int has_session=0,has_cursor=0,has_limit=0,has_seconds=0,mode=0; unsigned char p[148],boot[16]; int64_t deadline; uint64_t target=0,count=0; int incomplete=0;
 setvbuf(stdout,NULL,_IOLBF,0);
 int boundary_result=boundary_program(argc,argv);if(boundary_result!=-99)return boundary_result;
 if(argc<2) goto usage; if(!strcmp(argv[1],"snapshot")) mode=1; else if(!strcmp(argv[1],"drain")) mode=2; else if(!strcmp(argv[1],"follow")) mode=3; else goto usage;
 for(int i=2;i<argc;i+=2) { uint64_t v; if(i+1>=argc||number(argv[i+1],&v)) goto usage; if(!strcmp(argv[i],"--session")&&!has_session) session=v,has_session=1; else if(!strcmp(argv[i],"--cursor")&&!has_cursor) cursor=v,has_cursor=1; else if(!strcmp(argv[i],"--limit")&&!has_limit) limit=v,has_limit=1; else if(!strcmp(argv[i],"--seconds")&&!has_seconds) seconds=v,has_seconds=1; else goto usage; }
 if((has_session!=has_cursor)||(has_session&&(!session||!cursor))||!limit||limit>10000||!seconds||seconds>600||(mode==1&&argc!=2)||(mode==2&&has_seconds)) goto usage;
 if(geteuid()!=0) return error("root_required"); int64_t start=ms(); if(start<0) return error("clock_failure"); deadline=start+(int64_t)seconds*1000; alarm((unsigned)seconds+2); if(exchange(1,0,0,p,deadline)) return error("endpoint_failure"); if(!valid(p)||(int32_t)word(p,1)!=0||pair(p,17)) return error("malformed_snapshot"); memcpy(boot,p+8,16); emit("snapshot",p);
 if(mode==1) return 0;
 if(has_session && session!=pair(p,7)) return error("restart_or_session_mismatch"); session=pair(p,7); if(!has_cursor) { cursor=pair(p,13); if(cursor>1) { puts("{\"type\":\"gap\",\"reason\":\"beginning_not_retained\",\"evidence\":\"partial\"}"); incomplete=1; } if(!cursor) cursor=1; }
 target=pair(p,15);
 if(has_cursor && cursor>target && cursor-target>1) target=cursor;
 while(count<limit) { int64_t now=ms(); if(now<0) return error("clock_failure"); if(now>=deadline) break;
  if(cursor>target) { if(mode==2) break; int64_t now=ms(); if(now<0) return error("clock_failure"); if(now>=deadline) break; int64_t wait=deadline-now; if(wait>100) wait=100; struct timespec pause={wait/1000,(wait%1000)*1000000}; nanosleep(&pause,NULL); now=ms(); if(now<0) return error("clock_failure"); if(now>=deadline) break; if(exchange(1,0,0,p,deadline)) { now=ms(); if(now<0) return error("clock_failure"); if(now>=deadline) return error("time_budget_pending_snapshot"); return error("endpoint_failure"); } if(!valid(p)||(int32_t)word(p,1)!=0||pair(p,17)) return error("malformed_snapshot"); if(memcmp(boot,p+8,16)||pair(p,7)!=session) return error("restart"); target=pair(p,15); continue; }
  if(exchange(2,session,cursor,p,deadline)) { int64_t now=ms(); if(now<0) return error("clock_failure"); return error(now>=deadline?"time_budget_pending_event":"endpoint_failure"); } if(!valid(p)) return error("malformed_reply"); if(memcmp(boot,p+8,16)||pair(p,7)!=session) return error("restart"); int32_t status=(int32_t)word(p,1); if(status==-2) { emit("gap",p); return error("gap"); } if(status==-3) { emit("unknown",p); return error("unknown"); } if(status!=0||pair(p,17)!=cursor||!pair(p,19)||!pair(p,21)||word(p,23)<1||word(p,23)>8) return error("malformed_event"); emit("event",p); if(word(p,23)==6||word(p,23)==7) incomplete=1; count++; if(cursor==UINT64_MAX) return error("cursor_overflow"); cursor++;
 }
 if(cursor<=target || (mode==3 && count==limit)) incomplete=1; printf("{\"type\":\"end\",\"session\":\"%" PRIu64 "\",\"next_cursor\":\"%" PRIu64 "\",\"events\":\"%" PRIu64 "\",\"evidence\":\"%s\"}\n",session,cursor,count,incomplete?"partial":"bounded_range_only"); return incomplete?1:0;
usage: fprintf(stderr,"usage: observer snapshot | drain [--session N --cursor N] [--limit N] | follow [--session N --cursor N] [--limit N] [--seconds N]\n"); return 2;
}
