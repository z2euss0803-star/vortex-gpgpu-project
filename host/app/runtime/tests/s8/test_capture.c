#include "state_fixture.h"
#include <stdio.h>
int main(void) {
 struct state_fixture f;state_init(&f);state_complete(&f);
 assert(f.m.engine.job.state==VXL_COMPLETED && vxl_s8_can_reap(&f.c)<0);
 struct vxl_request r=sreq();r.job_id=1;assert(vxl_engine_call(&f.m.engine,1,VXL_REAP,&r)==-EBUSY);
 r=sreq();r.handle=f.output;assert(vxl_engine_call(&f.m.engine,1,VXL_FREE,&r)==-EBUSY);
 assert(vxl_engine_call(&f.m.engine,1,VXL_COPY_TO,&r)==-EBUSY);
 assert(vxl_engine_call(&f.m.engine,1,VXL_SUBMIT,&r)==-EBUSY);
 for(unsigned i=0;i<f.c.capture_count;++i) {
  struct vxl_s8_capture *part=&f.c.capture[i];
  assert(!vxl_s8_capture_range(&f.c,part->gpu,part->bytes-1));
  assert(part->delivered==part->bytes-1 && vxl_s8_can_reap(&f.c)<0);
  assert(!vxl_s8_capture_range(&f.c,part->gpu+part->bytes-1,1));
 }
 assert(vxl_s8_can_reap(&f.c)<0);f.c.trace_delivered=3;assert(vxl_s8_can_reap(&f.c)<0);
 f.c.trace_delivered=7;assert(!vxl_s8_can_reap(&f.c));
 r=sreq();r.job_id=1;assert(!vxl_engine_call(&f.m.engine,1,VXL_REAP,&r));
 assert(!f.c.capture_pending && f.m.engine.job.state==VXL_READY && f.c.next_sequence==2 && f.c.nonce_locked);
 assert(vxl_s8_capture_range(&f.c,0x81400000,4096)<0);
 vxl_engine_close(&f.m.engine,1);assert(!f.m.allocations);state_destroy(&f);
 state_init(&f);state_complete(&f);vxl_u32 generation=f.m.engine.generation;
 vxl_engine_close(&f.m.engine,1);assert(f.m.allocations && f.m.engine.quarantined);
 assert(vxl_engine_open(&f.m.engine,2)==-EIO && f.m.engine.generation==generation);state_destroy(&f);
 puts("S8_COMPLETE_CAPTURE_BEFORE_REAP_PASS ranges=182 TEST_ONLY");return 0;
}
