#ifndef VXL_DEMO_DISPATCH_H
#define VXL_DEMO_DISPATCH_H
#include <vxl/demo_frame.h>
#include <vxl/core.h>
#include <uapi/vx_demo.h>
struct demo_dispatch_control {int (*open)(void *);int (*close)(void *,int);int (*info)(void *,int,struct vxl_request *);int (*select)(void *,int,vxl_u32,vxl_u32,vxl_u32 *);int (*display)(void *,const struct demo_result *,enum demo_layout);void *context;};
struct demo_dispatch {struct demo_session *session;struct demo_gpu_ops apps[2];int (*detach[2])(void *,int);struct demo_dispatch_control control;enum demo_app active;vxl_u32 profile;};
#ifdef __cplusplus
extern "C" {
#endif
int demo_dispatch_init(struct demo_session *,struct demo_store *,vxl_u32,struct demo_dispatch *);
int demo_select_app(struct demo_session *,enum demo_app);
int demo_switch_idle(struct demo_session *,enum demo_app);
int demo_request_display(struct demo_session *,const char *,enum demo_layout);
int demo_dispatch_posix_info(void *,int,struct vxl_request *);
int demo_dispatch_posix_select(void *,int,vxl_u32,vxl_u32,vxl_u32 *);
#ifdef __cplusplus
}
#endif
#endif
