#ifndef VXL_HUNET_H
#define VXL_HUNET_H
#include <vxl/core.h>
#include <vxl/mgmt_timing.h>
#include <vxl/digest.h>
#include <uapi/vx_hunet.h>
#include <vxl/hunet_hybrid.h>
#include <vxl/hunet_fused.h>
#include <vxl/hunet_opt.h>
#define VXL_HUNET_CODE 0x80000000ULL
#define VXL_HUNET_WEIGHTS 0x80800000ULL
#define VXL_HUNET_WS 0x85000000ULL
#define VXL_HUNET_WS_BYTES 33976320ULL
#define VXL_HUNET_ARGS 0x10010040ULL
#define VXL_HUNET_INPUT 0x85000040ULL
#define VXL_HUNET_INPUT_BYTES 196608u
#define VXL_HUNET_LOGITS 0x86bf5f80ULL
#define VXL_HUNET_LOGITS_BYTES 32420u
#define VXL_HUNET_AUX 0x87064100ULL
#define VXL_HUNET_NETWORK_MS 180000u
struct vxl_hunet_sha { vxl_u32 h[8]; vxl_u64 count; vxl_u8 block[64]; };
struct vxl_hunet_digest_ctx {struct vxl_digest_ctx modern;struct vxl_hunet_sha legacy;vxl_u32 enabled;struct vxl_digest_audit *audit;};
struct vxl_hunet_program { vxl_u32 id,wire_bytes,ro_bytes; vxl_u8 wire_sha[32],ro_sha[32]; };
struct vxl_hunet_release {
 struct vxl_hunet_program program[2];
 struct vxl_hunet_binding_v1 binding;
 vxl_u8 binding_sha[32];
};
/* Trusted kernel callback only; addresses never come directly from a user. */
struct vxl_hunet_io { void *context; int (*rw)(void *,vxl_u64,void *,vxl_u64,int); vxl_u64 (*now_ns)(void *); };
struct vxl_hunet_hw_snapshot {struct vxl_event event;vxl_u32 watchdog,elapsed_raw,cdma_status,exitcode[4],crt_guard_flags,exit_valid,crt_checked;};
/* Captured under the device mutex after payload copy, before envelope copy. */
struct vxl_hunet_delivery {vxl_u64 owner;vxl_u32 generation,command;union {struct vxl_hunet_job_evidence_v1 evidence;struct vxl_hunet_end_v1 end;} payload;};
struct vxl_hunet_ctx {
 struct vxl_opt_ctx opt;
 /* Kernel caller sets this only around one device-mutex protected call. */
 vxl_u32 opt_load_locked,opt_load_generation;vxl_u64 opt_load_owner;
 struct vxl_mgmt_row *mgmt_row; /* optional, scoped to the locked transaction */
 vxl_u32 digest_enabled;enum vxl_digest_backend digest_backend;
 struct vxl_submit_time *submit_time; /* optional internal diagnostic; never UAPI */
 struct vxl_engine *engine;
 const struct vxl_hunet_release *release;
 struct vxl_hunet_io io;
 vxl_u32 sealed,program_id,active,next_sequence,evidence_delivered,program_sealed;
 vxl_u64 seal_owner;vxl_u32 seal_generation;
 vxl_u64 handles[4];
 vxl_u8 args_shadow[128],run_id[16],input_sha[32];
 vxl_u32 roi_index,sample_kind,sample_index;
 vxl_u8 *input_snapshot;
 vxl_u8 seen[256][32];vxl_u32 seen_count;
 struct vxl_hunet_job_evidence_v1 evidence;
 vxl_u32 end_pending;
 struct vxl_hunet_end_v1 end_receipt;
 struct vxl_hybrid_ctx hybrid;
 struct vxl_fused_ctx fused;
};
static inline vxl_u64 vxl_hunet_mgmt_now(struct vxl_hunet_ctx *c){return c&&c->mgmt_row?c->io.now_ns(c->io.context):0;}
static inline void vxl_hunet_mgmt_mark(struct vxl_hunet_ctx *c,enum vxl_mgmt_field f,vxl_u64 begin,vxl_u64 bytes){
 if(c&&c->mgmt_row)(void)vxl_mgmt_add(c->mgmt_row,f,begin,c->io.now_ns(c->io.context),bytes);
}
struct vxl_engine;
struct vxl_prepared;
struct vxl_ops;
#ifdef __cplusplus
extern "C" {
#endif
int vxl_hunet_payload_validate(vxl_u32 command,const void *payload,size_t bytes);
int vxl_engine_init_profile(struct vxl_engine *,const struct vxl_ops *,void *,vxl_u32,vxl_u32);
void vxl_hunet_release_defaults(struct vxl_hunet_release *);
void vxl_hunet_sha_init(struct vxl_hunet_sha *);
void vxl_hunet_sha_update(struct vxl_hunet_sha *,const void *,vxl_u64);
void vxl_hunet_sha_final(struct vxl_hunet_sha *,vxl_u8 [32]);
void vxl_hunet_hash(const void *,vxl_u64,vxl_u8 [32]);
int vxl_hunet_hash_gpu(struct vxl_hunet_ctx *,vxl_u64,vxl_u64,vxl_u8 [32]);
int vxl_hunet_digest_init(struct vxl_hunet_ctx *,struct vxl_hunet_digest_ctx *);
int vxl_hunet_digest_update(struct vxl_hunet_digest_ctx *,const void *,vxl_u64);
int vxl_hunet_digest_final(struct vxl_hunet_digest_ctx *,vxl_u8 [32]);
void vxl_hunet_digest_abort(struct vxl_hunet_digest_ctx *);
int vxl_hunet_digest_hash(struct vxl_hunet_ctx *,const void *,vxl_u64,vxl_u8 [32]);
int vxl_hunet_layout_init(struct vxl_hunet_ctx *,struct vxl_engine *,const struct vxl_hunet_release *);
int vxl_hunet_check_access(const struct vxl_hunet_ctx *,vxl_u64,vxl_u64,vxl_u32);
int vxl_hunet_check_program(struct vxl_hunet_ctx *,vxl_u32);
int vxl_hunet_io_bytes(struct vxl_hunet_ctx *,vxl_u64,void *,vxl_u64,int);
int vxl_hunet_allocate_check(struct vxl_hunet_ctx *,const struct vxl_request *,int);
int vxl_hunet_guards(struct vxl_hunet_ctx *,int initialize);
const vxl_u32 *vxl_hunet_catalog_job(vxl_u32 sequence);
const struct vxl_hunet_matrix_v1 *vxl_hunet_catalog_matrix(vxl_u32 index);
const vxl_u8 *vxl_hunet_catalog_aux(void);
int vxl_hunet_validate_spans(const struct vxl_hunet_job_v1 *,const struct vxl_hunet_matrix_v1 *);
int vxl_hunet_validate_job(const struct vxl_hunet_ctx *,const struct vxl_hunet_job_v1 *,vxl_u32);
int vxl_hunet_seal(struct vxl_hunet_ctx *,const struct vxl_hunet_seal_v1 *);
int vxl_hunet_seal_mode(struct vxl_hunet_ctx *,const struct vxl_hunet_seal_v1 *,vxl_u32,const vxl_u8 [32]);
int vxl_hunet_begin(struct vxl_hunet_ctx *,const struct vxl_hunet_begin_v1 *,const struct vxl_hunet_io *);
int vxl_hunet_end(struct vxl_hunet_ctx *,struct vxl_hunet_end_v1 *);
int vxl_hunet_stage_args(struct vxl_hunet_ctx *,const void *,size_t);
int vxl_hunet_command(struct vxl_hunet_ctx *,vxl_u32,struct vxl_request *);
int vxl_hunet_owner_trace(struct vxl_hunet_ctx *,vxl_u32,vxl_u32 counts[4]);
int vxl_hunet_prepare(struct vxl_hunet_ctx *,struct vxl_prepared *,vxl_u32);
int vxl_hunet_before_start(struct vxl_hunet_ctx *,vxl_u64,vxl_u32);
int vxl_hunet_accept_done(struct vxl_hunet_ctx *,const struct vxl_hunet_hw_snapshot *,vxl_u64,struct vxl_hunet_job_evidence_v1 *);
int vxl_hunet_after_ack(struct vxl_hunet_ctx *,vxl_u32,vxl_u32,vxl_u64);
int vxl_hunet_crt_snapshot(struct vxl_hunet_ctx *,struct vxl_hunet_hw_snapshot *);
int vxl_hunet_exit_snapshot(struct vxl_hunet_ctx *,struct vxl_hunet_hw_snapshot *);
void vxl_hunet_record_snapshot(struct vxl_hunet_ctx *,const struct vxl_hunet_hw_snapshot *,vxl_u64);
int vxl_hunet_get_evidence(struct vxl_hunet_ctx *,vxl_u64,struct vxl_hunet_job_evidence_v1 *);
void vxl_hunet_evidence_delivered(struct vxl_hunet_ctx *,const struct vxl_hunet_job_evidence_v1 *);
void vxl_hunet_delivery_capture(struct vxl_hunet_ctx *,vxl_u32,struct vxl_hunet_delivery *);
void vxl_hunet_delivery_finish(struct vxl_hunet_ctx *,const struct vxl_hunet_delivery *);
#ifdef __cplusplus
}
#endif
#endif
