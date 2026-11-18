#include "model_oracle.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    struct model_oracle o; struct oracle_event e; uint8_t boot[16] = {1};
    oracle_init(&o, 42, boot, 1);
    assert(o.first_open && !memcmp(o.boot_id,boot,16) && !oracle_earliest(&o));
    assert(oracle_read(&o,42,1,&e)==ORACLE_UNKNOWN);
    assert(oracle_append(&o,1,10,ORACLE_BOOT,0,2)==ORACLE_OK);
    assert(oracle_append(&o,1,11,ORACLE_ADMISSION,665,1)==ORACLE_OK);
    assert(oracle_append(&o,1,12,ORACLE_STATE,665,1)==ORACLE_OK);
    assert(oracle_append(&o,1,13,ORACLE_STATE,665,2)==ORACLE_OK);
    assert(oracle_read(&o,42,2,&e)==ORACLE_OK && e.kind==ORACLE_ADMISSION && e.value==1);
    assert(oracle_read(&o,42,3,&e)==ORACLE_OK && e.kind==ORACLE_STATE && e.value==1);
    assert(oracle_read(&o,42,4,&e)==ORACLE_OK && e.kind==ORACLE_STATE && e.value==2);
    assert(oracle_append(&o,1,12,ORACLE_STATE,665,1)==ORACLE_INVALID);
    for (uint64_t i=5; i<=300; ++i) assert(oracle_append(&o,2,10+i,ORACLE_EMIT,665,2)==ORACLE_OK);
    assert(oracle_earliest(&o)==45 && oracle_read(&o,42,44,&e)==ORACLE_GAP);
    assert(oracle_read(&o,42,45,&e)==ORACLE_OK && e.seq==45 && e.epoch==2);
    assert(oracle_read(&o,43,300,&e)==ORACLE_GAP);
    assert(oracle_read(&o,42,301,&e)==ORACLE_UNKNOWN);
    oracle_init(&o,43,boot,0);
    assert(!o.first_open && oracle_read(&o,42,1,&e)==ORACLE_GAP);
    return 0;
}
