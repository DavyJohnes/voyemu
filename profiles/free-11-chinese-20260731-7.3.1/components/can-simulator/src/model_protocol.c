#include "model_protocol.h"
#include <arpa/inet.h>
#include <string.h>
#define MAGIC 0x43414e32u
bool model_protocol(struct model_queue *m, bool source_present, const uint8_t *request, size_t length, uint8_t output[MODEL_REPLY_SIZE])
{
    uint32_t wire[8], reply[12], op;
    int32_t state, value, status = MODEL_INVALID;
    uint64_t session, epoch;
    if (length != sizeof(wire)) return false;
    memcpy(wire, request, sizeof(wire));
    if (ntohl(wire[0]) != MAGIC) return false;
    op = ntohl(wire[1]); state = (int32_t)ntohl(wire[2]); value = (int32_t)ntohl(wire[3]);
    session = ((uint64_t)ntohl(wire[4]) << 32) | ntohl(wire[5]);
    epoch = ((uint64_t)ntohl(wire[6]) << 32) | ntohl(wire[7]);
    if (op == 1 && !state && !value && !session && !epoch) status = MODEL_OK;
    else if (op == 4 && !state && !value && !source_present && !m->admission_closed)
        status = session == m->session && epoch == m->epoch ? MODEL_OK : MODEL_STALE;
    else if (op == 2 && state != 0 && source_present) status = model_submit(m, state, value, session, epoch);
    else if (op == 3 && state == 0 && source_present) status = model_submit(m, state, value, session, epoch);
    reply[0] = htonl(MAGIC); reply[1] = htonl((uint32_t)status);
    reply[2] = htonl((uint32_t)model_get(m, VEHICLE_WARNING));
    reply[3] = htonl((uint32_t)model_get(m, VEHICLE_FORCED_EV));
    reply[4] = htonl(m->acc == 2 ? m->drive : (uint32_t)-1); reply[5] = htonl((uint32_t)m->acc);
    reply[6] = htonl(m->session >> 32); reply[7] = htonl((uint32_t)m->session);
    reply[8] = htonl(m->epoch >> 32); reply[9] = htonl((uint32_t)m->epoch);
    reply[10] = htonl(m->revision >> 32); reply[11] = htonl((uint32_t)m->revision);
    memcpy(output, reply, sizeof(reply));
    return true;
}
