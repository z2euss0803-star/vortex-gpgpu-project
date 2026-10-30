#ifndef VXL_S8_H
#define VXL_S8_H
#include <vxl/core.h>
#include <vxl/s8_wire.h>
#include <vxl/s8_timing.h>
struct vxl_s8_program;
struct vxl_s8_trace;
struct vxl_s8_io {void *context;int (*rw)(void *,vxl_u64,void *,vxl_u64,int);vxl_u64 (*now_ns)(void *);};
enum vxl_s8_error_class {VXL_S8_NO_ERROR,VXL_S8_HOST_REJECT,VXL_S8_EXPECTED_PROTOCOL_ERROR,VXL_S8_NUMERIC_MISMATCH,VXL_S8_UNEXPECTED_PROTOCOL,VXL_S8_RUNTIME_FAULT,VXL_S8_TIMING_INVALID,VXL_S8_IDENTITY_MISMATCH,VXL_S8_RESOURCE_REFUSED};
struct vxl_s8_hw_snapshot {
 struct vxl_event event;
 vxl_u64 job_id;
 vxl_u64 elapsed_address;
 vxl_u32 generation,sequence,elapsed_raw,elapsed_valid,pre_ack,watchdog,cdma_status,jobs_before;
 vxl_u32 exitcode[4],exit_valid,guard_valid,immutable_valid,input_valid,ack_valid;
 vxl_u32 tex_pending,ack_idle,ack_ready,identity_valid,metadata_valid,metadata_count;
 vxl_u8 crt_guards[192];
};
struct vxl_s8_capture {vxl_u64 gpu;vxl_u32 bytes,delivered;};
struct vxl_s8_ctx {
 struct vxl_engine *engine;
 vxl_u32 generation,next_sequence,nonce_locked;
 vxl_u8 run_id[16],binding_tag[16];
 struct vxl_s8_job_v1 job_args;
 const struct vxl_s8_program *program;
 struct vxl_s8_io io;
 vxl_u64 restore_bytes,restore_ops,code_check_bytes,input_hash_bytes,guard_init_bytes,guard_check_bytes;
 vxl_u64 results_check_bytes;
 struct vxl_s8_hw_snapshot hw;
 struct vxl_s8_capture capture[192];
 vxl_u32 capture_count,capture_pending,delivery_fault,trace_delivered,error_class;
 struct vxl_s8_timing times;
 struct vxl_s8_trace *trace;
 vxl_u64 submit_enter_ns;
 int (*observe)(struct vxl_s8_ctx *,vxl_u32,vxl_u64,vxl_u64,vxl_u64,int);
};
struct vxl_s8_region { vxl_u64 begin,size; };
struct vxl_s8_layout {
 struct vxl_s8_region regions[8]; /* CODE ARGS A B OUT IDENT CTRL STACK */
 vxl_u64 runtime_bytes,tls_base;
 vxl_u32 tbss_bytes,tbss_stride,tbss_align,tdata_bytes,static_stack_bytes,objects_count;
};
struct vxl_s8_mutable { vxl_u32 offset,length; };
struct vxl_s8_input_digest {vxl_u32 case_id,slot;vxl_u8 a_sha[32],b_sha[32];};
struct vxl_s8_program {
 const vxl_u8 *flat;vxl_u32 flat_bytes,entry;
 struct vxl_s8_layout layout;
 const struct vxl_s8_mutable *mutable_ranges;vxl_u32 mutable_count;
 struct vxl_s8_binding_v1 binding;
 vxl_u8 flat_sha[32],binding_sha[32];
 const struct vxl_s8_input_digest *inputs;vxl_u32 input_count;
};
struct vxl_engine;
struct vxl_prepared;
#ifdef __cplusplus
extern "C" {
#endif
int vxl_s8_layout_check(const struct vxl_s8_layout *);
vxl_u32 vxl_s8_guard_byte(vxl_u32 region,vxl_u32 slot,vxl_u32 index,vxl_u32 j);
int vxl_s8_guard_check(const vxl_u8 *,vxl_u32 region,vxl_u32 slot,vxl_u32 index);
int vxl_s8_gpu_bus(vxl_u32 gpu,vxl_u64 length,vxl_u64 *bus);
int vxl_s8_ps_user_range(vxl_u64 bus,vxl_u64 length);
int vxl_s8_check_unchanged(const vxl_u8 *expected,const vxl_u8 *actual,vxl_u32 bytes,const struct vxl_s8_mutable *,vxl_u32 count);
int vxl_s8_context_init(struct vxl_s8_ctx *,struct vxl_engine *,const vxl_u8 binding_tag[16]);
void vxl_s8_owner_open(struct vxl_s8_ctx *);
vxl_u32 vxl_s8_job_crc(const struct vxl_s8_job_v1 *);
int vxl_s8_validate_job(struct vxl_s8_ctx *,const struct vxl_s8_job_v1 *);
int vxl_s8_canonical_job(struct vxl_s8_ctx *,const struct vxl_s8_job_v1 *,struct vxl_s8_job_v1 *);
int vxl_s8_accept_job(struct vxl_s8_ctx *,const struct vxl_s8_job_v1 *);
int vxl_s8_reserve_check(const struct vxl_request *,int reserve);
int vxl_s8_program_register(struct vxl_s8_ctx *,const struct vxl_s8_program *);
int vxl_s8_io_bytes(struct vxl_s8_ctx *,vxl_u64,void *,vxl_u64,int);
int vxl_s8_immutable_check(struct vxl_s8_ctx *);
int vxl_s8_restore(struct vxl_engine *,const struct vxl_s8_layout *);
int vxl_s8_prepare(struct vxl_engine *,struct vxl_prepared *);
int vxl_s8_guards(struct vxl_s8_ctx *,const struct vxl_s8_job_v1 *,int initialize);
int vxl_s8_inputs_check(struct vxl_s8_ctx *,const struct vxl_s8_job_v1 *);
int vxl_s8_results_check(struct vxl_s8_ctx *);
vxl_u32 vxl_s8_input_bytes(const struct vxl_s8_job_v1 *,unsigned which);
int vxl_s8_complete(struct vxl_engine *,const struct vxl_event *);
int vxl_s8_capture_range(struct vxl_s8_ctx *,vxl_u64 gpu,vxl_u64 length);
int vxl_s8_can_reap(const struct vxl_s8_ctx *);
void vxl_s8_reaped(struct vxl_s8_ctx *);
void vxl_s8_delivery_fault(struct vxl_s8_ctx *,int error);
#ifdef __cplusplus
}
#endif
#endif
