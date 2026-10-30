#ifndef VXL_DEMO_HUNET_RUNNER_H
#define VXL_DEMO_HUNET_RUNNER_H
#include <vxl/demo_dispatch.h>
#include <vxl/hunet_fused_client.h>
struct demo_hunet_config {struct demo_store *store;const char *release_root;const void *input;size_t input_bytes;vxl_u32 roi;const struct demo_labels *labels;const struct demo_frame *source_frame;const float *historical;size_t historical_count;int historical_ort_pass,historical_r2_pass;const char *historical_probe_json;size_t historical_probe_bytes;vxl_u8 historical_probe_sha[32],historical_logits_sha[32];vxl_u8 hardware_sha[32],historical_input_sha[32],historical_model_sha[32],libm_sha[32];const vxl_u8 *gate;size_t gate_bytes;};
struct demo_hunet_adapter {struct vxl_fused_client client;const struct demo_hunet_config *config;void *state;vxl_u64 serial;int completed,first_error;};
#ifdef __cplusplus
extern "C" {
#endif
int demo_hunet_adapter_init(struct demo_hunet_adapter *,const struct demo_hunet_config *);
int demo_hunet_adapter_destroy(struct demo_hunet_adapter *);
int demo_hunet_adapter_ops(struct demo_hunet_adapter *,struct demo_gpu_ops *);
int demo_hunet_adapter_detach(void *,int);
#ifdef __cplusplus
}
#endif
#endif
