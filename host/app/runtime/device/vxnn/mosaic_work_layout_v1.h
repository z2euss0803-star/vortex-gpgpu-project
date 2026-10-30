#ifndef VXNN_MOSAIC_WORK_LAYOUT_V1_H
#define VXNN_MOSAIC_WORK_LAYOUT_V1_H
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
/* Private software ABI only. All offsets are bytes from one owned core slice. */
enum {
 MOSAIC_WORK_LAYOUT_V1=1,
 MOSAIC_SCHEDULE_CHANNEL_MAJOR_GLOBAL_MOD4_V1=1,
 MOSAIC_WEIGHT_CO_KPAD_I16_V1=1,
 MOSAIC_WORK_PREFIX_BYTES=64,
 MOSAIC_PROFILE_BYTES_V1=256,
 MOSAIC_RAW_BYTES_V1=384,
 MOSAIC_PROFILE_STAGE_COUNT_V1=6,
 MOSAIC_PROFILE_MAGIC_V1=0x3350434d,
 MOSAIC_PROFILE_VERSION_V1=2,
 MOSAIC_OPTION_CYCLES_V1=1,
 MOSAIC_TIMER_NONE_V1=0,
 /* Per-core RV32 rdcycleh/rdcycle pair. NOT controller32 raw GPU cycles. */
 MOSAIC_TIMER_RV32_CSR64_V1=1,
 MOSAIC_PROFILE_EMPTY_V1=0,
 MOSAIC_PROFILE_STARTED_V1=1,
 MOSAIC_PROFILE_COMPLETE_V1=2,
 MOSAIC_PROFILE_FAILED_V1=3
};
enum MosaicProfileStageV1 {
 MOSAIC_STAGE_INPUT_PACK_V1=0, MOSAIC_STAGE_WEIGHT_PREPARE_V1=1,
 MOSAIC_STAGE_BEGIN_STEP_V1=2, MOSAIC_STAGE_READ_DECODE_V1=3,
 MOSAIC_STAGE_OUTPUT_RESTORE_V1=4, MOSAIC_STAGE_POINT_STATS_V1=5
};
#define MOSAIC_WORK_ABSENT_V1 UINT32_MAX
/* Fixed little-endian wire representation; read only after matching retirement. */
typedef struct MosaicProfileHeaderV1 {
 uint32_t magic,version,bytes,layout_version;
 uint32_t opcode,core,first,count;
 uint32_t options,timer_kind,schedule_version,weight_layout_version;
 uint32_t status,valid_stages;
 int32_t error_code;
 uint32_t argument_tag;
} MosaicProfileHeaderV1;
typedef struct MosaicProfileCounterV1 {
 uint64_t cycles,units,logical_bytes,calls;
} MosaicProfileCounterV1;
typedef struct MosaicProfileRecordV1 {
 MosaicProfileHeaderV1 header;
 MosaicProfileCounterV1 stage[MOSAIC_PROFILE_STAGE_COUNT_V1];
} MosaicProfileRecordV1;
typedef struct MosaicWorkLayoutV1 {
 uint32_t layout_version,schedule_version,weight_layout_version;
 uint32_t core_stride,compute_bytes,total_work_bytes,norm_elements;
 uint32_t a_offset,a_bytes,b_offset,b_bytes,acc_offset,acc_bytes;
 uint32_t raw_offset,raw_bytes,profile_offset,profile_bytes;
} MosaicWorkLayoutV1;
#ifdef __cplusplus
#define MOSAIC_LAYOUT_ASSERT(c,m) static_assert(c,m)
#else
#define MOSAIC_LAYOUT_ASSERT(c,m) _Static_assert(c,m)
#endif
MOSAIC_LAYOUT_ASSERT(sizeof(MosaicProfileHeaderV1)==64,"profile header64");
MOSAIC_LAYOUT_ASSERT(sizeof(MosaicProfileCounterV1)==32,"profile counter32");
MOSAIC_LAYOUT_ASSERT(offsetof(MosaicProfileRecordV1,stage)==64,"profile stages at64");
MOSAIC_LAYOUT_ASSERT(sizeof(MosaicProfileRecordV1)==256,"profile record256");
MOSAIC_LAYOUT_ASSERT(offsetof(MosaicProfileHeaderV1,error_code)==56,"signed error offset56");
#undef MOSAIC_LAYOUT_ASSERT
/* Geometry/role validation belongs to the wire validator. No pointer or FP work.
 * No schedule changes: tile=channel_block*nblocks+spatial_block, core=tile%4.
 * Output is untouched on error. tree is zero except for the norm-stat opcode3. */
