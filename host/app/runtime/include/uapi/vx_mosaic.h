#ifndef VX_MOSAIC_H
#define VX_MOSAIC_H
#include <uapi/vx_linux.h>
#define VXL_MOSAIC_BINDING _IOWR('V',65,struct vxl_request)
#define VXL_MOSAIC_SEAL _IOWR('V',66,struct vxl_request)
#define VXL_MOSAIC_BEGIN _IOWR('V',67,struct vxl_request)
#define VXL_MOSAIC_END _IOWR('V',68,struct vxl_request)
#define VXL_MOSAIC_EVIDENCE _IOWR('V',69,struct vxl_request)
#define VXL_MOSAIC_SEAL_MAGIC 0x31534d56u
#define VXL_MOSAIC_FRAME_MAGIC 0x31464d56u
#define VXL_MOSAIC_EVIDENCE_BYTES 512u
struct vxl_mosaic_seal_v1 {
 __u32 magic,version,bytes,flags;
 __aligned_u64 handles[5]; /* code, weights, dynamic, stack, virtual args */
 __u32 layout,generation;
 __u8 binding_sha[32],nonce[16],reserved[16];
};
struct vxl_mosaic_frame_v1 {
 __u32 magic,version,bytes,flags;
 __u8 run_id[16];
 __u32 layout,input_epoch,generation,reserved0;
 __u8 input_sha[32],binding_tag[16],reserved[32];
};
#endif
