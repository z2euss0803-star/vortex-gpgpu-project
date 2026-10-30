#include "norm_compat.h"
#if defined(MOSAIC_BASELINE_WITNESS)
#define stats_impl mosaic_norm_stats_compat_v1
#define apply_impl mosaic_norm_apply_compat_v1
#else
#define stats_impl mosaic_norm_stats_compat_v2
#define apply_impl mosaic_norm_apply_compat_v2
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <initializer_list>
static uint32_t bits(float x){uint32_t y;memcpy(&y,&x,4);return y;}
int main(){
 // Closed-form variance=1. Exact primitive evidence distinguishes split root.
 float input[]={-1.f,1.f},hi[2],lo[2],stats[3];mosaic_norm_workspace ws={hi,lo,2};
 assert(!stats_impl(input,2,&ws,stats));assert(stats[0]==0&&stats[1]==1);
 assert(bits(stats[2])==0x3f7fffabu);
#if !defined(MOSAIC_BASELINE_WITNESS)
 float x=0x1.000002p0f,out=0,apply_stats[3]={0,1,0x1.fffffcp-1f};
 assert(!apply_impl(&x,1,apply_stats,1,-1,&out));assert(bits(out)==bits(-0x1p-46f));
 // Raw primitive map gives 0x3f7ffffe here; mathematical RN is 0x3f7fffff.
 float y;assert(!mosaic_norm_rsqrt_compat_v2(0x1.000002p0f,&y));assert(bits(y)==0x3f7ffffeu);
 assert(!mosaic_norm_rsqrt_compat_v2(0x1.fffffep1f,&y));assert(isfinite(y)&&y>0);
 assert(!mosaic_norm_rsqrt_compat_v2(0x1p-126f,&y));assert(isfinite(y)&&y>0);
 assert(!mosaic_norm_rsqrt_compat_v2(0x1.fffffep127f,&y));assert(isfinite(y)&&y>0);
 for(float v:{0.f,-1.f,NAN,INFINITY,0x1p-149f})assert(mosaic_norm_rsqrt_compat_v2(v,&y)<0);
#endif
 puts("IN_COMPAT_V2_PRIMITIVE_TEST_PASS_NOT_NETWORK_NOT_GPU raw_gpu_cycles=null");
}
