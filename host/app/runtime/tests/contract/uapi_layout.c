#include <uapi/vx_linux.h>
#include <stdint.h>
#include <stddef.h>
_Static_assert(sizeof(struct vxl_request)==176,"exact v1 size");
_Static_assert(_Alignof(struct vxl_request)==8,"u64 alignment");
const uint32_t vxl_layout[] __attribute__((section(".vxl_abi")))={
 sizeof(struct vxl_request),_Alignof(struct vxl_request),
 offsetof(struct vxl_request,handle),offsetof(struct vxl_request,user_ptr),
 offsetof(struct vxl_request,job_id),offsetof(struct vxl_request,domain),
 offsetof(struct vxl_request,result),offsetof(struct vxl_request,reserved2),
 VXL_GET_INFO,VXL_ALLOC,VXL_RESERVE,VXL_FREE,VXL_COPY_TO,VXL_COPY_FROM,VXL_COPY_DEVICE,VXL_SUBMIT,VXL_WAIT,VXL_GET_STATUS,VXL_REAP
};
