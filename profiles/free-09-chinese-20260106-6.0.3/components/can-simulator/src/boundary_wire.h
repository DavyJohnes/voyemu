#ifndef VOYEMU_BOUNDARY_WIRE_H
#define VOYEMU_BOUNDARY_WIRE_H
#include "can_boundary.h"
#include <arpa/inet.h>
#include <string.h>
#define BOUNDARY_WIRE_MAGIC 0x43425231u
#define BOUNDARY_REQUEST_SIZE 40
#define BOUNDARY_REPLY_SIZE 184
#define BOUNDARY_INSPECT 1u
#define BOUNDARY_PREPARE 2u
#define BOUNDARY_STATUS 3u
#define BOUNDARY_ABORT_REQUEST 4u
struct boundary_request { can_u32 op,ttl_ms;can_u64 nonce,session,epoch; };
static inline can_u32 boundary_word(const unsigned char *p,unsigned n) {can_u32 x;memcpy(&x,p+4*n,4);return ntohl(x);}
static inline can_u64 boundary_pair(const unsigned char *p,unsigned n) {return (can_u64)boundary_word(p,n)<<32|boundary_word(p,n+1);}
static inline void boundary_put32(unsigned char *p,unsigned n,can_u32 x) {x=htonl(x);memcpy(p+4*n,&x,4);}
static inline void boundary_put64(unsigned char *p,unsigned n,can_u64 x) {boundary_put32(p,n,x>>32);boundary_put32(p,n+1,x);}
static inline int boundary_decode_request(const unsigned char *p,size_t n,struct boundary_request *r) {
    if(n!=BOUNDARY_REQUEST_SIZE||boundary_word(p,0)!=BOUNDARY_WIRE_MAGIC||boundary_word(p,1)!=1)return -1;
    r->op=boundary_word(p,2);r->ttl_ms=boundary_word(p,3);r->nonce=boundary_pair(p,4);r->session=boundary_pair(p,6);r->epoch=boundary_pair(p,8);
    if(r->op==BOUNDARY_INSPECT)return r->ttl_ms||r->nonce||r->session||r->epoch?-1:0;
    if(r->op<BOUNDARY_PREPARE||r->op>BOUNDARY_ABORT_REQUEST||!r->nonce||!r->session||!r->epoch)return -1;
    return r->op==BOUNDARY_PREPARE?(r->ttl_ms<1||r->ttl_ms>180000?-1:0):(r->ttl_ms?-1:0);
}
#define BOUNDARY_FIELDS64(X) X(session,4) X(epoch,6) X(nonce,8) X(deadline_ns,10) X(raw_seq,12) X(open_seq,16) X(commit_seq,18) X(on_count,20) X(first_mode,22) X(first_ns,24) X(fence_seq,26) X(fence_ns,28) X(queued,30) X(inflight,32) X(loss,34) X(generated_seq,36) X(now_ns,38) X(fence_generated_seq,40)
#define BOUNDARY_FIELDS32(X) X(magic,0) X(version,1) X(operation,2) X(flags,3) X(mode,14) X(semantic_pending,15) X(stage,42) X(invalid,43)
static inline void boundary_encode_reply(unsigned char p[BOUNDARY_REPLY_SIZE],int status,const struct can_boundary_control *c) {
    memset(p,0,BOUNDARY_REPLY_SIZE);boundary_put32(p,0,BOUNDARY_WIRE_MAGIC);boundary_put32(p,1,(can_u32)status);
#define PUT64(name,n) boundary_put64(p+8,n,c->name);
#define PUT32(name,n) boundary_put32(p+8,n,c->name);
    BOUNDARY_FIELDS64(PUT64) BOUNDARY_FIELDS32(PUT32)
#undef PUT64
#undef PUT32
}
static inline int boundary_decode_reply(const unsigned char p[BOUNDARY_REPLY_SIZE],struct can_boundary_control *c) {
    if(boundary_word(p,0)!=BOUNDARY_WIRE_MAGIC)return -1;
    memset(c,0,sizeof(*c));
#define GET64(name,n) c->name=boundary_pair(p+8,n);
#define GET32(name,n) c->name=boundary_word(p+8,n);
    BOUNDARY_FIELDS64(GET64) BOUNDARY_FIELDS32(GET32)
#undef GET64
#undef GET32
    return (int32_t)boundary_word(p,1);
}
#endif
