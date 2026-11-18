#include "rx_schedule.h"
#include <stdio.h>
#include <stdlib.h>
static void check(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "%s\n", message);
        exit(1);
    }
}
int main(void)
{
    int32_t delivered = -1;
    static uint8_t datagram[RX_SERVICE_DATAGRAM_SIZE];
    unsigned i, sent = 0;
    check(!rx_acc_due(&delivered, -1), "unknown ACC is never delivered");
    check(rx_acc_due(&delivered, 2) && delivered == 2, "first known ACC is delivered once");
    for (i = 0; i < 240; ++i) sent += rx_acc_due(&delivered, 2);
    check(sent == 0, "a stable ACC is not repeated");
    check(rx_acc_due(&delivered, 1) && !rx_acc_due(&delivered, 1), "each ACC change is delivered once");
    check(rx_acc_due(&delivered, 2), "returning to ON is a new change");
    check(!rx_acc_due(&delivered, -1) && delivered == 2, "losing the value keeps the delivered ACC");
    memset(datagram, 0xff, sizeof(datagram));
    rx_service_datagram(datagram);
    check(datagram[0] == 0x8f && datagram[1] == 0x1c, "service datagram keeps the observed header");
    check(datagram[1] + 2 != RX_SERVICE_DATAGRAM_SIZE, "the second byte is not the datagram length");
    for (i = 2; i < RX_SERVICE_DATAGRAM_SIZE; ++i) check(datagram[i] == 0, "unknown payload stays zero");
    return 0;
}
