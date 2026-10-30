#ifndef VXL_MOSAIC_HOST_H
#define VXL_MOSAIC_HOST_H
#include <vxl/mosaic.h>
#include <vxl/mosaic_assets.h>
#include <vxl/engine.h>
#define MOSAIC_PROFILE 6u
#define MOSAIC_PROGRAM 12u
#define MOSAIC_BINDING_MAGIC 0x31424d56u
#define MOSAIC_CODE_GPU 0x80000000u
#define MOSAIC_ARGS_GPU 0x80100040u
enum mosaic_digest_index {MOSAIC_SOURCE_SHA,MOSAIC_FLAT_SHA,MOSAIC_ELF_SHA,MOSAIC_MODEL_SHA,MOSAIC_ASSETS_SHA,MOSAIC_GRAPH_SHA,MOSAIC_LAYOUT_SHA,MOSAIC_COMPILER_SHA,MOSAIC_RUNTIME_SHA,MOSAIC_RUNNER_SHA,MOSAIC_KERNEL_CONFIG_SHA,MOSAIC_HARDWARE_SHA,MOSAIC_BIT_SHA,MOSAIC_XSA_SHA};
struct mosaic_binding_v1 {vxl_u32 magic,version,bytes,profile,program,abi_major,features,reserved0;vxl_u8 digests[14][32],reserved[32];};
struct mosaic_mutable {vxl_u32 offset,bytes;};
struct mosaic_catalog_layout {struct mosaic_layout layout;vxl_u8 graph_sha[32],layout_sha[32];};
struct mosaic_program {
 const vxl_u8 *flat;vxl_u32 flat_bytes,runtime_bytes,entry;
 const struct mosaic_mutable *mutable_ranges;vxl_u32 mutable_count;
 const struct mosaic_catalog_layout *layouts;vxl_u32 layout_count;
 const struct mosaic_manifest *assets;vxl_u32 assets_bytes;
 vxl_u8 graph_policy_sha[32],runtime_deps_sha[32],binding_sha[32];
 struct mosaic_binding_v1 binding;
 vxl_u32 crt_base,crt_bytes,tls_base,tbss_bytes,tbss_stride,tbss_align,stack_stride,static_stack_bytes;
 vxl_u64 stack_base,stack_bytes;
 /* Private catalog sidecar inside the existing 8MiB code object, after the
  * parameter/guard gap. Not part of public ioctl or 512B binding layout. */
 const vxl_u8 *rodata;vxl_u32 rodata_bytes,rodata_gpu;vxl_u8 rodata_sha[32];
};
enum mosaic_check {MOSAIC_CHECK_IDENTITY=1u,MOSAIC_CHECK_GUARDS=2u,MOSAIC_CHECK_IMMUTABLE=4u,MOSAIC_CHECK_INPUT=8u,MOSAIC_CHECK_EXITS=16u,MOSAIC_CHECK_ACK=32u,MOSAIC_CHECK_TIMING=64u};
#define MOSAIC_REQUIRED_CHECKS 127u
/* Trusted driver observations only; never accepted as an ioctl payload. */
struct mosaic_completion {
 struct vxl_event before_ack;
 vxl_u32 generation,sequence,checks,exit_valid,exitcode[4];
 vxl_u32 elapsed_raw,watchdog,cdma_status,ack_idle,ack_ready,tex_pending;
 vxl_u64 elapsed_address;
 vxl_u64 submit_enter,prepare_enter,prepare_done,start,done_seen,ack_done,complete;
};
/* In-memory evidence component. T17 provides the versioned external encoding. */
struct mosaic_evidence {
 struct mosaic_job_v1 args;struct mosaic_completion observed;
 vxl_u64 job_id;vxl_u32 state,elapsed_valid;int result;
};
struct mosaic_io {
 void *context;
 int (*rw)(void *,vxl_u64,void *,vxl_u64,int);
 vxl_u32 (*read)(void *,vxl_u32);
 void (*write)(void *,vxl_u32,vxl_u32);
 int (*ready)(void *);
 vxl_u32 (*cdma)(void *);
 vxl_u64 (*now_ns)(void *);
};
struct mosaic_delivery {
 vxl_u64 serial,owner,job_id;
 vxl_u32 generation,kind,offset,bytes;
};
/* Host/kernel only. No transport of these pointers or sizeof this state. */
struct mosaic_host_state {
 struct vxl_engine *engine;const struct mosaic_program *program;
 vxl_u8 expected_binding_sha[32];
 vxl_u32 nonce_locked,loaded,frame_active,phase,first,sequence,generation,epoch;
 int first_error;
 vxl_u32 job_active,evidence_pending,evidence_bytes,result_bytes,diagnostics;
 vxl_u64 owner,last_job_id;
 vxl_u8 run_id[16];
 struct mosaic_evidence evidence;
 struct mosaic_io io;
 vxl_u64 handles[5],deadline_ns,submit_enter_ns,io_ns,io_bytes,guard_ns,immutable_ns,poll_ns,handoff_ns;
 vxl_u32 sealed_layout,input_written;
 vxl_u8 nonce[16],input_sha[32],args_shadow[128];
 vxl_u64 delivery_serial;
 struct mosaic_delivery pending_delivery;
};
#ifdef __cplusplus
static_assert(sizeof(struct mosaic_binding_v1)==512,"Mosaic binding size");
extern "C" {
#else
_Static_assert(sizeof(struct mosaic_binding_v1)==512,"Mosaic binding size");
#endif
int mosaic_host_init(struct mosaic_ctx *,struct mosaic_host_state *,struct vxl_engine *,const vxl_u8 expected[32]);
int mosaic_program_register(struct mosaic_ctx *,const struct mosaic_program *);
int mosaic_program_rodata_check(const struct mosaic_program *);
int mosaic_program_code_span(const struct mosaic_program *,vxl_u64 offset,vxl_u64 bytes);
void mosaic_program_catalog_hash(const struct mosaic_program *,int graph,vxl_u8 out[32]);
void mosaic_program_runtime_hash(const struct mosaic_program *,vxl_u8 out[32]);
int mosaic_phase(vxl_u32 phase,vxl_u32 width,vxl_u32 height,vxl_u32 *layer,vxl_u32 *op,vxl_u32 *items);
int mosaic_frame_begin(struct mosaic_ctx *,vxl_u64 owner,const vxl_u8 run[16],vxl_u32 layout,vxl_u32 epoch,vxl_u32 diagnostics);
int mosaic_next_job(struct mosaic_ctx *,vxl_u32 count,struct mosaic_job_v1 *);
int mosaic_submit_begin(struct mosaic_ctx *,vxl_u64 owner,vxl_u64 job_id,vxl_u32 jobs_before,const struct mosaic_job_v1 *,vxl_u64 entered);
int mosaic_state_complete(struct mosaic_ctx *,const struct mosaic_completion *);
int mosaic_state_fault(struct mosaic_ctx *,int result);
int mosaic_evidence_delivered(struct mosaic_ctx *,vxl_u64 owner,vxl_u64 job_id,vxl_u32 offset,vxl_u32 bytes,int ok);
int mosaic_reap(struct mosaic_ctx *);
int mosaic_result_delivered(struct mosaic_ctx *,vxl_u64 owner,vxl_u32 offset,vxl_u32 bytes,int ok);
int mosaic_frame_end(struct mosaic_ctx *,vxl_u64 owner);
#ifdef __cplusplus
}
#endif
#endif
