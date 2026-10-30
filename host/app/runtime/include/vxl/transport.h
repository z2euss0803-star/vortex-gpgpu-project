#ifndef VXL_TRANSPORT_H
#define VXL_TRANSPORT_H
#include <vxl/core.h>
struct vxl_transport {int fd;void *mock;};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_transport_open(struct vxl_transport *,int mock);
int vxl_transport_call(struct vxl_transport *,vxl_u32,void *,vxl_u32);
void vxl_transport_close(struct vxl_transport *);
int vxl_transport_close_checked(struct vxl_transport *);
#ifdef __cplusplus
}
#endif
#endif
