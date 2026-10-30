#ifndef VXL_V2_DISPATCH_H
#define VXL_V2_DISPATCH_H
#include <vxl/v2/jobs.h>
struct vxl_v2_port;
/* OS boundary only. lock is the existing device mutex; sleep is called unlocked
 * with a revision captured while locked, closing the observe/sleep wakeup race. */
struct vxl_v2_port_ops {
 void (*lock)(void *);void (*unlock)(void *);
 int (*input)(void *,void *,vxl_u64,vxl_u32);
 int (*output)(void *,vxl_u64,const void *,vxl_u32);
 void *(*allocate)(void *,vxl_u32);void (*release)(void *,void *);
 vxl_u64 (*now_ns)(void *);
 int (*sleep)(void *,vxl_u64 revision,vxl_u64 deadline_ns);
 void (*wake)(void *);void (*kick)(void *);
 int (*snapshot)(void *,vxl_u64,struct vxl_v2_snapshot *);
};
struct vxl_v2_port {
 struct vxl_v2_ctx *ctx;const struct vxl_v2_port_ops *ops;void *os;
 vxl_u64 revision;
 /* U06 installs only the matching startup provider's object/catalog adapter. */
 int (*control)(struct vxl_v2_port *,vxl_u64,unsigned int,struct vxl_compute_v2_request *);
 void *control_context;
};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_v2_dispatch(struct vxl_v2_port *,vxl_u64 owner,unsigned int cmd,vxl_u64 request_address);
int vxl_v2_worker_locked(struct vxl_v2_port *);
vxl_u32 vxl_v2_events_locked(struct vxl_v2_port *,vxl_u64 owner);
int vxl_v2_close_locked(struct vxl_v2_port *,vxl_u64 owner);
#ifdef __cplusplus
}
#endif
#endif
