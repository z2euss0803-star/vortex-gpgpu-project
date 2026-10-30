#ifndef VXL_HUNET_OPT_WIRE_H
#define VXL_HUNET_OPT_WIRE_H
#include <vxl/hunet_fused_wire.h>
#define VXL_OPT_PROGRAM 10u
#define VXL_OPT_FEATURES 31u
#define VXL_OPT_JOB_MAGIC 0x32474c48u
#define VXL_OPT_DESC_MAGIC 0x32444748u
#define VXL_OPT_POLICY 2u
#define VXL_OPT_GN_NORM_OP 16u
#define VXL_OPT_GRAPH_BLOCK_OP 17u
struct vxl_opt_graph_job_v2 {
 vxl_gpu_u32 magic,version,bytes,opcode,first,last,first_stage,last_stage;
 vxl_gpu_u32 block_id,descriptor_gpu,descriptor_bytes,scratch_gpu,scratch_bytes;
 vxl_gpu_u32 owner_trace_gpu,owner_trace_elements,policy_id,reserved[16];
};
struct vxl_opt_graph_header_v2 {
 vxl_gpu_u32 magic,version,bytes,block_id,first,last,first_stage,last_stage;
 vxl_gpu_u32 node_count,policy_id,scratch_gpu,scratch_bytes,reserved[20];
};
struct vxl_opt_graph_node_v2 {
 vxl_gpu_u32 type,stage,first,last,input0,input1,output,weights,bias,gamma,beta,aux;
 vxl_gpu_u32 ci,co,ih,iw,oh,ow,k,stride,pad,groups,tasks,flags;
};
#endif
