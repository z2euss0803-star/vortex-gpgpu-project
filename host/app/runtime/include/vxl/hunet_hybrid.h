#ifndef VXL_HUNET_HYBRID_H
#define VXL_HUNET_HYBRID_H
#include <vxl/core.h>
struct vxl_hunet_ctx;
enum vxl_hybrid_phase {VXL_HYBRID_GPU_READY=0,VXL_HYBRID_CPU_WAIT=1,VXL_HYBRID_CPU_COMMITTED=2,VXL_HYBRID_ABORTED=3};
struct vxl_hybrid_release {struct vxl_hybrid_binding_v2 binding;vxl_u8 binding_sha[32];};
struct vxl_hybrid_delivery {vxl_u64 owner;vxl_u32 generation,command;union {struct vxl_hybrid_cpu_evidence_v1 cpu;struct vxl_hybrid_end_v1 end;} payload;};
struct vxl_hybrid_ctx {
 const struct vxl_hybrid_release *release;
 int (*hw_check)(struct vxl_hunet_ctx *);
 vxl_u32 enabled,phase,logical_jobs,gpu_jobs,cpu_jobs,hw_jobs_before,sequence,stage;
 vxl_u64 owner,token,token_serial,deadline_ns,network_deadline_ns,begin_ns,tensor_bytes,bytes_written;
 vxl_u32 generation,input_ready,end_pending;
 vxl_u8 *cpu_input,*cpu_output;
 struct vxl_hybrid_begin_v1 begin_receipt;
 struct vxl_hybrid_commit_v1 commit_receipt;
 struct vxl_hybrid_cpu_evidence_v1 evidence;
 struct vxl_hybrid_end_v1 end_receipt;
};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_hybrid_payload_validate(vxl_u32,const void *,size_t);
int vxl_hybrid_mode(const struct vxl_hunet_ctx *);
vxl_u32 vxl_hybrid_expected_gpu_jobs(const struct vxl_hunet_ctx *);
int vxl_hybrid_init(struct vxl_hunet_ctx *,const struct vxl_hybrid_release *);
int vxl_hybrid_seal(struct vxl_hunet_ctx *,const struct vxl_hunet_seal_v1 *);
int vxl_hybrid_check_gpu(struct vxl_hunet_ctx *,const struct vxl_hunet_job_v1 *);
int vxl_hybrid_gpu_completed(struct vxl_hunet_ctx *);
int vxl_hybrid_enter_cpu(struct vxl_hunet_ctx *);
int vxl_hybrid_cpu_delivered(struct vxl_hunet_ctx *);
void vxl_hybrid_start_roi(struct vxl_hunet_ctx *);
void vxl_hybrid_abort(struct vxl_hunet_ctx *,int,int);
void vxl_hybrid_poll(struct vxl_hunet_ctx *);
void vxl_hybrid_cleanup(struct vxl_hunet_ctx *);
int vxl_hybrid_busy(const struct vxl_hunet_ctx *);
int vxl_hybrid_command(struct vxl_hunet_ctx *,vxl_u32,struct vxl_request *);
int vxl_hybrid_idle(struct vxl_hunet_ctx *);
int vxl_hybrid_snapshot_input(struct vxl_hunet_ctx *);
int vxl_hybrid_begin_cpu(struct vxl_hunet_ctx *,struct vxl_hybrid_begin_v1 *);
int vxl_hybrid_commit_cpu(struct vxl_hunet_ctx *,const struct vxl_hybrid_commit_v1 *);
int vxl_hybrid_get_cpu_evidence(struct vxl_hunet_ctx *,vxl_u64,struct vxl_hybrid_cpu_evidence_v1 *);
int vxl_hybrid_end(struct vxl_hunet_ctx *,struct vxl_hybrid_end_v1 *);
void vxl_hybrid_delivery_capture(struct vxl_hunet_ctx *,vxl_u32,struct vxl_hybrid_delivery *);
void vxl_hybrid_delivery_finish(struct vxl_hunet_ctx *,const struct vxl_hybrid_delivery *);
int vxl_hybrid_transaction(struct vxl_hunet_ctx *,const vxl_u8 [16],vxl_u32,vxl_u32,vxl_u32,vxl_u64);
void *vxl_hybrid_alloc(size_t);
void vxl_hybrid_free(void *);
#ifdef __cplusplus
}
#endif
#endif
