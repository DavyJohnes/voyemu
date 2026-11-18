#include "boundary_wire.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 unsigned char req[BOUNDARY_REQUEST_SIZE]={0},reply[BOUNDARY_REPLY_SIZE];
 struct boundary_request r;struct can_boundary_control a={0},b;
 assert(sizeof(a)==176);boundary_put32(req,0,BOUNDARY_WIRE_MAGIC);boundary_put32(req,1,1);boundary_put32(req,2,BOUNDARY_INSPECT);
 assert(!boundary_decode_request(req,sizeof(req),&r));assert(boundary_decode_request(req,sizeof(req)-1,&r));
 boundary_put32(req,2,BOUNDARY_PREPARE);assert(boundary_decode_request(req,sizeof(req),&r));
 boundary_put32(req,3,180000);boundary_put64(req,4,1);boundary_put64(req,6,42);boundary_put64(req,8,1);
 assert(!boundary_decode_request(req,sizeof(req),&r));boundary_put32(req,3,180001);assert(boundary_decode_request(req,sizeof(req),&r));
 boundary_put32(req,2,BOUNDARY_STATUS);boundary_put32(req,3,0);assert(!boundary_decode_request(req,sizeof(req),&r));
 a.magic=CAN_BOUNDARY_MAGIC;a.version=1;a.operation=CAN_BOUNDARY_WITNESS;a.flags=1;a.session=0xfedcba9876543210ULL;
 a.epoch=7;a.mode=2;a.nonce=9;a.stage=CAN_BOUNDARY_FROZEN;a.on_count=0xffffffffffffffffULL;a.first_ns=33;a.now_ns=99;
 boundary_encode_reply(reply,0,&a);assert(!boundary_decode_reply(reply,&b));assert(!memcmp(&a,&b,sizeof(a)));
 boundary_encode_reply(reply,CAN_BOUNDARY_STALE,&a);assert(boundary_decode_reply(reply,&b)==CAN_BOUNDARY_STALE);
 reply[0]^=1;assert(boundary_decode_reply(reply,&b)==-1);
 puts("boundary wire size/endian/stale/malformed negatives passed");return 0;
}
