/* SPDX-License-Identifier: GPL-2.0 */
#ifndef VOYEMU_CAN_DATAGRAM_H
#define VOYEMU_CAN_DATAGRAM_H
/* The A9 protocol-21 multicast group carries 10-byte CAN frames (byte 1 is the data length 8)
 * and longer service datagrams; only the model device writes either. */
#define CAN_FRAME_SIZE 10
#define CAN_DATAGRAM_MAX 65536

enum can_datagram_kind { CAN_DATAGRAM_INVALID, CAN_DATAGRAM_FRAME, CAN_DATAGRAM_SERVICE };

static inline enum can_datagram_kind can_datagram_kind(const unsigned char *data,
                                                       unsigned long size)
{
    if (size == CAN_FRAME_SIZE)
        return data[1] == 8 ? CAN_DATAGRAM_FRAME : CAN_DATAGRAM_INVALID;
    return size > CAN_FRAME_SIZE && size <= CAN_DATAGRAM_MAX ? CAN_DATAGRAM_SERVICE
                                                             : CAN_DATAGRAM_INVALID;
}
#endif
