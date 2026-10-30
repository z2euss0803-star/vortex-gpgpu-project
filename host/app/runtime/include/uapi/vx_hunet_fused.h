/* SPDX-License-Identifier: MIT */
/* GPU32 wire payloads. The outer vxl_request remains 176 bytes. */
#ifndef VX_HUNET_FUSED_H
#define VX_HUNET_FUSED_H
#include <uapi/vx_hunet_hybrid.h>
#include <vxl/hunet_fused_wire.h>
struct vxl_fused_binding_v3 {
 struct vxl_hybrid_binding_v2 v2;
 __u8 fusion_contract_sha256[32],descriptors_sha256[32],layout_sha256[32],reserved[32];
};
#endif
