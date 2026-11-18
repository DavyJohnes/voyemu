#include "observer_protocol.h"
#include <arpa/inet.h>
#include <string.h>
#define OBSERVER_MAGIC 0x4f425331u
static void put64(uint32_t *words, unsigned index, uint64_t value)
{
    words[index] = htonl(value >> 32); words[index+1] = htonl((uint32_t)value);
}
bool observer_protocol(const struct model_oracle *o, const struct model_queue *m,
                       uint64_t now, const uint8_t *request, size_t length,
                       uint8_t output[OBSERVER_REPLY_SIZE])
{
    uint32_t in[6], out[37] = {0}; struct oracle_event event = {0};
    uint64_t session, cursor; int status = ORACLE_INVALID;
    if (length != sizeof(in) || !now) return false;
    memcpy(in, request, sizeof(in));
    if (ntohl(in[0]) != OBSERVER_MAGIC) return false;
    session = ((uint64_t)ntohl(in[2]) << 32) | ntohl(in[3]);
    cursor = ((uint64_t)ntohl(in[4]) << 32) | ntohl(in[5]);
    if (ntohl(in[1]) == 1 && !session && !cursor) status = ORACLE_OK;
    else if (ntohl(in[1]) == 2) status = oracle_read(o, session, cursor, &event);
    out[0]=htonl(OBSERVER_MAGIC); out[1]=htonl((uint32_t)status);
    memcpy((uint8_t *)out+8, o->boot_id, sizeof(o->boot_id));
    out[6]=htonl(o->first_open); put64(out,7,o->session); put64(out,9,m->epoch);
    put64(out,11,now); put64(out,13,oracle_earliest(o)); put64(out,15,o->latest);
    put64(out,17,event.seq); put64(out,19,event.epoch); put64(out,21,event.boottime_ns);
    out[23]=htonl((uint32_t)event.kind); out[24]=htonl((uint32_t)event.state);
    out[25]=htonl((uint32_t)event.value); out[26]=htonl((uint32_t)m->acc);
    out[27]=htonl((uint32_t)model_get(m,VEHICLE_WARNING));
    out[28]=htonl((uint32_t)model_get(m,VEHICLE_FORCED_EV));
    out[29]=htonl(m->acc == 2 ? m->drive : (uint32_t)-1); put64(out,30,o->state_ns);
    memcpy((uint8_t *)out+128,event.frame,10); out[35]=htonl(event.frame_length);
    /* Raw ioctl admissions before model read are not observable here. */
    out[36]=htonl(1);
    memcpy(output,out,sizeof(out)); return true;
}
