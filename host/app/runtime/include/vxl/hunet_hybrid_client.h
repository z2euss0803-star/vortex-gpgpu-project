#ifndef VXL_HUNET_HYBRID_CLIENT_H
#define VXL_HUNET_HYBRID_CLIENT_H
#include <vxl/hunet_client.h>
struct vxl_hybrid_client {struct vxl_hunet_client gpu;struct vxl_hybrid_binding_v2 binding;};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_hybrid_client_open(struct vxl_hybrid_client *,const char *);
int vxl_hybrid_client_load(struct vxl_hybrid_client *,const char *);
int vxl_hybrid_client_begin_cpu(struct vxl_hybrid_client *,struct vxl_hybrid_begin_v1 *);
int vxl_hybrid_client_commit_cpu(struct vxl_hybrid_client *,const struct vxl_hybrid_commit_v1 *);
int vxl_hybrid_client_evidence(struct vxl_hybrid_client *,vxl_u64,struct vxl_hybrid_cpu_evidence_v1 *);
int vxl_hybrid_client_end(struct vxl_hybrid_client *,struct vxl_hybrid_end_v1 *);
int vxl_hybrid_client_close(struct vxl_hybrid_client *);
#ifdef __cplusplus
}
#endif
#endif
