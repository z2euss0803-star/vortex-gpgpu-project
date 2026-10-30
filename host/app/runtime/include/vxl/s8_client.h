#ifndef VXL_S8_CLIENT_H
#define VXL_S8_CLIENT_H
#include <vxl/core.h>
#include <vxl/s8_wire.h>
struct vxl_s8_client_object {vxl_u32 gpu,bytes,purpose;vxl_u64 handle;const vxl_u8 *initial;};
/* Boundary injection is for CPU tests. The product session uses ioctl,
 * CLOCK_MONOTONIC_RAW and durable O_EXCL files, never the mock transport. */
struct vxl_s8_client_io {
 void *context;
 int (*call)(void *,int,vxl_u32,struct vxl_request *);
 int (*save)(void *,const char *,const void *,vxl_u32);
 int (*trace)(void *,const char *,int post_reap);
 int (*seal)(void *,int post_reap);
 vxl_u64 (*now)(void *);
 int (*event)(void *,const char *,vxl_u64,vxl_u64,int);
};
struct vxl_s8_client {int fd,stopped,loaded;vxl_u32 generation,sequence,captured;vxl_u64 job_id;struct vxl_s8_client_object objects[53];struct vxl_s8_client_io io;};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_s8_object_contract(unsigned,struct vxl_s8_client_object *);
int vxl_s8_client_bind(struct vxl_s8_client *);
void vxl_s8_client_unbind(struct vxl_s8_client *);
int vxl_s8_client_prepare(struct vxl_s8_client *);
int vxl_s8_run_case(int,const struct vxl_s8_job_v1 *,const char *evidence_dir);
#ifdef __cplusplus
}
#endif
#endif
