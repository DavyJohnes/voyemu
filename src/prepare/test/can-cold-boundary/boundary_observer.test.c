#define main production_observer_main
#include "observer.c"
#undef main
#include <assert.h>
int main(void) {
 struct can_boundary_control c={0};
 c.magic=CAN_BOUNDARY_MAGIC;c.version=1;c.operation=CAN_BOUNDARY_WITNESS;c.flags=1;
 c.session=42;c.epoch=1;c.nonce=9;c.deadline_ns=100;c.mode=2;c.open_seq=1;c.commit_seq=5;
 c.first_mode=2;c.first_ns=1;c.fence_seq=5;c.fence_ns=5;c.now_ns=10;c.stage=CAN_BOUNDARY_FROZEN;
 assert(boundary_valid(0,&c));
 c.now_ns=100;assert(!boundary_valid(0,&c));c.now_ns=10;
 c.fence_seq=4;assert(!boundary_valid(0,&c));c.fence_seq=5;
 c.inflight=1;assert(!boundary_valid(0,&c));c.inflight=0;
 c.mode=5;assert(!boundary_valid(0,&c));c.mode=2;
 c.invalid=1;assert(!boundary_valid(0,&c));c.invalid=0;
 assert(!boundary_valid(-1,&c));
 boundary_emit(0,&c,"01234567-89ab-cdef-0123-456789abcdef",1);
 return 0;
}
