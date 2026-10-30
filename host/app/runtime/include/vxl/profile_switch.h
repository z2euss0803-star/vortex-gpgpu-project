#ifndef VXL_PROFILE_SWITCH_H
#define VXL_PROFILE_SWITCH_H
#include <vxl/engine.h>
#include <uapi/vx_demo.h>
/* Driver mutex held; prepare/commit are kernel callbacks, never user data. */
struct vxl_profile_factory {int enabled;void *context;int (*idle)(void *,struct vxl_engine *);int (*prepare)(void *,vxl_u32,vxl_u32,struct vxl_engine **);void (*discard)(void *,struct vxl_engine *);void (*commit)(void *,struct vxl_engine *);};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_select_profile(struct vxl_engine *,vxl_u64 owner,vxl_u32 target,vxl_u32 expected_generation);
int vxl_demo_hunet_release(struct vxl_engine *,vxl_u64);
int vxl_select_request(struct vxl_engine *,vxl_u64,struct vxl_request *);
#ifdef __cplusplus
}
#endif
#endif
