#ifndef VXL_S8_TIMING_H
#define VXL_S8_TIMING_H
#include <vxl/core.h>
enum vxl_s8_time_event {VXL_S8_SUBMIT_ENTER,VXL_S8_PREP_BEGIN,VXL_S8_PREP_END,VXL_S8_GPU_START,VXL_S8_FIRST_DONE,VXL_S8_ACK_BEGIN,VXL_S8_ACK_END,VXL_S8_WAIT_ENTER,VXL_S8_WAIT_RETURN,VXL_S8_CAPTURE_DONE,VXL_S8_TIME_EVENTS};
enum vxl_s8_metric_id {VXL_S8_PREPARE,VXL_S8_RESTORE,VXL_S8_CODE_VALIDATE,VXL_S8_INPUT_HASH,VXL_S8_GUARD_INIT,VXL_S8_GUARD_CHECK,VXL_S8_CDMA,VXL_S8_GUARD_CDMA,VXL_S8_CDMA_SLEEP,VXL_S8_GUARD_CDMA_SLEEP,VXL_S8_STATE_POLL,VXL_S8_WORKER_POLL,VXL_S8_WAIT_SLEEP,VXL_S8_WAIT_IOCTL,VXL_S8_SUBMIT_IOCTL,VXL_S8_COPY_FROM_TIME,VXL_S8_COPY_TO_TIME,VXL_S8_USER_COPY,VXL_S8_ACK_TIME,VXL_S8_DELIVERY,VXL_S8_CONTROL_PREFLIGHT,VXL_S8_MUTEX_WAIT,VXL_S8_RESULTS_CHECK,VXL_S8_METRICS};
struct vxl_s8_metric {vxl_u64 ns,calls,bytes,first_ns,last_begin_ns,last_ns;vxl_u32 valid,overflow;};
struct vxl_s8_timing {vxl_u64 first[VXL_S8_TIME_EVENTS],last[VXL_S8_TIME_EVENTS],event_calls[VXL_S8_TIME_EVENTS],valid_events;struct vxl_s8_metric metrics[VXL_S8_METRICS];vxl_u32 clock,invalid,guard_depth;};
struct vxl_s8_ctx;
#ifdef __cplusplus
extern "C" {
#endif
void vxl_s8_timing_reset(struct vxl_s8_ctx *);
int vxl_s8_timing_event(struct vxl_s8_ctx *,vxl_u32 event,vxl_u64 ns);
int vxl_s8_timing_add(struct vxl_s8_ctx *,vxl_u32 metric,vxl_u64 begin,vxl_u64 end,vxl_u64 bytes);
vxl_u64 vxl_s8_now_ns(struct vxl_s8_ctx *);
#ifdef __cplusplus
}
#endif
#endif
