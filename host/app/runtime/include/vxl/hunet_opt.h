#ifndef VXL_HUNET_OPT_H
#define VXL_HUNET_OPT_H
#include <vxl/core.h>
#include <vxl/digest.h>
#include <uapi/vx_hunet_opt.h>
struct vxl_opt_meta {vxl_u32 first,last,first_stage,last_stage,op,block_id,owner_tiles,materialized_stages;};
struct vxl_opt_catalog {
 vxl_u32 gpu_jobs,cpu_jobs,logical_jobs,tile_n,runtime_flags;
 const vxl_u32 (*jobs)[32];const struct vxl_opt_meta *metadata;const vxl_u32 (*cpu)[2];
 const vxl_u8 *matrix_data,*graph_data;vxl_u32 matrix_bytes,graph_bytes;
 vxl_u8 jobs_sha[32],graph_sha[32],matrix_sha[32],descriptors_sha[32],layout_sha[32];
};
struct vxl_opt_release {
 vxl_u32 wire_bytes,ro_bytes;
 vxl_u8 wire_sha[32],ro_sha[32];
 struct vxl_opt_binding_v4 binding;
 vxl_u8 binding_sha[32];
 const struct vxl_opt_catalog *catalog;
 enum vxl_digest_backend digest_backend;
};
struct vxl_opt_ctx {const struct vxl_opt_release *release;struct vxl_opt_meta pending;vxl_u32 acked;struct vxl_digest_audit digest_audit;};
struct vxl_hunet_ctx;
#ifdef __cplusplus
extern "C" {
#endif
const vxl_u32 *vxl_opt_catalog_job(const struct vxl_opt_catalog *,vxl_u32);
int vxl_opt_check_program(struct vxl_hunet_ctx *);
int vxl_opt_prepare_control(struct vxl_hunet_ctx *);
int vxl_opt_guards(struct vxl_hunet_ctx *,int);
int vxl_opt_owner_expected(const struct vxl_opt_catalog *,const struct vxl_opt_meta *,vxl_u32,vxl_u32 *);
int vxl_opt_payload_validate(vxl_u32,const void *,size_t);
int vxl_opt_graph_validate(const void *,size_t,const void *,size_t);
int vxl_opt_validate_job(const struct vxl_hunet_ctx *,const void *,vxl_u32,struct vxl_opt_meta *);
int vxl_opt_init(struct vxl_hunet_ctx *,const struct vxl_opt_release *);
int vxl_opt_seal(struct vxl_hunet_ctx *,const struct vxl_hunet_seal_v1 *);
int vxl_opt_begin(struct vxl_hunet_ctx *);
int vxl_opt_restore_descriptors(struct vxl_hunet_ctx *);
int vxl_opt_gpu_completed(struct vxl_hunet_ctx *,const struct vxl_opt_meta *);
int vxl_opt_owner_trace(struct vxl_hunet_ctx *,const struct vxl_opt_meta *,vxl_u32 [4]);
int vxl_opt_load_fastwait_allowed(const struct vxl_hunet_ctx *,vxl_u32,vxl_u64,vxl_u64,vxl_u64);
const struct vxl_opt_catalog *vxl_opt_catalog_default(void);
int vxl_opt_descriptor_matches(const struct vxl_opt_catalog *,const void *,const void *,size_t);
#ifdef __cplusplus
}
#endif
#endif
