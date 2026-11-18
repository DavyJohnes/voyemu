/* Host transport seam: production model decoder/queue/MCU logic, no kernel claim. */
#include "model_protocol.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void snapshot(struct model_queue *model, unsigned char reply[MODEL_REPLY_SIZE]) {
    const uint32_t request[8] = {htonl(0x43414e32),htonl(1),0,0,0,0,0,0};
    if (!model_protocol(model, false, (const uint8_t *)request, sizeof(request), reply)) _Exit(3);
}
int main(void) {
    struct mcu_model fixture = {.forced_known=true,.warning_known=true,.forced=2,.warning=2};
    struct model_queue model;
    unsigned char action, request[32], reply[MODEL_REPLY_SIZE];
    bool leased=false;
    model_init(&model,&fixture,1,2,UINT64_C(0x123456789abcdef));
    while (fread(&action,1,1,stdin)==1) {
        if (fread(request,1,sizeof(request),stdin)!=sizeof(request)) return 2;
        if (action==0) {
            if (!model_protocol(&model,leased,request,sizeof(request),reply)) return 3;
            uint32_t op,status;memcpy(&op,request+4,4);memcpy(&status,reply+4,4);
            if (ntohl(op)==4 && ntohl(status)==0) leased=true;
            int32_t changed;while(model.count)if(model_step(&model,&changed)==MODEL_FATAL)return 4;
        } else if (action==1) {
            int32_t changed;
            if (model_apply_raw(&model,request,&changed)!=1) return 5;
            snapshot(&model,reply);
        } else if (action==2) {
            if(model_source_lost(&model)!=MODEL_OK)return 6;
            leased=false;snapshot(&model,reply);
        } else return 7;
        if(fwrite(reply,1,sizeof(reply),stdout)!=sizeof(reply)||fflush(stdout))return 8;
    }
    return ferror(stdin)?9:0;
}
