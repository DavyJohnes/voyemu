#include "../../../../profiles/common-qinggan-09/components/kernel/can_datagram.h"
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
    static unsigned char data[CAN_DATAGRAM_MAX + 1];
    check(CAN_DATAGRAM_MAX == 65536, "service datagrams may reach 64 KiB");
    data[0] = 0x6a;
    data[1] = 8;
    check(can_datagram_kind(data, CAN_FRAME_SIZE) == CAN_DATAGRAM_FRAME, "10-byte CAN frame");
    data[1] = 7;
    check(can_datagram_kind(data, CAN_FRAME_SIZE) == CAN_DATAGRAM_INVALID,
          "a 10-byte frame must declare 8 data bytes");
    data[0] = 0x8f;
    data[1] = 0x1c;
    check(can_datagram_kind(data, 7424) == CAN_DATAGRAM_SERVICE,
          "the observed 7424-byte service datagram is accepted whatever its second byte");
    check(can_datagram_kind(data, CAN_FRAME_SIZE + 1) == CAN_DATAGRAM_SERVICE &&
                  can_datagram_kind(data, CAN_DATAGRAM_MAX) == CAN_DATAGRAM_SERVICE,
          "service datagrams span 11 bytes to the maximum");
    check(can_datagram_kind(data, CAN_FRAME_SIZE - 1) == CAN_DATAGRAM_INVALID &&
                  can_datagram_kind(data, CAN_DATAGRAM_MAX + 1) == CAN_DATAGRAM_INVALID,
          "shorter than a frame or above the maximum is rejected");
    return 0;
}
