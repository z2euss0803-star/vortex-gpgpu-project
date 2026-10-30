#ifndef VXL_MOSAIC_WIRE_H
#define VXL_MOSAIC_WIRE_H
#include <vxl/mosaic_types.h>
#include <stddef.h>
#define MOSAIC_MAX_BATCH 4096u
/* LE wire fields, not a public ioctl or a host pointer container. */
struct mosaic_job_v1 {
 mosaic_u32 magic; unsigned short version,bytes;
 mosaic_u8 run_id[16],binding_tag[16];
 mosaic_u32 generation,sequence,op,layer,tile_first,tile_count,core_mask,input_epoch;
 mosaic_u8 layout_tag[16],graph_tag[16];
 mosaic_u32 crc,flags; mosaic_u8 reserved[16];
};
#if defined(__cplusplus)
#define MOS_ASSERT static_assert
#else
#define MOS_ASSERT _Static_assert
#endif
MOS_ASSERT(sizeof(struct mosaic_job_v1)==128,"private Mosaic wire size");
MOS_ASSERT(offsetof(struct mosaic_job_v1,run_id)==8,"run offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,generation)==40,"generation offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,binding_tag)==24,"binding offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,sequence)==44,"sequence offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,op)==48,"operation offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,layer)==52,"layer offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,tile_first)==56,"tile first offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,tile_count)==60,"tile count offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,core_mask)==64,"core mask offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,input_epoch)==68,"epoch offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,layout_tag)==72,"layout offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,graph_tag)==88,"graph offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,crc)==104,"CRC offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,flags)==108,"flags offset");
MOS_ASSERT(offsetof(struct mosaic_job_v1,reserved)==112,"reserved offset");
#undef MOS_ASSERT
#endif
