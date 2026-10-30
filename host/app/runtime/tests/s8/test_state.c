#include "state_fixture.h"
#include <stdio.h>
int main(void) {
 struct state_fixture f;vxl_u8 data[64];state_init(&f);state_complete(&f);
 assert(f.c.capture_pending && f.c.capture_count==182);
 struct vxl_request r=sreq();r.handle=f.output;r.offset=0;r.length=64;r.user_ptr=(uintptr_t)data;
 f.m.fault=MOCK_WRITE_USER;assert(vxl_engine_call(&f.m.engine,1,VXL_COPY_FROM,&r)==-EFAULT);
 assert(f.c.capture_pending && !f.c.capture[0].delivered && f.m.engine.job.pinned);
 assert(vxl_s8_can_reap(&f.c)<0);
 vxl_u32 generation=f.m.engine.generation;vxl_u32 raw=f.c.hw.elapsed_raw;
 vxl_engine_close(&f.m.engine,1);assert(f.m.allocations==1 && f.m.engine.quarantined);
 assert(vxl_engine_open(&f.m.engine,2)==-EIO);
 assert(f.m.engine.generation==generation && f.c.hw.elapsed_raw==raw);
 state_destroy(&f);
 state_init(&f);f.c.hw.elapsed_raw=0;struct vxl_event ev=done_event();assert(vxl_s8_complete(&f.m.engine,&ev)<0 && !f.c.capture_pending);
 assert(f.c.error_class==VXL_S8_TIMING_INVALID && f.c.hw.elapsed_raw==0);state_destroy(&f);
 state_init(&f);f.c.hw.job_id=99;assert(vxl_s8_complete(&f.m.engine,&ev)<0);state_destroy(&f);
 state_init(&f);f.c.hw.exitcode[2]=1;assert(vxl_s8_complete(&f.m.engine,&ev)<0);state_destroy(&f);
 state_init(&f);f.c.hw.cdma_status=2;assert(vxl_s8_complete(&f.m.engine,&ev)<0);state_destroy(&f);
 state_init(&f);f.c.hw.event.status=0;assert(vxl_s8_complete(&f.m.engine,&ev)<0);state_destroy(&f);
 state_init(&f);state_complete(&f);vxl_s8_delivery_fault(&f.c,-EFAULT);
 assert(f.c.delivery_fault && f.m.engine.job.pinned && vxl_s8_can_reap(&f.c)<0);state_destroy(&f);
 state_init(&f);vxl_engine_close(&f.m.engine,1);
 assert(f.m.engine.quarantined && f.m.engine.job.pinned && f.m.allocations==1 && f.c.delivery_fault);
 assert(vxl_engine_open(&f.m.engine,2)==-EIO);state_destroy(&f);
 puts("S8_STATE_CAPTURE_FAULT_PINS_AND_PRESERVES_PASS TEST_ONLY");return 0;
}
