#ifndef VOYEMU_DEV_MODEL_ORACLE_H
#define VOYEMU_DEV_MODEL_ORACLE_H
#include <stdint.h>
#define ORACLE_CAPACITY 256
#define ORACLE_OK 0
#define ORACLE_INVALID -1
#define ORACLE_GAP -2
#define ORACLE_UNKNOWN -3
/* RAW_READ is not kernel ioctl admission; EMIT is not delivery. LOSS means
 * the transport reset may have dropped requests or datagrams. */
enum oracle_kind { ORACLE_BOOT=1, ORACLE_ADMISSION=2, ORACLE_RAW_READ=3,
                   ORACLE_STATE=4, ORACLE_EMIT=5, ORACLE_LOSS=6, ORACLE_DROP=7, ORACLE_NOOP=8 };
struct oracle_event { uint64_t session, epoch, seq, boottime_ns; int32_t kind, state, value; uint8_t frame[10], frame_length; };
struct model_oracle {
    struct oracle_event events[ORACLE_CAPACITY];
    uint64_t session, latest, last_ns, state_ns;
    uint8_t boot_id[16], first_open;
};
void oracle_init(struct model_oracle *oracle, uint64_t session, const uint8_t boot_id[16], int first_open);
int oracle_append(struct model_oracle *oracle, uint64_t epoch, uint64_t ns, int32_t kind, int32_t state, int32_t value);
int oracle_append_frame(struct model_oracle *oracle, uint64_t epoch, uint64_t ns, int32_t kind, const uint8_t frame[10]);
int oracle_read(const struct model_oracle *oracle, uint64_t session, uint64_t seq, struct oracle_event *event);
uint64_t oracle_earliest(const struct model_oracle *oracle);
#endif
