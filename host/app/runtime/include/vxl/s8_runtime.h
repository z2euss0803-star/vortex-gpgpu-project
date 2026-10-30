#ifndef VXL_S8_RUNTIME_WIRE_H
#define VXL_S8_RUNTIME_WIRE_H
#include <vxl/s8_wire.h>
#define VXL_S8_CORE_MAGIC 0x31433853u
#define VXL_S8_GLOBAL_MAGIC 0x31473853u
#define VXL_S8_STACK_TAG 0x53540000u
#define VXL_S8_TLS_TAG 0x544c0000u
enum s8_result_valid {S8_RAW_VALID=1,S8_DECODED_VALID=2,S8_RECOVERY_RAW_VALID=4,S8_RECOVERY_DECODED_VALID=8,S8_STATUS_VALID=16,S8_PHASE_VALID=32};
enum s8_record_op {S8_OP_BEGIN=1,S8_OP_STATUS=2,S8_OP_DATA=3,S8_OP_RELEASE=4,S8_OP_STEP=5,S8_OP_MASK=6};
struct vxl_s8_core_done {s8_u32 magic,version,core,generation,sequence,error,harts,done;};
struct vxl_s8_global_done {s8_u32 magic,version,bytes,generation,sequence,cores,harts,done;s8_u8 run_id[16],binding_tag[16];};
struct vxl_s8_sync {s8_u32 phase_seq,owner_ready,contender_done,abort,reserved[12];};
S8_ASSERT(sizeof(struct vxl_s8_core_done)==32);
S8_ASSERT(sizeof(struct vxl_s8_global_done)==64);
S8_ASSERT(sizeof(struct vxl_s8_sync)==64);
#endif
