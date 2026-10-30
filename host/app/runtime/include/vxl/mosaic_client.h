#ifndef VXL_MOSAIC_CLIENT_H
#define VXL_MOSAIC_CLIENT_H
#include <vxl/mosaic_host.h>
#include <vxl/demo_runner.h>
#ifdef __cplusplus
extern "C" {
#endif
struct mosaic_client_metrics {vxl_u64 cold_ns,input_ns,begin_ns,submit_wait_ns,evidence_ns,output_ns,end_ns,total_ns,raw_cycles,jobs,io_ns,io_bytes,guard_ns,immutable_ns,poll_ns,handoff_ns;};
struct mosaic_client {int fd,attached,loaded,frame_active,first_error,evidence_fd;vxl_u32 generation,layout_index,epoch;vxl_u64 handles[5];struct mosaic_binding_v1 binding;const struct mosaic_program *program;const void *assets,*input;void *output;size_t assets_bytes,input_bytes,output_bytes;vxl_u8 binding_sha[32],nonce[16],last_run[16],last_evidence[512];struct mosaic_client_metrics metrics;
 /* User-space diagnostic policy; no ioctl or GPU wire structure change. */
 vxl_u32 batch_limits[6],prefix_max_jobs,frame_max_jobs;vxl_u64 prefix_max_evidence_bytes,frame_max_evidence_bytes;int prefix_stopped;
};
int mosaic_client_attach_fd(struct mosaic_client *,int borrowed_fd);
int mosaic_client_configure(struct mosaic_client *,const struct mosaic_program *,const void *,size_t,vxl_u32 layout);
int mosaic_client_load(struct mosaic_client *,const struct mosaic_manifest *);
int mosaic_client_frame(struct mosaic_client *,const void *,size_t,void *,size_t,int borrowed_evidence_fd);
int mosaic_client_run(struct mosaic_client *,const struct demo_run *,struct demo_result *);
int mosaic_client_close(struct mosaic_client *);
int mosaic_batch_next(vxl_u32 items,vxl_u32 first,vxl_u32 limit,vxl_u32 *count);
int mosaic_client_set_batch_policy(struct mosaic_client *,const vxl_u32 limits[6]);
int mosaic_client_set_prefix_budget(struct mosaic_client *,vxl_u32 max_jobs,vxl_u64 max_evidence_bytes);
int mosaic_client_set_frame_budget(struct mosaic_client *,vxl_u32 max_jobs,vxl_u64 max_evidence_bytes);
int vxl_mosaic_diag(struct mosaic_client *,const struct demo_run *,struct demo_result *,vxl_u32 convolution_limit);
#ifdef __cplusplus
}
#endif
#endif
