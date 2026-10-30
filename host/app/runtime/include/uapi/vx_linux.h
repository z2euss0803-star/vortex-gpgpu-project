/* SPDX-License-Identifier: MIT */
#ifndef VX_LINUX_H
#define VX_LINUX_H
#include <linux/types.h>
#include <linux/ioctl.h>
#define VXL_ABI_MAJOR 1
/* All v1 commands use this exact 176-byte envelope. No raw MMIO/physical API. */
struct vxl_request {
 __u32 abi_major, struct_size, flags, reserved;
 __aligned_u64 handle, handle2, offset, offset2, length, user_ptr;
 __aligned_u64 job_id, deadline_ms, wait_ms, address, capacity, used;
 __u32 domain, purpose, gpu_address, state;
 __s32 result;
 __u32 generation, profile_id, capability, alignment;
 __u32 hwstatus, errors, pending_w, pending_r, jobs, trust_id, reserved2;
};
#define VXL_GET_INFO    _IOWR('V', 1, struct vxl_request)
#define VXL_ALLOC       _IOWR('V', 2, struct vxl_request)
#define VXL_RESERVE     _IOWR('V', 3, struct vxl_request)
#define VXL_FREE        _IOWR('V', 4, struct vxl_request)
#define VXL_COPY_TO     _IOWR('V', 5, struct vxl_request)
#define VXL_COPY_FROM   _IOWR('V', 6, struct vxl_request)
#define VXL_COPY_DEVICE _IOWR('V', 7, struct vxl_request)
#define VXL_SUBMIT      _IOWR('V', 8, struct vxl_request)
#define VXL_WAIT        _IOWR('V', 9, struct vxl_request)
#define VXL_GET_STATUS  _IOWR('V', 10, struct vxl_request)
#define VXL_REAP        _IOWR('V', 11, struct vxl_request)
#define VXL_HUNET_SEAL        _IOWR('V', 12, struct vxl_request)
#define VXL_HUNET_BEGIN_ROI   _IOWR('V', 13, struct vxl_request)
#define VXL_HUNET_END_ROI     _IOWR('V', 14, struct vxl_request)
#define VXL_HUNET_GET_EVIDENCE _IOWR('V', 15, struct vxl_request)
#define VXL_HUNET_GET_BINDING _IOWR('V', 16, struct vxl_request)
#define VXL_PS 1
#define VXL_PL 2
#define VXL_CODE 1
#define VXL_ARGS 2
#define VXL_DATA 3
#include <uapi/vx_hunet.h>
#include <uapi/vx_hunet_hybrid.h>
#endif
