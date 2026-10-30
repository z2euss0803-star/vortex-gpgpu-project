// Pointer-bound adaptation of the frozen V5 span32/ordered4 fast paths.
// Call only after wire/shape/ref validation; never consume legacy GPU addresses.
#pragma once
#include "hunet_ops.h"
#include "vendor/hunet/v5/include/hunet_ordered_reductions.h"

static inline bool vxnn_hunet_is_span_op(uint32_t op){
 return op==HUNET_GN_APPLY||op==HUNET_SHIFT||op==HUNET_ADD||op==HUNET_MUL_CHANNEL||op==HUNET_SLICE||op==HUNET_CONCAT||op==HUNET_COPY;
}
static inline uint32_t vxnn_hunet_span_chunk(const HunetJobV1*j){return j->tasks>=2048?32u:1u;}
static inline uint32_t vxnn_hunet_scalar_grid(const HunetJobV1*j){
 const uint32_t width=vxnn_hunet_is_span_op(j->op)?4*vxnn_hunet_span_chunk(j):4;
 return (j->tasks+width-1)/width;
}
static inline uint32_t vxnn_hunet_span_end(const HunetJobV1*j,uint32_t first,uint32_t width){
 const uint32_t end=first+width;return end>j->tasks?j->tasks:end;
}
static inline void vxnn_hunet_scalar_write(float*y,uint32_t t,float value,bool&finite){
 y[t]=value;if(!isfinite(value))finite=false;
}
static inline bool vxnn_hunet_gn_apply_span(const HunetJobV1*j,const VxnnHunetScalarBuffers&b,uint32_t t,uint32_t end,uint32_t step){
 const float*x=b.x.data,*s=b.stats.data,*g=b.gamma.data,*beta=b.beta.data;float*y=b.y.data;
 const uint32_t hw=j->ih*j->iw;const float mean=s[0],inv=s[1];bool finite=true;
 while(t<end){uint32_t c=t/hw,stop=(c+1)*hw;if(stop>end)stop=end;float gamma=g[c],offset=beta[c];
  for(;t<stop;t+=step)vxnn_hunet_scalar_write(y,t,((x[t]-mean)*inv)*gamma+offset,finite);
 }
 return finite;
}
static inline bool vxnn_hunet_add_span(const HunetJobV1*,const VxnnHunetScalarBuffers&b,uint32_t t,uint32_t end,uint32_t step){
 const float*x=b.x.data,*w=b.w.data;float*y=b.y.data;bool finite=true;
 for(;t<end;t+=step)vxnn_hunet_scalar_write(y,t,x[t]+w[t],finite);return finite;
}
static inline bool vxnn_hunet_mul_span(const HunetJobV1*j,const VxnnHunetScalarBuffers&b,uint32_t t,uint32_t end,uint32_t step){
 const float*x=b.x.data,*w=b.w.data;float*y=b.y.data;const uint32_t hw=j->oh*j->ow;bool finite=true;
 while(t<end){uint32_t c=t/hw,stop=(c+1)*hw;if(stop>end)stop=end;float scale=w[c];for(;t<stop;t+=step)vxnn_hunet_scalar_write(y,t,x[t]*scale,finite);}
 return finite;
}
static inline bool vxnn_hunet_copy_span(const HunetJobV1*,const VxnnHunetScalarBuffers&b,uint32_t t,uint32_t end,uint32_t step){
 const float*x=b.x.data;float*y=b.y.data;bool finite=true;
 for(;t<end;t+=step)vxnn_hunet_scalar_write(y,t,x[t],finite);return finite;
}
static inline bool vxnn_hunet_slice_span(const HunetJobV1*j,const VxnnHunetScalarBuffers&b,uint32_t t,uint32_t end,uint32_t step){
 const float*x=b.x.data+j->groups*j->ih*j->iw;float*y=b.y.data;bool finite=true;
 for(;t<end;t+=step)vxnn_hunet_scalar_write(y,t,x[t],finite);return finite;
}
static inline bool vxnn_hunet_concat_span(const HunetJobV1*j,const VxnnHunetScalarBuffers&b,uint32_t t,uint32_t end,uint32_t step){
 const float*x=b.x.data,*w=b.w.data;float*y=b.y.data;const uint32_t cut=j->ci*j->ih*j->iw;bool finite=true;
 for(;t<end&&t<cut;t+=step)vxnn_hunet_scalar_write(y,t,x[t],finite);
 for(;t<end;t+=step)vxnn_hunet_scalar_write(y,t,w[t-cut],finite);return finite;
}
static inline bool vxnn_hunet_shift_span(const HunetJobV1*j,const VxnnHunetScalarBuffers&b,uint32_t t,uint32_t end,uint32_t step){
 const float*x=b.x.data;float*y=b.y.data;const uint32_t hw=j->ih*j->iw,g=j->ci/j->n_div;bool finite=true;
 while(t<end){uint32_t c=t/hw,channel_end=(c+1)*hw;if(channel_end>end)channel_end=end;
  int dx=0,dy=0;if(c<g)dx=1;else if(c<2*g)dx=-1;else if(c<3*g)dy=1;else if(c<4*g)dy=-1;
  while(t<channel_end){uint32_t row=(t-c*hw)/j->iw,col=t-c*hw-row*j->iw,stop=c*hw+(row+1)*j->iw;if(stop>channel_end)stop=channel_end;
   int iy=(int)row+dy;for(;t<stop;t+=step,col+=step){int ix=(int)col+dx;float value=(iy>=0&&ix>=0&&iy<(int)j->ih&&ix<(int)j->iw)?x[c*hw+(uint32_t)iy*j->iw+(uint32_t)ix]:0.0f;vxnn_hunet_scalar_write(y,t,value,finite);}
  }
 }
 return finite;
}
// These helpers reuse the exact single sum/carry chain, loading four successive
// elements per loop without a parallel merge or arithmetic reassociation.
static inline bool vxnn_hunet_gn_stats_ordered(const HunetJobV1*j,const VxnnHunetScalarBuffers&b){
 const uint32_t size=j->ci*j->ih*j->iw;
 const float mean=hunet_kahan_sum_ordered4(b.x.data,size)/(float)size;
 b.y.data[0]=mean;b.y.data[1]=1.0f/sqrtf(hunet_variance_sum_ordered4(b.x.data,size,mean)/(float)size+hunet_float(j->epsilon_bits));
 return isfinite(b.y.data[0])&&isfinite(b.y.data[1]);
}
static inline bool vxnn_hunet_gap_ordered(const HunetJobV1*j,const VxnnHunetScalarBuffers&b,uint32_t t){
 const uint32_t hw=j->ih*j->iw;b.y.data[t]=hunet_kahan_sum_ordered4(b.x.data+t*hw,hw)/(float)hw;return isfinite(b.y.data[t]);
}