static inline int mosaic_work_layout_v1(uint32_t opcode,uint32_t kpad,uint32_t tree,MosaicWorkLayoutV1 *out){
#ifdef __cplusplus
 MosaicWorkLayoutV1 l{};
#else
 MosaicWorkLayoutV1 l={0};
#endif
 uint64_t compute=0,raw=0,stride,total;
 if(!out||opcode<1||opcode>8)return -EINVAL;
 /* The six point opcodes share a constant layout. Keep this complete return
  * separate from the variable convolution/statistics layout below. Besides
  * avoiding unnecessary wide arithmetic, this prevents the frozen Vortex
  * target pass from merging zero compute/raw PHIs into a rejecting branch.
  * Error leaves the caller's output untouched, exactly as before. */
 if(opcode!=2&&opcode!=3){
  if(tree)return -EINVAL;
  l.layout_version=MOSAIC_WORK_LAYOUT_V1;
  l.schedule_version=MOSAIC_SCHEDULE_CHANNEL_MAJOR_GLOBAL_MOD4_V1;
  l.weight_layout_version=MOSAIC_WEIGHT_CO_KPAD_I16_V1;
  l.core_stride=MOSAIC_PROFILE_BYTES_V1;
  l.total_work_bytes=MOSAIC_WORK_PREFIX_BYTES+4*MOSAIC_PROFILE_BYTES_V1;
  l.a_offset=l.b_offset=l.acc_offset=l.raw_offset=MOSAIC_WORK_ABSENT_V1;
  l.profile_bytes=MOSAIC_PROFILE_BYTES_V1;
  *out=l;return 0;
 }
 if(opcode==2){
  if(kpad<32||kpad>2592||kpad%32||tree)return -EINVAL;
  compute=((uint64_t)kpad*32+512+63)/64*64;raw=MOSAIC_RAW_BYTES_V1;
 }else if(opcode==3){
  if(!tree||tree>2097152||(tree&(tree-1)))return -EINVAL;
  compute=((uint64_t)tree*8+63)/64*64;
 }else if(tree)return -EINVAL;
 stride=(compute+raw+MOSAIC_PROFILE_BYTES_V1+63)/64*64;
 total=MOSAIC_WORK_PREFIX_BYTES+4*stride;
 if(compute>UINT32_MAX||stride>UINT32_MAX||total>UINT32_MAX)return -EOVERFLOW;
 l.layout_version=MOSAIC_WORK_LAYOUT_V1;
 l.schedule_version=MOSAIC_SCHEDULE_CHANNEL_MAJOR_GLOBAL_MOD4_V1;
 l.weight_layout_version=MOSAIC_WEIGHT_CO_KPAD_I16_V1;
 l.compute_bytes=(uint32_t)compute;l.core_stride=(uint32_t)stride;l.total_work_bytes=(uint32_t)total;
 l.norm_elements=opcode==3?tree:0;
 l.a_offset=l.b_offset=l.acc_offset=l.raw_offset=MOSAIC_WORK_ABSENT_V1;
 if(opcode==2){
  l.a_offset=0;l.a_bytes=kpad*16;l.b_offset=kpad*16;l.b_bytes=kpad*16;
  l.acc_offset=kpad*32;l.acc_bytes=512;l.raw_offset=(uint32_t)compute;l.raw_bytes=(uint32_t)raw;
 }
 l.profile_offset=(uint32_t)(compute+raw);l.profile_bytes=MOSAIC_PROFILE_BYTES_V1;
 *out=l;return 0;
}
#endif

