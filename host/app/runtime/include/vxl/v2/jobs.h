#ifndef VXL_V2_JOBS_H
#define VXL_V2_JOBS_H
#include <vxl/v2/program.h>
#define VXL_V2_PENDING 32u
#define VXL_V2_COMPLETIONS 64u
#define VXL_V2_JOB_PINS 14u /* 8 data + args + up to 5 implicit program objects */
enum vxl_v2_job_state {VXL_V2_SLOT_FREE=0,VXL_V2_QUEUED=1,VXL_V2_RUNNING=2,VXL_V2_OK=3,VXL_V2_FAILED=4,VXL_V2_SKIPPED=5,VXL_V2_STAGED=6};
struct vxl_v2_start_proof {vxl_u64 start_ns;vxl_u32 jobs_before,watchdog,started;};
struct vxl_v2_snapshot {
 vxl_u64 sequence,now_ns;
 vxl_u32 status,errors,pending_w,pending_r,jobs,core_mask,elapsed_raw,watchdog,cycles_valid;
 vxl_u32 done_seen,cdma_idle,guard_ok,parameters_ok,source_ok,exitcode;
 /* FAST metadata validity is never a claim of fresh content verification. */
 vxl_u32 metadata_ok;
 vxl_u64 cdma_ns,guard_ns,digest_ns,poll_ns,handoff_ns,io_bytes;
};
struct vxl_v2_execution_ops {
 int (*prepare)(void *,const struct vxl_v2_program *,const struct vxl_v2_job *,const void *,vxl_u32);
 int (*start)(void *,const struct vxl_v2_program *,const struct vxl_v2_job *,struct vxl_v2_start_proof *);
 int (*ack)(void *,vxl_u32 *idle,vxl_u32 *ready);
 vxl_u64 (*now_ns)(void *);
};
struct vxl_v2_slot {
 struct vxl_v2_job job;struct vxl_v2_completion completion;struct vxl_v2_program *program;
 vxl_u8 args[4096];struct vxl_v2_ref refs[14];vxl_u32 ref_count,state;
 struct vxl_v2_start_proof start;vxl_u64 deadline_ns;
};
struct vxl_v2_queue {
 struct vxl_v2_slot slots[64];vxl_u32 fifo[32],head,tail,pending,reserved,completed;
 int active_slot,fault_result;vxl_u64 next_sequence,last_cookie,fault_cookie,owner_epoch;
 const struct vxl_v2_execution_ops *ops;void *hardware;
 int (*snapshot)(void *,vxl_u64,struct vxl_v2_snapshot *);
 const struct vxl_v2_snapshot *metrics; /* provider-owned, same locked lifetime */
 vxl_u32 fast_supported; /* native provider explicitly enables additive policy */
};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_v2_queue_bind(struct vxl_v2_ctx *,struct vxl_v2_queue *,const struct vxl_v2_execution_ops *,void *);
int vxl_v2_accept(struct vxl_v2_ctx *,vxl_u64,const struct vxl_v2_job *,vxl_u64 *sequence);
int vxl_v2_batch_accept(struct vxl_v2_ctx *,vxl_u64,const struct vxl_v2_job *,vxl_u32);
int vxl_v2_pump(struct vxl_v2_ctx *);
int vxl_v2_poll(struct vxl_v2_ctx *,const struct vxl_v2_snapshot *);
int vxl_v2_observe(struct vxl_v2_ctx *,const struct vxl_v2_snapshot *,int snapshot_result);
int vxl_v2_read_completion(struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 sequence,vxl_u64 cookie,struct vxl_v2_completion *);
int vxl_v2_ack_reap(struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 sequence,vxl_u64 cookie);
int vxl_v2_close_queue(struct vxl_v2_ctx *,vxl_u64 owner);
vxl_u32 vxl_v2_poll_events(const struct vxl_v2_ctx *);
int vxl_v2_deliver_completion(struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 sequence,vxl_u64 cookie,int (*write)(void *,const void *,vxl_u32),void *);
/* Shared implementation helpers. Every operation is under the one device mutex. */
int vxl_v2_slot_find(struct vxl_v2_ctx *,vxl_u64 owner,vxl_u64 sequence,vxl_u64 cookie,struct vxl_v2_slot **);
void vxl_v2_finish_slot(struct vxl_v2_queue *,struct vxl_v2_slot *,vxl_u32 state,int result,vxl_u64 now);
int vxl_v2_fail_queue(struct vxl_v2_ctx *,int result,vxl_u64 now);
#ifdef __cplusplus
}
#endif
#endif
