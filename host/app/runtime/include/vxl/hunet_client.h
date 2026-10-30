#ifndef VXL_HUNET_CLIENT_H
#define VXL_HUNET_CLIENT_H
#include <vxl/core.h>
struct vxl_hunet_client {int fd,loaded,stopped;vxl_u32 program_id;vxl_u64 handles[4],last_job;struct vxl_hunet_binding_v1 binding;int borrowed_fd;};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_hunet_client_open(struct vxl_hunet_client *,const char *);
int vxl_hunet_client_load(struct vxl_hunet_client *,const char *);
int vxl_hunet_client_load_wmma(struct vxl_hunet_client *,const char *);
int vxl_hunet_client_begin(struct vxl_hunet_client *,const struct vxl_hunet_begin_v1 *);
int vxl_hunet_client_submit(struct vxl_hunet_client *,const struct vxl_hunet_job_v1 *,vxl_u64 *);
int vxl_hunet_client_wait(struct vxl_hunet_client *,vxl_u64,vxl_u64);
int vxl_hunet_client_evidence(struct vxl_hunet_client *,vxl_u64,struct vxl_hunet_job_evidence_v1 *);
int vxl_hunet_client_reap(struct vxl_hunet_client *,vxl_u64);
int vxl_hunet_client_end(struct vxl_hunet_client *,struct vxl_hunet_end_v1 *);
int vxl_hunet_client_close(struct vxl_hunet_client *);
int vxl_hunet_client_copy(struct vxl_hunet_client *,vxl_u64,vxl_u64,void *,vxl_u64,int);
int vxl_hunet_client_reserve(struct vxl_hunet_client *,vxl_u32,vxl_u64,vxl_u64,vxl_u64 *);
int vxl_hunet_client_free(struct vxl_hunet_client *,vxl_u64);
int vxl_hunet_read_asset(const char *,const char *,void *,vxl_u64);
#ifdef __cplusplus
}
#endif
#endif
