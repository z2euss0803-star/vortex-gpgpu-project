#ifndef VXL_HUNET_FUSED_CLIENT_H
#define VXL_HUNET_FUSED_CLIENT_H
#include <vxl/hunet_client.h>
#include <vxl/hunet_fused.h>
struct vxl_fused_client {struct vxl_hunet_client gpu;struct vxl_fused_binding_v3 binding;};
struct vxl_opt_load_timing;
#ifdef __cplusplus
extern "C" {
#endif
int vxl_fused_client_attach_fd(struct vxl_fused_client *,int borrowed_fd);
int vxl_fused_client_open(struct vxl_fused_client *,const char *);
int vxl_fused_client_load(struct vxl_fused_client *,const char *);
int vxl_fused_client_load_timed(struct vxl_fused_client *,const char *,struct vxl_opt_load_timing *);
int vxl_fused_client_begin_cpu(struct vxl_fused_client *,struct vxl_hybrid_begin_v1 *);
int vxl_fused_client_commit_cpu(struct vxl_fused_client *,const struct vxl_hybrid_commit_v1 *);
int vxl_fused_client_evidence(struct vxl_fused_client *,vxl_u64,struct vxl_hybrid_cpu_evidence_v1 *);
int vxl_fused_client_end(struct vxl_fused_client *,struct vxl_hybrid_end_v1 *);
int vxl_fused_client_close(struct vxl_fused_client *);
#ifdef __cplusplus
}
#endif
#endif
