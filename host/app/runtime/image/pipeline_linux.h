#ifndef VX_IMAGE_PIPELINE_LINUX_H
#define VX_IMAGE_PIPELINE_LINUX_H
#include <stdint.h>
#ifdef __cplusplus
#define _Static_assert static_assert
#endif
#include "vendor/image-studio/src/vortex23-image-studio-720p-fast/image_contract.h"
#ifdef __cplusplus
#undef _Static_assert
extern "C" {
#endif
/* Legacy address constants below are LOGICAL TOKENS, never CPU/DMA/GPU VAs.
 * Only checked slot translation may resolve them to owned BufferRefs. */
#define VX_IMAGE_RESERVED_BYTES UINT32_C(138412032)
#define VX_IMAGE_MAX_PASSES 16u
typedef struct {uint32_t valid,epoch,crc;image_params params;} vx_image_cache;
typedef struct {
 uint32_t serial,epoch,input_id,width,height,cache_hit,output_token,output_crc;
 uint32_t count,ops[VX_IMAGE_MAX_PASSES];image_job jobs[VX_IMAGE_MAX_PASSES];
 image_params params;uint32_t next_smooth,next_neon,next_mean;
} vx_image_plan;
typedef struct {
 uint32_t width,height,epoch,input_id,input_crc,serial,busy,accepted;
 int first_error;image_rect rect;vx_image_cache results[22];
 uint32_t smooth_valid,neon_valid,mean_block;vx_image_plan pending;
} vx_image_state;
typedef struct {uint32_t slot,offset,length,access;} vx_image_view;
int vx_image_init(vx_image_state*,uint32_t width,uint32_t height);
int vx_image_commit(vx_image_state*,const image_rect*,uint32_t id,uint32_t crc);
int vx_image_prepare(vx_image_state*,const image_params*,vx_image_plan*);
int vx_image_accept(vx_image_state*,const vx_image_plan*,uint32_t index,int error);
int vx_image_finish(vx_image_state*,const vx_image_plan*,uint32_t output_crc);
/* After all ACK/guards and restored display, retain successful intermediates. */
int vx_image_present_failed(vx_image_state*,const vx_image_plan*);
int vx_image_cancel_current(vx_image_state*,const vx_image_plan*);
int vx_image_job_validate(uint32_t op,const image_job*);
int vx_image_slot(uint32_t token,uint32_t length,uint32_t writing,uint32_t*slot,uint32_t*offset);
/* Canonical64B private args: all addresses zero before C relocation. param2/3
 * are checked table suboffsets ignored by the frozen RV32 instructions. */
int vx_image_lower(uint32_t op,const image_job*,image_job*wire,vx_image_view views[5]);
#ifdef __cplusplus
}
#endif
#endif
