#pragma once
#include "hunet_ops_baseline.h"
#ifndef HUNET_SCALAR_CHUNK
#define HUNET_SCALAR_CHUNK 32u
#endif
#ifndef HUNET_SCALAR_WRITE
#define HUNET_SCALAR_WRITE(i,v) (y[(i)]=(v))
#endif
static inline uint32_t hunet_span_chunk(const HunetJobV1*j){return j->tasks>=2048?HUNET_SCALAR_CHUNK:1u;}
static inline uint32_t hunet_span_end(const HunetJobV1*j,uint32_t first,uint32_t width){uint32_t end=first+width;return end>j->tasks?j->tasks:end;}
static inline int hunet_is_span_op(uint32_t op){return op==HUNET_GN_APPLY || op==HUNET_SHIFT || op==HUNET_ADD || op==HUNET_MUL_CHANNEL || op==HUNET_SLICE || op==HUNET_CONCAT || op==HUNET_COPY;}
// Channel loads are held only while the lane stays inside that channel.
// FP32 subtraction, multiplication, multiplication, addition keep their order.
static inline void hunet_gn_apply_span(const HunetJobV1*j,uint32_t t,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x,*s=(const float*)(uintptr_t)j->scratch;
 const float*g=(const float*)(uintptr_t)j->gamma,*b=(const float*)(uintptr_t)j->beta;
 float*y=(float*)(uintptr_t)j->y;uint32_t hw=j->ih*j->iw;
 const float mean=s[0],inv=s[1];
 while(t<end){uint32_t c=t/hw,stop=(c+1)*hw;if(stop>end)stop=end;float gamma=g[c],beta=b[c];
  for(;t<stop;t+=step)HUNET_SCALAR_WRITE(t,((x[t]-mean)*inv)*gamma+beta);
 }
}
static inline void hunet_add_span(const HunetJobV1*j,uint32_t t,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x,*w=(const float*)(uintptr_t)j->w;float*y=(float*)(uintptr_t)j->y;
 for(;t<end;t+=step)HUNET_SCALAR_WRITE(t,x[t]+w[t]);
}
static inline void hunet_mul_span(const HunetJobV1*j,uint32_t t,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x,*w=(const float*)(uintptr_t)j->w;float*y=(float*)(uintptr_t)j->y;uint32_t hw=j->oh*j->ow;
 while(t<end){uint32_t c=t/hw,stop=(c+1)*hw;if(stop>end)stop=end;float scale=w[c];for(;t<stop;t+=step)HUNET_SCALAR_WRITE(t,x[t]*scale);}
}
static inline void hunet_copy_span(const HunetJobV1*j,uint32_t t,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x;float*y=(float*)(uintptr_t)j->y;
 for(;t<end;t+=step)HUNET_SCALAR_WRITE(t,x[t]);
}
static inline void hunet_slice_span(const HunetJobV1*j,uint32_t t,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x+j->groups*j->ih*j->iw;float*y=(float*)(uintptr_t)j->y;
 for(;t<end;t+=step)HUNET_SCALAR_WRITE(t,x[t]);
}
static inline void hunet_concat_span(const HunetJobV1*j,uint32_t t,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x,*w=(const float*)(uintptr_t)j->w;float*y=(float*)(uintptr_t)j->y;uint32_t cut=j->ci*j->ih*j->iw;
 for(;t<end && t<cut;t+=step)HUNET_SCALAR_WRITE(t,x[t]);
 for(;t<end;t+=step)HUNET_SCALAR_WRITE(t,w[t-cut]);
}
static inline void hunet_shift_span(const HunetJobV1*j,uint32_t t,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x;float*y=(float*)(uintptr_t)j->y;uint32_t hw=j->ih*j->iw,g=j->ci/j->n_div;
 while(t<end){uint32_t c=t/hw,channel_end=(c+1)*hw;if(channel_end>end)channel_end=end;
  int dx=0,dy=0;if(c<g)dx=1;else if(c<2*g)dx=-1;else if(c<3*g)dy=1;else if(c<4*g)dy=-1;
  while(t<channel_end){uint32_t row=(t-c*hw)/j->iw,col=t-c*hw-row*j->iw,stop=c*hw+(row+1)*j->iw;if(stop>channel_end)stop=channel_end;
   int iy=(int)row+dy;for(;t<stop;t+=step,col+=step){int ix=(int)col+dx;float v=(iy>=0 && ix>=0 && iy<(int)j->ih && ix<(int)j->iw)?x[c*hw+(uint32_t)iy*j->iw+(uint32_t)ix]:0.0f;HUNET_SCALAR_WRITE(t,v);}
  }
 }
}
