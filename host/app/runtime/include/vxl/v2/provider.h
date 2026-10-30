#ifndef VXL_V2_PROVIDER_H
#define VXL_V2_PROVIDER_H
#include <vxl/v2/jobs.h>
struct vxl_v2_boot_identity {struct vxl_v2_provider provider;char compatible[64];};
struct vxl_v2_hw {
 struct vxl_v2_ctx *ctx;struct vxl_v2_boot_identity identity;
 struct vxl_cdma_io cdma;void *os;void *ps;vxl_u64 ps_dma;
 int (*locked)(void *);int (*ready)(void *);
 vxl_u32 (*read)(void *,vxl_u32);void (*write)(void *,vxl_u32,vxl_u32);
 void (*memory_barrier)(void *,int write);vxl_u32 dma_completed,tex_state[6];
 vxl_u64 (*now_ns)(void *);
 vxl_u64 cdma_ns,io_bytes;
};
struct vxl_v2_tex {vxl_u32 enabled,bilinear,ref_index,width,height,pitch;};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_v2_s8_admit(const struct vxl_v2_boot_identity *,const struct vxl_v2_catalog_entry *);
int vxl_v2_p8_admit(const struct vxl_v2_boot_identity *,const struct vxl_v2_catalog_entry *);
int vxl_v2_hw_idle(struct vxl_v2_hw *);
int vxl_v2_native_guards(struct vxl_v2_hw *,struct vxl_v2_object *,int);
int vxl_v2_prepare_tex(struct vxl_v2_hw *,const struct vxl_v2_job *,const struct vxl_v2_tex *);
extern const struct vxl_v2_memory_ops vxl_v2_native_memory_ops;
#ifdef __cplusplus
}
#endif
#endif
