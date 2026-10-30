#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vxl/hunet_fused_wire.h>
#include "vendor/hunet/v5/include/hunet_job.h"
// Typed mathematical boundary. Counts are elements, never Linux GPU addresses.
int vxnn_hunet_pack_row(const HunetFusedStageV1*,const HunetFusedDescriptorV1*,const float*,size_t,uint16_t*,size_t,uint32_t,uint32_t,uint32_t);
int vxnn_hunet_store_fragment(const HunetFusedDescriptorV1*,float*,size_t,const float*,size_t,uint32_t,uint32_t,uint32_t,const float*,size_t);
int vxnn_hunet_gn_stats(const float*,size_t,float,float*,size_t);
struct VxnnHunetRead { const float* data; size_t count; };
struct VxnnHunetWrite { float* data; size_t count; };
struct VxnnHunetScalarBuffers {
 VxnnHunetRead x,w,gamma,beta,stats;
 VxnnHunetWrite y;
};
// The legacy address words MUST be zero: address translation belongs to the
// trusted device adapter, never to this typed arithmetic boundary. GELU stays CPU.
int vxnn_hunet_scalar(const HunetJobV1*,const VxnnHunetScalarBuffers*,uint32_t first,uint32_t count);
