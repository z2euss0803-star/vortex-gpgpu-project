#ifndef VXL_DEMO_DUAL_RUNTIME_H
#define VXL_DEMO_DUAL_RUNTIME_H
#include <vxl/demo_mosaic.h>
#include <vxl/demo_hunet_runner.h>
struct demo_dual_config {struct demo_store *store;const struct mosaic_program *mosaic_program;const void *mosaic_assets,*mosaic_input;void *mosaic_output;size_t mosaic_assets_bytes,mosaic_frame_bytes;vxl_u32 mosaic_layout;const struct demo_hunet_config *hunet;const char *viewer_socket;};
struct demo_dual_runtime {struct demo_dispatch dispatch;struct demo_mosaic_adapter mosaic;struct demo_hunet_adapter hunet;const struct demo_dual_config *config;};
#ifdef __cplusplus
extern "C" {
#endif
/* Caller supplies a zero-initialized runtime and keeps config/assets alive. */
int demo_dual_runtime_init(struct demo_dual_runtime *,struct demo_session *,const struct demo_dual_config *);
int demo_dual_runtime_destroy(struct demo_dual_runtime *);
#ifdef __cplusplus
}
#endif
#endif
