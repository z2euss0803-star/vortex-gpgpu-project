/* Fixed little-endian HW4 HUNet ABI. No pointers inside GPU job descriptors. */
#ifndef VX_HUNET_H
#define VX_HUNET_H
#include <linux/types.h>
#define VXL_HUNET_PROFILE 4u
#define VXL_HUNET_PROGRAM 6u
#define VXL_HUNET_WMMA_PROGRAM 7u
#define VXL_WEIGHTS 4u
#define VXL_WORKSPACE 5u
#define VXL_HUNET_SEAL_MAGIC 0x45534c48u
#define VXL_HUNET_BEGIN_MAGIC 0x47424c48u
#define VXL_HUNET_END_MAGIC 0x4e454c48u
#define VXL_HUNET_BINDING_MAGIC 0x44424c48u
#define VXL_HUNET_EVIDENCE_MAGIC 0x454a4c48u
#define VXL_HUNET_FIRST 1u
#define VXL_HUNET_WARMUP 2u
#define VXL_HUNET_MEASURED 3u
#define VXL_HUNET_DIAGNOSTIC 4u
#define VXL_HUNET_FIXED_GRAPH 1u
#define VXL_HUNET_WMMA_FEATURE 2u
#define VXL_HUNET_CHECK_GUARDS 1u
#define VXL_HUNET_CHECK_FINITE 2u
#define VXL_HUNET_CHECK_INPUT 4u
#define VXL_HUNET_CHECK_EXIT 8u
#define VXL_HUNET_CHECK_OWNER 16u
#define VXL_HUNET_VALID_TIMING 1u
#define VXL_HUNET_VALID_CRT 2u
#define VXL_HUNET_VALID_OWNER 4u
#define VXL_HUNET_VALID_HW_SNAPSHOT 8u
#define VXL_HUNET_VALID_EXIT_SNAPSHOT 16u
struct vxl_hunet_job_v1 {
 __u32 magic,abi,bytes,op,sequence,flags,dtype,tasks;
 __u32 x,w,bias,gamma,beta,y,scratch,aux;
 __u32 ci,co,ih,iw,oh,ow,k,nitems;
 __u32 stride,pad,groups,n_div,epsilon_bits,activation,stage_id,reserved;
};
struct vxl_hunet_matrix_v1 { __u32 m,n,k,mp,np,kp,offset,count; };
struct vxl_hunet_seal_v1 {
 __u32 magic,version,bytes,flags;
 __aligned_u64 code,weights,workspace,args;
 __u8 binding_sha256[32],reserved[48];
};
struct vxl_hunet_begin_v1 {
 __u32 magic,version,bytes,flags;
 __u8 run_id[16];
 __u32 roi_index,sample_kind,sample_index,reserved0;
 __aligned_u64 input_ptr,input_bytes;
 __u8 input_sha256[32],reserved[32];
};
struct vxl_hunet_end_v1 {
 __u32 magic,version,bytes,flags;
 __u8 run_id[16];
 __u32 roi_index,completed_jobs,generation,check_flags;
 __aligned_u64 last_job_id;
 __u32 last_hw_jobs,final_idle;
 __u8 logits_sha256[32],input_sha256[32];
};
struct vxl_hunet_binding_v1 {
 __u32 magic,version,bytes,profile_id,program_id,features,generation,state;
 __u8 profile_sha256[32],program_sha256[32],source_sha256[32],weights_sha256[32],jobs_sha256[32],aux_sha256[32],model_sha256[32];
};
struct vxl_hunet_job_evidence_v1 {
 __u32 magic,version,bytes,flags;
 __u8 run_id[16];
 __aligned_u64 job_id,submit_ns,done_ns,ack_ns;
 __u32 sequence,op,stage_id,program_id;
 __u32 status,errors,pending_w,pending_r;
 __u32 jobs_before,jobs_after,watchdog,elapsed_raw;
 __u32 ready,idle,cdma_status,valid_flags;
 __u32 exitcode[4];
 __u32 software_state;
 __s32 result;
 __u32 generation,reserved0;
 __u8 args_sha256[32];
 __aligned_u64 execution_deadline_ns,wait_ms;
 __u32 crt_guard_flags,owner_counts[4];
 __u8 reserved[28];
};
#endif
