/* Fixed little-endian hybrid ABI. Native 64-bit userspace only. */
#ifndef VX_HUNET_HYBRID_H
#define VX_HUNET_HYBRID_H
#include <uapi/vx_hunet.h>
#define VXL_HYBRID_PROGRAM 8u
#define VXL_HYBRID_FEATURE 4u
#define VXL_HYBRID_ERF_NATIVE_V1 1u
#define VXL_HYBRID_SEAL_MAGIC 0x45535948u
#define VXL_HYBRID_BEGIN_MAGIC 0x42435948u
#define VXL_HYBRID_COMMIT_MAGIC 0x43435948u
#define VXL_HYBRID_EVIDENCE_MAGIC 0x45435948u
#define VXL_HYBRID_END_MAGIC 0x4e455948u
#define VXL_HYBRID_SEAL _IOWR('V',17,struct vxl_request)
#define VXL_HYBRID_CPU_BEGIN _IOWR('V',18,struct vxl_request)
#define VXL_HYBRID_CPU_COMMIT _IOWR('V',19,struct vxl_request)
#define VXL_HYBRID_GET_CPU_EVIDENCE _IOWR('V',20,struct vxl_request)
#define VXL_HYBRID_END _IOWR('V',21,struct vxl_request)
#define VXL_HYBRID_GET_BINDING _IOWR('V',22,struct vxl_request)
#define VXL_HYBRID_CHECK_INPUT 1u
#define VXL_HYBRID_CHECK_FINITE 2u
#define VXL_HYBRID_CHECK_WRITEBACK 4u
#define VXL_HYBRID_CHECK_GUARD 8u
#define VXL_HYBRID_CHECK_IDLE 16u
#define VXL_HYBRID_CHECK_DELIVERY 32u
struct vxl_hybrid_begin_v1 {
 __u32 magic,version,bytes,flags;
 __u8 run_id[16];
 __u32 generation,sequence,stage,dtype;
 __aligned_u64 token,input_ptr,input_bytes,output_bytes;
 __u8 input_sha256[32],reserved[16];
};
struct vxl_hybrid_commit_v1 {
 __u32 magic,version,bytes,flags;
 __u8 run_id[16];
 __u32 generation,sequence,stage,algorithm;
 __aligned_u64 token,output_ptr,output_bytes,cpu_duration_ns;
 __u8 output_sha256[32];
 __u32 threads;
 __u8 reserved[12];
};
struct vxl_hybrid_binding_v2 {
 struct vxl_hunet_binding_v1 v1;
 __u8 cpu_binary_sha256[32],cpu_contract_sha256[32],cpu_stages_sha256[32],reserved[32];
};
struct vxl_hybrid_cpu_evidence_v1 {
 __u32 magic,version,bytes,flags;
 __u32 kind,generation,sequence,stage;
 __u8 run_id[16];
 __aligned_u64 token;
 __u32 algorithm,threads;
 __aligned_u64 input_bytes,output_bytes;
 __u8 input_sha256[32],output_sha256[32],catalog_sha256[32];
 __aligned_u64 read_ns,write_ns,guard_ns,total_ns,user_cpu_ns;
 __u32 gpu_jobs_before,gpu_jobs_after,cdma_status,state;
 __s32 result;
 __u32 check_flags;
 __aligned_u64 owner_cookie,deadline_ns;
 __u8 reserved[128];
};
struct vxl_hybrid_end_v1 {
 __u32 magic,version,bytes,flags;
 __u8 run_id[16];
 __u32 roi,generation,logical_jobs,gpu_jobs,cpu_jobs,stages,final_idle,check_flags;
 __aligned_u64 last_gpu_job_id;
 __u32 hw_jobs_before,hw_jobs_after;
 __u8 input_sha256[32],logits_sha256[32],binding_sha256[32],reserved[80];
};
#endif
