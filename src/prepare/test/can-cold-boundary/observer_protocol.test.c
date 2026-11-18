#include "observer_protocol.h"
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
static uint32_t word(const uint8_t *v, unsigned i) { uint32_t x; memcpy(&x,v+4*i,4); return ntohl(x); }
int main(void)
{
    struct mcu_model f={.forced_known=true,.warning_known=true,.forced=2,.warning=2};
    struct model_queue m; struct model_oracle o; uint8_t boot[16]={1}, reply[OBSERVER_REPLY_SIZE];
    uint32_t req[6]={htonl(0x4f425331),htonl(1),0,0,0,0};
    model_init(&m,&f,2,2,42); oracle_init(&o,42,boot,1);
    assert(oracle_append(&o,1,100,ORACLE_BOOT,0,2)==ORACLE_OK);
    assert(!observer_protocol(&o,&m,101,(uint8_t *)req,23,reply));
    assert(observer_protocol(&o,&m,101,(uint8_t *)req,24,reply));
    assert(word(reply,0)==0x4f425331 && word(reply,1)==0 && reply[8]==1);
    assert(word(reply,6)==1 && word(reply,8)==42 && word(reply,10)==1);
    assert(word(reply,12)==101 && word(reply,14)==1 && word(reply,16)==1);
    assert(word(reply,26)==2 && word(reply,27)==2 && word(reply,28)==2 && word(reply,31)==100 && word(reply,36)==1);
    req[1]=htonl(2); req[3]=htonl(42); req[5]=htonl(1);
    assert(observer_protocol(&o,&m,102,(uint8_t *)req,24,reply));
    assert(word(reply,1)==0 && word(reply,18)==1 && word(reply,22)==100 && word(reply,23)==ORACLE_BOOT);
    { uint8_t frame[10]={0x71,8,0x10,0,0,0,0,0,0,0};
      assert(oracle_append_frame(&o,1,103,ORACLE_EMIT,frame)==ORACLE_OK);
      req[5]=htonl(2); assert(observer_protocol(&o,&m,104,(uint8_t *)req,24,reply));
      assert(word(reply,23)==ORACLE_EMIT && word(reply,35)==10 && !memcmp(reply+128,frame,10)); }
    req[3]=htonl(43); assert(observer_protocol(&o,&m,102,(uint8_t *)req,24,reply));
    assert(word(reply,1)==0xfffffffe);
    return 0;
}
