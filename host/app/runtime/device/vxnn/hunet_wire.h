#pragma once
#include <stddef.h>
#include <stdint.h>
#include <vxl/hunet_fused_wire.h>
// Private schema of NEW controlled program 0x10010. Never a legacy program9 wire.
// Addresses start zero and are relocated only by the v2 driver from eight refs.
enum VxnnHunetRef { VXH_X,VXH_W,VXH_BIAS,VXH_GAMMA,VXH_BETA,VXH_STATS,VXH_Y,VXH_WORK };
struct VxnnHunetArgs {
 uint32_t abi,bytes,op,flags,address[8],extent[8];
 uint32_t ci,co,ih,iw,oh,ow,k,stride,pad,groups,n_div,epsilon_bits;
};
struct VxnnHunetShape { uint32_t required[8],tasks,pack_rows,gemm_tiles,packed_bytes,trace_offset,descriptor_count; };
static_assert(sizeof(VxnnHunetArgs)==128,"controlled HUNet args128");
static_assert(offsetof(VxnnHunetArgs,address)==16&&offsetof(VxnnHunetArgs,extent)==48&&offsetof(VxnnHunetArgs,ci)==80,"sealed schema offsets");
// Descriptors are a separate already-bounded READ reference. This function
// never dereferences GPU addresses, accepts CPU pointers only for descriptors,
// and publishes shape only after every descriptor/span has been validated.
int vxnn_hunet_validate(const VxnnHunetArgs*,const HunetFusedDescriptorV1*,size_t,bool relocated,VxnnHunetShape*);
