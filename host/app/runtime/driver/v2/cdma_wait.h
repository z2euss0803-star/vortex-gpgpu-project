#ifndef VXL_V2_CDMA_WAIT_H
#define VXL_V2_CDMA_WAIT_H
#include <vxl/core.h>
/* Driver-private wait policy; no UAPI, profile matching or shared CDMA change. */
struct vxl_v2_cdma_wait {vxl_u64 began_ns;vxl_u32 pauses;};
static inline void vxl_v2_cdma_wait_begin(struct vxl_v2_cdma_wait *w,vxl_u64 now){w->began_ns=now;w->pauses=0;}
static inline int vxl_v2_cdma_wait_short(struct vxl_v2_cdma_wait *w,vxl_u64 now){
 /* Both elapsed time and pause count are bounded, even on a stalled clock.
  * Once exhausted, only the original sleeping/transaction timeout applies. */
 if(w->pauses>=50 || now<w->began_ns || now-w->began_ns>=50000){w->pauses=50;return 0;}
 w->pauses++;return 1;
}
#endif
