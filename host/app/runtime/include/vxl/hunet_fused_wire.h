/* SPDX-License-Identifier: MIT */
/* Shared GPU32 wire layout; no Linux host types or pointers. */
#ifndef VXL_HUNET_FUSED_WIRE_H
#define VXL_HUNET_FUSED_WIRE_H
#ifdef __KERNEL__
#include <linux/types.h>
typedef __u32 vxl_gpu_u32;
#else
#include <stdint.h>
typedef uint32_t vxl_gpu_u32;
#endif
#define VXL_FUSED_PROGRAM 9u
#define VXL_FUSED_FEATURE 8u
#define VXL_FUSED_OP 15u
#define VXL_FUSED_MAGIC 0x53554648u
#define VXL_FUSED_POLICY_PACK_ONCE 1u
typedef struct {
 vxl_gpu_u32 magic,abi,bytes,op;
 vxl_gpu_u32 first_logical_sequence,last_logical_sequence,stage_id,descriptor_count;
 vxl_gpu_u32 input_gpu,weights_gpu,bias_gpu,output_gpu;
 vxl_gpu_u32 packed_b_gpu,packed_b_bytes,stage_descriptors_gpu,owner_trace_gpu;
 vxl_gpu_u32 pack_grid_rows,gemm_grid_tiles;
 vxl_gpu_u32 ci,co,ih,iw,oh,ow,kernel_k,stride,pad,flags;
 vxl_gpu_u32 owner_trace_elements,dtype,policy,reserved;
} HunetFusedStageV1;
typedef struct {
 vxl_gpu_u32 m,n,k,mp,np,kp,offset,count;
 vxl_gpu_u32 packed_b_offset,pack_grid_base,gemm_grid_base;
 vxl_gpu_u32 original_first_sequence,original_last_sequence,reserved[3];
} HunetFusedDescriptorV1;
#endif
