#ifndef VOYEMU_CAN_BOUNDARY_H
#define VOYEMU_CAN_BOUNDARY_H
/* Private development-ECU proof. This does not attest a physical CAN bus. */
#ifdef __KERNEL__
#include <linux/types.h>
typedef __u32 can_u32;
typedef __u64 can_u64;
#else
#include <stdint.h>
typedef uint32_t can_u32;
typedef uint64_t can_u64;
#endif
#define CAN_BOUNDARY_MAGIC 0x43425731u
#define CAN_BOUNDARY_VERSION 1u
#define CAN_BOUNDARY_TTL_NS 180000000000ULL
#define CAN_BOUNDARY_NONE 0u
#define CAN_BOUNDARY_QUIESCING 1u
#define CAN_BOUNDARY_FROZEN 2u
#define CAN_BOUNDARY_ABORTED 3u
#define CAN_BOUNDARY_EXPIRED 4u
#define CAN_BOUNDARY_INVALID -1
#define CAN_BOUNDARY_STALE -2
#define CAN_BOUNDARY_BUSY -3
#define CAN_BOUNDARY_FROZEN_ERROR -4
#define CAN_BOUNDARY_INIT 1u
#define CAN_BOUNDARY_COMMIT 2u
#define CAN_BOUNDARY_COMMIT_RAW 3u
#define CAN_BOUNDARY_BEGIN 4u
#define CAN_BOUNDARY_FINISH 5u
#define CAN_BOUNDARY_ABORT 6u
#define CAN_BOUNDARY_WITNESS 7u
struct can_boundary_control {
    can_u32 magic, version, operation, flags;
    can_u64 session, epoch, nonce, deadline_ns, raw_seq;
    can_u32 mode, semantic_pending;
    can_u64 open_seq, commit_seq, on_count, first_mode, first_ns;
    can_u64 fence_seq, fence_ns, queued, inflight, loss, generated_seq;
    can_u64 now_ns, fence_generated_seq;
    can_u32 stage, invalid;
};
#ifdef CAN_BOUNDARY_IMPLEMENTATION
struct can_boundary_state {
    can_u64 session, epoch, open_seq, commit_seq, on_count;
    can_u64 first_mode, first_ns, last_ns, mode;
    can_u64 nonce, nonce_session, nonce_epoch, deadline_ns, fence_seq, fence_ns, fence_generated_seq;
    can_u32 stage, invalid, initialized;
};
static int can_boundary_invalid(struct can_boundary_state *s) {
    s->invalid = 1; return CAN_BOUNDARY_INVALID;
}
static int can_boundary_tick(struct can_boundary_state *s, can_u64 now) {
    if (s->invalid || !now || now < s->last_ns) return can_boundary_invalid(s);
    s->last_ns = now;
    if ((s->stage == CAN_BOUNDARY_QUIESCING || s->stage == CAN_BOUNDARY_FROZEN) && now >= s->deadline_ns)
        s->stage = CAN_BOUNDARY_EXPIRED;
    return 0;
}
static int can_boundary_is_frozen(const struct can_boundary_state *s) {
    return s->stage == CAN_BOUNDARY_QUIESCING || s->stage == CAN_BOUNDARY_FROZEN || s->stage == CAN_BOUNDARY_EXPIRED;
}
static int can_boundary_open(struct can_boundary_state *s, can_u64 now) {
    if (can_boundary_tick(s,now)) return CAN_BOUNDARY_INVALID;
    if (can_boundary_is_frozen(s)) return CAN_BOUNDARY_FROZEN_ERROR;
    if (s->open_seq == ~(can_u64)0) return can_boundary_invalid(s);
    ++s->open_seq; s->session=0; s->epoch=0;
    return 0;
}
static int can_boundary_commit(struct can_boundary_state *s, can_u64 session, can_u64 epoch, can_u32 mode, can_u64 now) {
    if (can_boundary_tick(s,now)) return CAN_BOUNDARY_INVALID;
    if (!session || session != s->session || !epoch || epoch < s->epoch) return CAN_BOUNDARY_STALE;
    if (s->stage == CAN_BOUNDARY_FROZEN || s->stage == CAN_BOUNDARY_EXPIRED) return CAN_BOUNDARY_FROZEN_ERROR;
    if (mode < 1 || mode > 5 || s->commit_seq == ~(can_u64)0) return can_boundary_invalid(s);
    if (mode == 5 && s->mode != 5) {
        if (s->on_count == ~(can_u64)0) return can_boundary_invalid(s);
        ++s->on_count;
    }
    s->epoch=epoch; s->mode=mode; ++s->commit_seq;
    return 0;
}
static int can_boundary_init(struct can_boundary_state *s, can_u64 session, can_u64 epoch, can_u32 mode, can_u64 now) {
    if (can_boundary_tick(s,now)) return CAN_BOUNDARY_INVALID;
    if (!s->open_seq || s->session || !session || !epoch || mode < 1 || mode > 5) return CAN_BOUNDARY_STALE;
    if (can_boundary_is_frozen(s)) return CAN_BOUNDARY_FROZEN_ERROR;
    s->session=session; s->epoch=epoch;
    if (!s->initialized) { s->first_mode=mode; s->first_ns=now; }
    if (can_boundary_commit(s,session,epoch,mode,now)) return CAN_BOUNDARY_INVALID;
    s->initialized=1;
    return 0;
}
static int can_boundary_begin(struct can_boundary_state *s, can_u64 session, can_u64 epoch, can_u64 nonce, can_u64 deadline, can_u64 now) {
    if (can_boundary_tick(s,now)) return CAN_BOUNDARY_INVALID;
    if (!s->initialized || !nonce || session != s->session || epoch != s->epoch) return CAN_BOUNDARY_STALE;
    if (s->nonce == nonce) {
        return (s->stage == CAN_BOUNDARY_QUIESCING || s->stage == CAN_BOUNDARY_FROZEN) && s->deadline_ns == deadline ? 0 : CAN_BOUNDARY_STALE;
    }
    if (can_boundary_is_frozen(s)) return CAN_BOUNDARY_BUSY;
    if (s->mode != 2 || deadline <= now || deadline-now > CAN_BOUNDARY_TTL_NS) return CAN_BOUNDARY_INVALID;
    s->nonce=nonce; s->nonce_session=session; s->nonce_epoch=epoch; s->deadline_ns=deadline; s->fence_seq=0; s->fence_ns=0;
    s->fence_generated_seq=0; s->stage=CAN_BOUNDARY_QUIESCING;
    return 0;
}
static int can_boundary_finish(struct can_boundary_state *s, can_u64 session, can_u64 epoch, can_u64 nonce, can_u64 queued, can_u64 inflight, can_u64 now) {
    if (can_boundary_tick(s,now)) return CAN_BOUNDARY_INVALID;
    if (!nonce || session != s->session || epoch != s->epoch || nonce != s->nonce) return CAN_BOUNDARY_STALE;
    if (s->stage == CAN_BOUNDARY_FROZEN) return 0;
    if (s->stage != CAN_BOUNDARY_QUIESCING) return CAN_BOUNDARY_STALE;
    if (queued || inflight) return CAN_BOUNDARY_BUSY;
    if (s->mode != 2) return CAN_BOUNDARY_INVALID;
    s->stage=CAN_BOUNDARY_FROZEN; s->fence_seq=s->commit_seq; s->fence_ns=now;
    return 0;
}
static int can_boundary_abort(struct can_boundary_state *s, can_u64 nonce, can_u64 now) {
    if (can_boundary_tick(s,now)) return CAN_BOUNDARY_INVALID;
    if (!nonce || nonce != s->nonce) return CAN_BOUNDARY_STALE;
    if (s->stage == CAN_BOUNDARY_ABORTED) return 0;
    if (!can_boundary_is_frozen(s)) return CAN_BOUNDARY_STALE;
    s->stage=CAN_BOUNDARY_ABORTED;
    return 0;
}
#endif
#endif
