#ifndef VXL_HUNET_OPT_CLIENT_H
#define VXL_HUNET_OPT_CLIENT_H
#include <vxl/hunet_client.h>
#include <vxl/hunet_opt.h>
struct vxl_opt_load_timing;
struct vxl_opt_client {
 struct vxl_hunet_client gpu;struct vxl_opt_binding_v4 binding;
 const struct vxl_opt_catalog *catalog;enum vxl_digest_backend digest_backend;struct vxl_digest_audit digest_audit;
};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_opt_client_open(struct vxl_opt_client *,const char *,const struct vxl_opt_catalog *,enum vxl_digest_backend);
int vxl_opt_client_load_timed(struct vxl_opt_client *,const char *,struct vxl_opt_load_timing *);
int vxl_opt_client_hash(struct vxl_opt_client *,const void *,size_t,vxl_u8 [32]);
int vxl_opt_client_begin_cpu(struct vxl_opt_client *,struct vxl_hybrid_begin_v1 *);
int vxl_opt_client_commit_cpu(struct vxl_opt_client *,const struct vxl_hybrid_commit_v1 *);
int vxl_opt_client_evidence(struct vxl_opt_client *,vxl_u64,struct vxl_hybrid_cpu_evidence_v1 *);
int vxl_opt_client_end(struct vxl_opt_client *,struct vxl_hybrid_end_v1 *);
int vxl_opt_client_close(struct vxl_opt_client *);
#ifdef __cplusplus
}
#endif
#endif
