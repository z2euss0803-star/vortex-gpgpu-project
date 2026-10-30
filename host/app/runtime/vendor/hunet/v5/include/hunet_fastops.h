#pragma once
#include "hunet_ops_baseline.h"
#ifdef __cplusplus
extern "C" {
#endif
float hunet_erff(float);
#ifdef __cplusplus
}
#endif
static inline float hunet_gelu(float v){return (v*.5f)*(1.0f+erff(v/1.4142135623730951f));}
/* One warp owns one packed K row. Channel/kernel division is once per row. */
static inline void hunet_pack_row(const HunetJobV1*j,uint32_t kk,uint32_t lane,uint32_t step){
 const HunetMatrix*m=(const HunetMatrix*)(uintptr_t)j->aux;
 const float*x=(const float*)(uintptr_t)j->x;uint16_t*y=(uint16_t*)(uintptr_t)j->y;
 uint32_t hw=j->ih*j->iw,ic=0,ky=0,kx=0;
 if(j->flags!=2){ic=kk/(j->k*j->k);ky=(kk/j->k)%j->k;kx=kk%j->k;}
 const int direct=j->flags!=2 && j->k==1 && j->stride==1 && j->pad==0 && j->ih==j->oh && j->iw==j->ow;
 for(uint32_t nn=lane;nn<m->NP;nn+=step){
  float value=0;
  if(kk<m->K && nn<m->count){uint32_t pos=m->offset+nn;
   if(j->flags==2){int ix=(int)pos+(int)kk-(int)j->pad;if(ix>=0 && ix<(int)j->iw)value=x[ix];}
   else if(direct)value=x[kk*hw+pos];
   else{int iy=(int)((pos/j->ow)*j->stride+ky)-(int)j->pad;int ix=(int)((pos%j->ow)*j->stride+kx)-(int)j->pad;
    if(iy>=0 && ix>=0 && iy<(int)j->ih && ix<(int)j->iw)value=x[ic*hw+(uint32_t)iy*j->iw+(uint32_t)ix];}
  }
  y[kk*m->NP+nn]=hunet_half(value);
 }
}
static inline void hunet_unpack_row(const HunetJobV1*j,uint32_t row,uint32_t lane,uint32_t step){
 const HunetMatrix*m=(const HunetMatrix*)(uintptr_t)j->aux;
 const float*x=(const float*)(uintptr_t)j->x+row*m->NP;
 float*y=(float*)(uintptr_t)j->y+row*m->N+m->offset;
 if(j->bias){float bias=((const float*)(uintptr_t)j->bias)[row];for(uint32_t c=lane;c<m->count;c+=step)y[c]=x[c]+bias;}
 else for(uint32_t c=lane;c<m->count;c+=step)y[c]=x[c];
}
static inline void hunet_gelu_span(const HunetJobV1*j,uint32_t first,uint32_t end,uint32_t step){
 const float*x=(const float*)(uintptr_t)j->x;float*y=(float*)(uintptr_t)j->y;
 for(uint32_t t=first;t<end;t+=step)y[t]=hunet_gelu(x[t]);
}
static inline void hunet_run_scalar(const HunetJobV1*j){
 const HunetMatrix*m=(const HunetMatrix*)(uintptr_t)j->aux;
 if(j->op==HUNET_PACK){for(uint32_t r=0;r<m->KP;++r)hunet_pack_row(j,r,0,1);}
 else if(j->op==HUNET_UNPACK){for(uint32_t r=0;r<m->M;++r)hunet_unpack_row(j,r,0,1);}
 else if(j->op==HUNET_GELU)hunet_gelu_span(j,0,j->tasks,1);
 else for(uint32_t t=0;t<j->tasks;++t)hunet_scalar_baseline(j,t);
}
