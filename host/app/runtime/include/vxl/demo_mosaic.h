#ifndef VXL_DEMO_MOSAIC_H
#define VXL_DEMO_MOSAIC_H
#include <vxl/mosaic_client.h>
struct demo_mosaic_adapter {struct mosaic_client client;struct demo_store *store;const struct mosaic_program *program;const void *assets,*input;void *output;size_t assets_bytes,frame_bytes;vxl_u32 layout;struct demo_result result;vxl_u64 serial;int completed,first_error;};
#ifdef __cplusplus
extern "C" {
#endif
int demo_mosaic_adapter_detach(void *,int);
int demo_mosaic_ops(struct demo_mosaic_adapter *,struct demo_gpu_ops *);
int demo_mosaic_run(struct demo_session *,const struct demo_run *,struct demo_result *);
#ifdef __cplusplus
}
#endif
#endif
