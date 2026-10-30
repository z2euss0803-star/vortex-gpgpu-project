#ifndef VXL_HUNET_FUSED_H
#define VXL_HUNET_FUSED_H
#include <vxl/core.h>
#include <uapi/vx_hunet_fused.h>
struct vxl_fused_meta {
 vxl_u32 first,last,stage,op,trace_elements,pack_rows,gemm_tiles;
};
struct vxl_hunet_ctx;
struct vxl_fused_release {
 vxl_u32 wire_bytes,ro_bytes;
 vxl_u8 wire_sha[32],ro_sha[32];
 struct vxl_fused_binding_v3 binding;
 vxl_u8 binding_sha[32];
};
struct vxl_fused_ctx {
 const struct vxl_fused_release *release;
 struct vxl_fused_meta pending;
 vxl_u32 acked;
};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_fused_payload_validate(vxl_u32 command,const void *payload,size_t bytes);
const vxl_u32 *vxl_fused_catalog_job(vxl_u32 first);
int vxl_fused_decode_meta(const void *,vxl_u32,struct vxl_fused_meta *);
int vxl_fused_validate_job(const struct vxl_hunet_ctx *,const void *,vxl_u32,struct vxl_fused_meta *);
int vxl_fused_check_program(struct vxl_hunet_ctx *);
int vxl_fused_restore_descriptors(struct vxl_hunet_ctx *);
int vxl_fused_init(struct vxl_hunet_ctx *,const struct vxl_fused_release *);
int vxl_fused_seal(struct vxl_hunet_ctx *,const struct vxl_hunet_seal_v1 *);
int vxl_fused_begin(struct vxl_hunet_ctx *);
int vxl_fused_gpu_completed(struct vxl_hunet_ctx *,const struct vxl_fused_meta *);
int vxl_fused_owner_trace(struct vxl_hunet_ctx *,const struct vxl_fused_meta *,vxl_u32 [4]);
#ifdef __cplusplus
}
#endif
#endif
