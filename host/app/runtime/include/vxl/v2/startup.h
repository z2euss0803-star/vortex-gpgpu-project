#ifndef VXL_V2_STARTUP_H
#define VXL_V2_STARTUP_H
#include <vxl/v2/execution.h>
int vxl_v2_boot_bind(struct vxl_v2_execution *,struct vxl_v2_hw *,struct vxl_v2_catalog *,const void *,vxl_u32,const void *,vxl_u32,const vxl_u8 catalog_sha[32],const vxl_u8 execution_sha[32]);
#endif
