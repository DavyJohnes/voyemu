#include "model_protocol.h"
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>
static uint32_t word(const uint8_t *reply, unsigned offset)
{
    uint32_t v; memcpy(&v, reply + 4 * offset, sizeof(v)); return ntohl(v);
}
static void prepare(uint32_t *req, uint32_t op, uint32_t state, uint32_t value, uint64_t session, uint64_t epoch)
{
    req[0]=htonl(0x43414e32); req[1]=htonl(op); req[2]=htonl(state); req[3]=htonl(value);
    req[4]=htonl(session>>32); req[5]=htonl((uint32_t)session);
    req[6]=htonl(epoch>>32); req[7]=htonl((uint32_t)epoch);
}
int main(void)
{
    struct mcu_model fixture={.forced_known=true,.warning_known=true,.forced=2,.warning=2};
    struct model_queue m; uint32_t req[8]; uint8_t reply[MODEL_REPLY_SIZE]; int32_t changed;
    model_init(&m,&fixture,2,2,0x123456789abcdef0ULL);
    prepare(req,1,0,0,0,0);
    assert(!model_protocol(&m,true,(uint8_t *)req,31,reply));
    assert(model_protocol(&m,true,(uint8_t *)req,32,reply));
    assert(word(reply,0)==0x43414e32 && word(reply,1)==0 && word(reply,2)==2 && word(reply,3)==2);
    assert(word(reply,5)==2 && word(reply,6)==0x12345678 && word(reply,7)==0x9abcdef0 && word(reply,9)==1 && word(reply,11)==1);
    prepare(req,2,VEHICLE_WARNING,1,m.session,0);
    assert(model_protocol(&m,true,(uint8_t *)req,32,reply) && word(reply,1)==0xfffffffe && m.count==0);
    prepare(req,2,VEHICLE_WARNING,1,m.session,m.epoch);
    assert(model_protocol(&m,true,(uint8_t *)req,32,reply) && word(reply,1)==0 && word(reply,2)==2);
    assert(model_step(&m,&changed)==1 && model_get(&m,VEHICLE_WARNING)==1);
    prepare(req,1,0,0,0,0); assert(model_protocol(&m,true,(uint8_t *)req,32,reply));
    assert(word(reply,2)==1 && word(reply,11)==2);
    assert(model_source_lost(&m) == MODEL_OK);
    prepare(req,1,0,0,0,0); assert(model_protocol(&m,false,(uint8_t *)req,32,reply));
    assert(word(reply,2)==0xffffffff && word(reply,3)==0xffffffff && word(reply,4)==0xffffffff && word(reply,5)==0xffffffff);
    prepare(req,4,0,0,m.session,m.epoch); assert(model_protocol(&m,false,(uint8_t *)req,32,reply) && word(reply,1)==0);
    assert(model_protocol(&m,true,(uint8_t *)req,32,reply) && word(reply,1)==0xffffffff);
    req[0]=0; assert(!model_protocol(&m,true,(uint8_t *)req,32,reply));
    return 0;
}
