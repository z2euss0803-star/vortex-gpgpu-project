#ifndef VXL_MGMT_TIMING_H
#define VXL_MGMT_TIMING_H
#include <vxl/core.h>
#define VXL_MGMT_CAPACITY 512u
#define VXL_MGMT_BACKWARDS 1u
#define VXL_MGMT_OVERFLOW 2u
#define VXL_MGMT_FULL 4u
#define VXL_MGMT_BUSY 8u
#define VXL_MGMT_STALE 16u
#define VXL_MGMT_INCOMPLETE 32u
#define VXL_MGMT_FIELDS(X) \
 X(VALIDATE,validate) X(ALLOC,alloc) X(GUARD,guard) X(INPUT_READ,input_read) \
 X(FINITE,finite) X(HASH_INPUT,hash_input) X(COPY_USER,copy_user) X(SNAPSHOT,snapshot) \
 X(HASH_OUTPUT,hash_output) X(IDLE,idle) X(ORIGINAL_READ,original_read) X(ORIGINAL_HASH,original_hash) \
 X(OUTPUT_WRITE,output_write) X(READBACK,readback) X(READBACK_HASH,readback_hash) X(FINALIZE,finalize) \
 X(ASSET_READ,asset_read) X(ASSET_HASH,asset_hash) X(RESERVE,reserve) X(CODE_COPY,code_copy) \
 X(WEIGHTS_COPY,weights_copy) X(SEAL_CODE_READ,seal_code_read) X(SEAL_CODE_HASH,seal_code_hash) \
 X(SEAL_WEIGHTS_READ,seal_weights_read) X(SEAL_WEIGHTS_HASH,seal_weights_hash) \
 X(AUX_RESTORE,aux_restore) X(DESC_RESTORE,descriptor_restore) X(STATE,state)
enum vxl_mgmt_field {
#define VXL_MGMT_ENUM(upper,lower) VXL_MGMT_##upper,
 VXL_MGMT_FIELDS(VXL_MGMT_ENUM)
#undef VXL_MGMT_ENUM
 VXL_MGMT_FIELD_COUNT
};
#define VXL_MGMT_NESTED(X) X(CDMA_TOTAL,cdma_total) X(CDMA_WAIT,cdma_wait) X(CDMA_FASTPOLL,cdma_fastpoll) X(CDMA_SLEEP,cdma_sleep)
enum vxl_mgmt_nested_field {
#define VXL_MGMT_NESTED_ENUM(upper,lower) VXL_MGMT_##upper,
 VXL_MGMT_NESTED(VXL_MGMT_NESTED_ENUM)
#undef VXL_MGMT_NESTED_ENUM
 VXL_MGMT_NESTED_COUNT
};
struct vxl_mgmt_key {vxl_u64 owner_cookie;vxl_u32 generation;vxl_u64 token;vxl_u32 command;};
struct vxl_mgmt_row {
 struct vxl_mgmt_key key;
 vxl_u64 start_ns,end_ns,total_ns,last_end_ns;
 int result;vxl_u32 flags;
 vxl_u64 ns[VXL_MGMT_FIELD_COUNT],bytes[VXL_MGMT_FIELD_COUNT],calls[VXL_MGMT_FIELD_COUNT];
 /* Nested metrics are never added to exclusive ns[]. */
 vxl_u64 nested_ns[VXL_MGMT_NESTED_COUNT],nested_bytes[VXL_MGMT_NESTED_COUNT],nested_calls[VXL_MGMT_NESTED_COUNT];
};
struct vxl_mgmt_trace {struct vxl_mgmt_row *rows,*active;vxl_u32 capacity,used,flags;vxl_u64 dropped;vxl_u32 exported_used;};
typedef int (*vxl_mgmt_emit_fn)(void *,const char *,size_t);
#ifdef __cplusplus
extern "C" {
#endif
struct vxl_mgmt_row *vxl_mgmt_begin(struct vxl_mgmt_trace *,const struct vxl_mgmt_key *,vxl_u64);
int vxl_mgmt_add(struct vxl_mgmt_row *,enum vxl_mgmt_field,vxl_u64,vxl_u64,vxl_u64);
int vxl_mgmt_nested_add(struct vxl_mgmt_row *,enum vxl_mgmt_nested_field,vxl_u64,vxl_u64,vxl_u64);
void vxl_mgmt_finish(struct vxl_mgmt_trace *,vxl_u64,int);
int vxl_mgmt_emit(const struct vxl_mgmt_trace *,vxl_mgmt_emit_fn,void *);
int vxl_mgmt_new_epoch(struct vxl_mgmt_trace *);
#ifdef __cplusplus
}
#endif
#endif
