#ifndef VXL_V2_EXECUTION_H
#define VXL_V2_EXECUTION_H
#include <vxl/v2/provider.h>
/* Trusted boot policy, NOT UAPI. Same catalog SHA and root-owned file admission.
 * Retirement words describe the frozen program's actual writeback protocol.
 * No ioctl can supply a watchdog, GPU address, TEX state or indirect schema. */
#define VXL_V2_EXEC_MAGIC 0x454c5856u
/* Private execution header version2 consumes policy.reserved[0] as this mode.
 * Version1 and mode0 preserve the original four explicit exit-word contract.
 * GLOBAL_DRAIN is for frozen programs without a CRT: original wrapper DONE=5,
 * inactive, error-free, memory/TEX/CDMA drained, exact job increment, preserved
 * code/args/source/guard and stable pre-ACK snapshot. It supplies no exit words.
 * completion.core_mask is a normalized retirement proof, not raw core MMIO. */
#define VXL_V2_RETIRE_EXIT_WORDS 0u
#define VXL_V2_RETIRE_GLOBAL_DRAIN 1u
struct vxl_v2_execution_header {vxl_u32 magic,version,bytes,count;vxl_u8 catalog_sha[32];vxl_u32 reserved[4];};
struct vxl_v2_private_guard {vxl_u32 gpu,offset,bytes,stride,count;};
struct vxl_v2_execution_policy {
 vxl_u8 entry_id[32];
 vxl_u32 watchdog,retire_ref,retire_offset,retire_stride,retire_value,retire_count;
 struct vxl_v2_tex tex;
 vxl_u32 directory_ref,indices_ref,faces_arg_offset,tile_count,max_faces,max_indices;
 vxl_u32 guard_count;struct vxl_v2_private_guard guards[4];vxl_u32 reserved[17];
};
struct vxl_v2_execution {
 struct vxl_v2_hw *hw;struct vxl_v2_execution_policy policies[16];vxl_u32 count;
 const struct vxl_v2_program *program;const struct vxl_v2_job *job;
 struct vxl_v2_execution_policy policy;struct vxl_v2_ref args,retire;
 vxl_u8 shadow[4096],input_sha[8][32];vxl_u32 args_bytes,prepared,started,verified;
 struct vxl_v2_snapshot last;vxl_u32 jobs_before;
 vxl_u64 dma_begin,bytes_begin;
 vxl_u32 retire_words[4],cdma_status;
};
#ifdef __cplusplus
extern "C" {
#endif
int vxl_v2_execution_bind(struct vxl_v2_execution *,struct vxl_v2_hw *,const struct vxl_v2_catalog *,const struct vxl_v2_execution_policy *,vxl_u32);
int vxl_v2_execution_snapshot(struct vxl_v2_execution *,vxl_u64,struct vxl_v2_snapshot *);
extern const struct vxl_v2_execution_ops vxl_v2_native_execution_ops;
#ifdef __cplusplus
}
#endif
#endif
