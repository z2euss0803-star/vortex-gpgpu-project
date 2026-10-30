#pragma once
#include "hunet_ops_baseline.h"

// One sum/carry chain, in the original element order. Four loads per loop
// reduce loop control; this is not a parallel or reassociated reduction.
static inline float hunet_kahan_sum_ordered4(const float*x,uint32_t size){
 float sum=0.0f,carry=0.0f;uint32_t i=0,bulk=size&~3u;
 for(;i<bulk;i+=4){
  const float a=x[i],b=x[i+1],c=x[i+2],d=x[i+3];
  hunet_kahan_add(a,&sum,&carry);
  hunet_kahan_add(b,&sum,&carry);
  hunet_kahan_add(c,&sum,&carry);
  hunet_kahan_add(d,&sum,&carry);
 }
 for(;i<size;++i)hunet_kahan_add(x[i],&sum,&carry);
 return sum;
}

static inline float hunet_variance_sum_ordered4(const float*x,uint32_t size,float mean){
 float sum=0.0f,carry=0.0f;uint32_t i=0,bulk=size&~3u;
 for(;i<bulk;i+=4){
  const float a=x[i]-mean,b=x[i+1]-mean,c=x[i+2]-mean,d=x[i+3]-mean;
  hunet_kahan_add(a*a,&sum,&carry);
  hunet_kahan_add(b*b,&sum,&carry);
  hunet_kahan_add(c*c,&sum,&carry);
  hunet_kahan_add(d*d,&sum,&carry);
 }
 for(;i<size;++i){const float v=x[i]-mean;hunet_kahan_add(v*v,&sum,&carry);}
 return sum;
}

static inline void hunet_gn_stats_ordered(const HunetJobV1*j){
 const float*x=(const float*)(uintptr_t)j->x;float*y=(float*)(uintptr_t)j->y;
 const uint32_t size=j->ci*j->ih*j->iw;
 const float sum=hunet_kahan_sum_ordered4(x,size);
 const float mean=sum/(float)size;
 const float variance_sum=hunet_variance_sum_ordered4(x,size,mean);
 y[0]=mean;y[1]=1.0f/sqrtf(variance_sum/(float)size+hunet_float(j->epsilon_bits));
}

static inline void hunet_gap_ordered(const HunetJobV1*j,uint32_t t){
 const uint32_t hw=j->ih*j->iw;
 const float*x=(const float*)(uintptr_t)j->x+t*hw;
 float*y=(float*)(uintptr_t)j->y;
 y[t]=hunet_kahan_sum_ordered4(x,hw)/(float)hw;
}
