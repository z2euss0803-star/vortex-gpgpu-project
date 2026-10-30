#ifndef MOSAIC_NORM_COMPAT_V1_H
#define MOSAIC_NORM_COMPAT_V1_H
#include "graph.h"
// Independent candidate only. The default graph retains instance_norm.cpp.
int mosaic_norm_stats_compat_v1(const float *,size_t,mosaic_norm_workspace *,float[3]);
int mosaic_norm_apply_compat_v1(const float *,size_t,const float[3],float,float,float *);
int mosaic_norm_stats_compat_v2(const float *,size_t,mosaic_norm_workspace *,float[3]);
int mosaic_norm_apply_compat_v2(const float *,size_t,const float[3],float,float,float *);
int mosaic_norm_rsqrt_compat_v2(float,float *);
int mosaic_norm_stats_compat_v3(const float *,size_t,mosaic_norm_workspace *,float[3]);
// count may be a tile; channel_spatial selects the arithmetic, never tile length.
int mosaic_norm_apply_compat_v3(const float *,size_t,const float[3],float,float,size_t,float *);
#endif
