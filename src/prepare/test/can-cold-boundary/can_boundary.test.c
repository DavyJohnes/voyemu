#define CAN_BOUNDARY_IMPLEMENTATION
#include "can_boundary.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 struct can_boundary_state s = {0};
 assert(can_boundary_open(&s,1)==0);
 assert(can_boundary_init(&s,42,1,2,2)==0);
 assert(s.first_mode==2 && s.on_count==0 && s.open_seq==1);
 assert(can_boundary_commit(&s,42,1,5,3)==0 && s.on_count==1);
 assert(can_boundary_commit(&s,42,1,5,4)==0 && s.on_count==1);
 assert(can_boundary_commit(&s,42,2,2,5)==0 && s.on_count==1);
 assert(can_boundary_begin(&s,42,2,9,100,6)==0);
 assert(can_boundary_finish(&s,42,2,9,1,0,7)==CAN_BOUNDARY_BUSY);
 assert(can_boundary_finish(&s,42,2,9,0,1,7)==CAN_BOUNDARY_BUSY);
 assert(can_boundary_finish(&s,42,2,9,0,0,8)==0);
 assert(s.stage==CAN_BOUNDARY_FROZEN && s.fence_seq==s.commit_seq && s.fence_ns==8);
 assert(can_boundary_commit(&s,42,2,5,9)==CAN_BOUNDARY_FROZEN_ERROR);
 assert(can_boundary_abort(&s,10,9)==CAN_BOUNDARY_STALE);
 assert(can_boundary_abort(&s,9,9)==0);
 assert(can_boundary_commit(&s,42,2,5,10)==0 && s.on_count==2);
 assert(can_boundary_open(&s,11)==0);
 assert(can_boundary_init(&s,43,1,2,12)==0 && s.on_count==2 && s.first_mode==2);
 assert(can_boundary_begin(&s,42,1,10,100,13)==CAN_BOUNDARY_STALE);
 assert(can_boundary_begin(&s,43,1,10,100,13)==0);
 assert(can_boundary_tick(&s,100)==0 && s.stage==CAN_BOUNDARY_EXPIRED);
 assert(can_boundary_finish(&s,43,1,10,0,0,101)==CAN_BOUNDARY_STALE);
 assert(can_boundary_commit(&s,43,1,5,101)==CAN_BOUNDARY_FROZEN_ERROR);
 assert(can_boundary_abort(&s,10,101)==0);
 s.commit_seq=~(can_u64)0;
 assert(can_boundary_commit(&s,43,1,2,102)==CAN_BOUNDARY_INVALID);
 assert(s.invalid);
 puts("can boundary tests passed");
 return 0;
}
