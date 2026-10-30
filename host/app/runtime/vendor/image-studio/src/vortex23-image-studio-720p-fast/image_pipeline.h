#ifndef IMAGE_PIPELINE_H
#define IMAGE_PIPELINE_H
#include "image_gpu.h"
typedef int (*image_execute_fn)(void*,enum image_op,const image_job*,image_gpu_stats*);
typedef int (*image_present_fn)(void*,const uint32_t*,uint32_t,uint32_t,uint64_t*,uint64_t*);
typedef struct {uint32_t input_id,epoch,style,preset,block,output_crc,cache_hit,pass_count;uint64_t gpu_cycles,generate_ticks,blit_ticks,retire_ticks;} image_result;
int image_pipeline_init(image_execute_fn run,image_present_fn present,void *ctx);
int image_pipeline_configure(uint32_t w,uint32_t h);
int image_pipeline_commit(const uint8_t *rgba,const image_rect *rect,uint32_t id,uint32_t crc);
int image_pipeline_render(const image_params *p,image_result *result);
int image_pipeline_guards_check(void);
uint32_t image_pipeline_input_id(void);
uint32_t image_crc32(const uint8_t *bytes,uint32_t n);
uint32_t image_crc_region(uint32_t address,uint32_t n);
#endif
