#ifndef MOSAIC_S8_GRAPH_H
#define MOSAIC_S8_GRAPH_H
#include <stdint.h>
#include <stddef.h>
#include "tile.h"
#include <vxl/mosaic.h>
struct mosaic_layer {
 uint32_t ci,co,h,w,k,stride,upsample,kpad;
 const int16_t *quantized;
 size_t quantized_count;
};
struct mosaic_norm_workspace {float *hi,*lo;size_t count;};
/* k0=0 is the initial full-K implementation; all Kpad tail entries are zero.
 * capacity is the number of signed16 elements, not bytes. */
int mosaic_pack_tile(const mosaic_layer *,uint32_t n0,uint32_t k0,int16_t *,size_t capacity);
// One owner-warp lane writes columns 2*lane and 2*lane+1; caller synchronizes before use.
int mosaic_pack_tile_lane(const mosaic_layer *,uint32_t n0,uint32_t k0,int16_t *,size_t capacity,uint32_t lane);
int mosaic_quantize_input(const float *,size_t,float inverse,int16_t *);
int mosaic_norm_stats(const float *,size_t,mosaic_norm_workspace *,float stats[3]);
int mosaic_norm_apply(const float *,size_t,const float stats[3],float gamma,float beta,float *);
struct mosaic_parameters {
 const int16_t *weights;size_t weight_count;
 const float *coeff;size_t coeff_count;
 float sa;const float *gamma,*beta;
};
struct mosaic_graph_memory {
 float *fp[2];size_t fp_count;
 int16_t *quantized;size_t quantized_count;
 float *skip;size_t skip_count;
 float *stats;size_t stats_count;
 int16_t *a[4],*b[4];size_t tile_capacity;
 int64_t *tile[4];mosaic_norm_workspace norm[4];
};
struct mosaic_device_context {
 uint32_t width,height,phase_index,core;
 uint32_t batch_items; /* host test scheduler: 0 selects frozen 4096 limit */
 mosaic_parameters parameters[16];mosaic_graph_memory memory;
 mosaic_s8_ops io[4];mosaic_job_v1 expected; mosaic_layout layout;
 uint64_t quantized_elements,tiles,logical_jobs,weight_packs;
 int failed;
 bool diagnostics;
 void *callback_context;
 int (*on_stage)(void *,uint32_t,const float *,size_t,const float *,size_t);
 uint32_t schedule_version=1,weight_layout_version=1;
};
int mosaic_layer_geometry(uint32_t index,uint32_t width,uint32_t height,mosaic_layer *);
int mosaic_phase_description(uint32_t phase,uint32_t width,uint32_t height,uint32_t *layer,uint32_t *op,uint32_t *items);
int mosaic_execute_job(const mosaic_job_v1 *,mosaic_device_context *);
/* Host-only aggregation of four explicit software ops contexts. It is not
 * target execution, and is never used by the Linux application adapter. */
#if !defined(__riscv)
int mosaic_run_graph(mosaic_device_context *);
#endif
#endif
