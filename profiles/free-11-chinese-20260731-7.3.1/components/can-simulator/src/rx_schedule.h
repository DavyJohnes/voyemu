#ifndef VOYEMU_DEV_RX_SCHEDULE_H
#define VOYEMU_DEV_RX_SCHEDULE_H
/* A9 protocol-21 RX as observed on the car: ACC (0x6a) and its SOC feedback (0x9a) arrive when
 * the value changes, never as a periodic stream; the same multicast group also carries long
 * service datagrams (7424 bytes, starting 8f 1c) whose payload schema is unknown. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RX_SERVICE_DATAGRAM_SIZE 7424
#define RX_SERVICE_PERIOD_MS 45000

/* True once per change of the known ACC value; *delivered keeps the last value sent. */
static inline bool rx_acc_due(int32_t *delivered, int32_t acc)
{
    if (acc == -1 || acc == *delivered) return false;
    *delivered = acc;
    return true;
}
/* Proven length and first two bytes; the rest is zero because the OEM payload is not known. */
static inline void rx_service_datagram(uint8_t out[RX_SERVICE_DATAGRAM_SIZE])
{
    memset(out, 0, RX_SERVICE_DATAGRAM_SIZE);
    out[0] = 0x8f;
    out[1] = 0x1c;
}
#endif
