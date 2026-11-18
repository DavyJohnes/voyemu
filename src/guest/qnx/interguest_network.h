#ifndef VOYEMU_QNX_INTERGUEST_NETWORK_H
#define VOYEMU_QNX_INTERGUEST_NETWORK_H
/* Single-frame mailboxes; release length publishes each immutable Ethernet frame. */
typedef unsigned QnetU32;
typedef unsigned long QnetSize;
typedef unsigned long long QnetU64;
typedef long long QnetI64;
typedef struct {
    int fd;
    short events, revents;
} QnetPoll;
/* UTC: host owns immutable epoch/request; guest releases ACK after status/readback.
 * status: 0 success, -1 range/sequence, -2 set failure, -3 read failure, -4 mismatch. */
/* Flush: each attempt uses a fresh increasing request; ACK follows sync and
 * readonly /var + root remount. status: 0 quiesced, -2 /var failure, -3 root failure. */
typedef struct {
    QnetU32 magic, version, txlen, rxlen, txseq, rxseq, stop, fault, txpackets, rxpackets;
    QnetU64 utc_epoch;
    QnetU32 utc_request, utc_ack;
    int utc_status;
    QnetU32 utc_actual;
    unsigned char tx[1536], rx[1536];
    QnetU32 flush_request, flush_ack;
    int flush_status;
    unsigned char padding[948];
} QnetPage;
_Static_assert(sizeof(QnetPage) == 4096 && __builtin_offsetof(QnetPage, tx) == 64 &&
                       __builtin_offsetof(QnetPage, rx) == 1600 && sizeof(QnetPoll) == 8,
               "QNET ABI");
typedef struct {
    QnetI64 seconds, nanoseconds;
} QnetTimespec;
_Static_assert(sizeof(QnetTimespec) == 16 && __builtin_offsetof(QnetTimespec, nanoseconds) == 8 &&
                       __builtin_offsetof(QnetPage, utc_epoch) == 40 &&
                       __builtin_offsetof(QnetPage, utc_request) == 48 &&
                       __builtin_offsetof(QnetPage, utc_ack) == 52 &&
                       __builtin_offsetof(QnetPage, utc_status) == 56 &&
                       __builtin_offsetof(QnetPage, utc_actual) == 60,
               "QNET UTC ABI");
_Static_assert(__builtin_offsetof(QnetPage, flush_request) == 3136 &&
                       __builtin_offsetof(QnetPage, flush_ack) == 3140 &&
                       __builtin_offsetof(QnetPage, flush_status) == 3144,
               "QNET flush ABI");
#endif
