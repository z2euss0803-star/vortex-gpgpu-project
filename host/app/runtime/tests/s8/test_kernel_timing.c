#include <vxl/s8.h>
#include <vxl/s8_timing.h>
#include <assert.h>
#include <stdio.h>
int main(void) {
 struct vxl_s8_ctx c={0};
 assert(!vxl_s8_timing_event(&c,VXL_S8_GPU_START,0));assert(c.times.valid_events&(1ULL<<VXL_S8_GPU_START));
 assert(!vxl_s8_timing_event(&c,VXL_S8_GPU_START,10));assert(c.times.first[VXL_S8_GPU_START]==0 && c.times.last[VXL_S8_GPU_START]==10);
 assert(!vxl_s8_timing_add(&c,VXL_S8_CDMA,10,30,64));assert(c.times.metrics[VXL_S8_CDMA].ns==20 && c.times.metrics[VXL_S8_CDMA].bytes==64);
 assert(vxl_s8_timing_add(&c,VXL_S8_CDMA,31,30,64)<0 && c.times.invalid);
 assert(c.times.metrics[VXL_S8_CDMA].ns==20);
 assert(c.times.metrics[VXL_S8_CDMA].last_begin_ns==31 && c.times.metrics[VXL_S8_CDMA].last_ns==30);
 assert(vxl_s8_timing_event(&c,999,0)<0);
 puts("S8_KERNEL_TIMING_RAW_ZERO_VALID_AND_BACKWARDS_REFUSED_PASS");return 0;
}
