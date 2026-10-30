#ifndef VXL_V2_CONTROL_H
#define VXL_V2_CONTROL_H
#include <vxl/v2/dispatch.h>
struct vxl_v2_controls {struct vxl_v2_caps caps;const struct vxl_v2_file_ops *files;};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_v2_control(struct vxl_v2_port *,vxl_u64,unsigned int,struct vxl_compute_v2_request *);
#ifdef __cplusplus
}
#endif
#endif
