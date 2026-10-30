/* Internal diagnostic accounting. This is not a userspace ABI. */
#ifndef VXL_SUBMIT_TIMING_H
#define VXL_SUBMIT_TIMING_H
#include <vxl/core.h>
#define VXL_TIME_BACKWARDS 1u
#define VXL_TIME_OVERFLOW 2u
#define VXL_TIME_BUFFER_FULL 4u
#define VXL_TIME_BUSY 8u
#define VXL_TIME_FIELDS(X) \
 X(generation) X(job_id) X(sequence) X(op) X(stage) X(result) X(flags) \
 X(submit_enter_ns) X(submit_return_ns) X(wait_enter_ns) X(wait_return_ns) \
 X(gpu_start_ns) X(gpu_done_observed_ns) X(ack_end_ns) X(elapsed_cycles) \
 X(prepare_ns) X(guard_init_ns) X(guard_check_ns) X(state_poll_ns) X(state_poll_calls) X(ack_ns) \
 X(cdma_ns) X(cdma_calls) X(cdma_bytes) X(cdma_sleep_ns) X(cdma_pause_calls) \
 X(guard_cdma_ns) X(guard_cdma_calls) X(guard_cdma_bytes) X(guard_cdma_sleep_ns) \
 X(submit_mutex_wait_ns) X(wait_mutex_wait_ns) X(wait_sleep_ns) X(wait_sleep_calls) \
 X(worker_poll_ns) X(worker_poll_calls) X(cdma_fast_poll_ns) X(cdma_fast_poll_calls)
struct vxl_submit_time {
#define VXL_TIME_DECLARE(n) vxl_u64 n;
 VXL_TIME_FIELDS(VXL_TIME_DECLARE)
#undef VXL_TIME_DECLARE
 vxl_u32 guard_depth;
};
struct vxl_submit_trace {
 struct vxl_submit_time *rows,*active;
 vxl_u32 capacity,used,flags;
};
static inline void vxl_time_add(struct vxl_submit_time *t,vxl_u64 *field,vxl_u64 begin,vxl_u64 end){
 vxl_u64 delta;if(!t)return;
 if(end<begin){t->flags|=VXL_TIME_BACKWARDS;return;}
 delta=end-begin;if(*field>~(vxl_u64)0-delta){*field=~(vxl_u64)0;t->flags|=VXL_TIME_OVERFLOW;}else *field+=delta;
}
static inline struct vxl_submit_time *vxl_time_begin(struct vxl_submit_trace *tr,vxl_u32 gen,vxl_u64 now){
 struct vxl_submit_time *t;if(!tr||!tr->rows)return NULL;
 if(tr->active){tr->flags|=VXL_TIME_BUSY;return NULL;}
 if(tr->used>=tr->capacity){tr->flags|=VXL_TIME_BUFFER_FULL;return NULL;}
 t=&tr->rows[tr->used++];memset(t,0,sizeof(*t));t->generation=gen;t->submit_enter_ns=now;tr->active=t;return t;
}
static inline void vxl_time_finish(struct vxl_submit_trace *tr,vxl_u64 now,int result){
 if(!tr||!tr->active)return;
 tr->active->wait_return_ns=now;tr->active->result=(vxl_u64)(long long)result;tr->active=NULL;
}
#endif
