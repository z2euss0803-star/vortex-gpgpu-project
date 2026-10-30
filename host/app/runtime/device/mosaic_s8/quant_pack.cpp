#include "graph.h"
#include <errno.h>
#include <math.h>
#if !defined(__riscv)
#include <fenv.h>
#endif
static bool rne(){
#if defined(__riscv)
 uint32_t frm;__asm__ volatile("csrr %0, frm":"=r"(frm));return frm==0;
#else
 return fegetround()==FE_TONEAREST;
#endif
}
int mosaic_quantize_input(const float *x,size_t n,float inverse,int16_t *out){
 if(!x||!out||!n||n>128u*960u*1280u||!isfinite(inverse)||inverse<=0||!rne())return -EINVAL;
 for(size_t i=0;i<n;++i)if(!isfinite(x[i]))return -EDOM;
 for(size_t i=0;i<n;++i){
  volatile float scaled=x[i]*inverse;
  if(scaled>=32767.f){out[i]=32767;continue;}
  if(scaled<=-32767.f){out[i]=-32767;continue;}
  int32_t value=int32_t(scaled);volatile float fraction=scaled-float(value);
  if(fraction>.5f||(fraction==.5f&&(value&1)))++value;
  if(fraction<-.5f||(fraction==-.5f&&(value&1)))--value;
  out[i]=int16_t(value);
 }
 return 0;
}
/* The accepted geometry gives -r <= x <= n-1+r and r <= n-1.
 * One reflection is therefore identical to periodic reflect101, including
 * x=2*(n-1) at the minimum extent. Coordinates are in [-4,2563]. */
static uint32_t reflect_bounded(int32_t x,uint32_t n){
 if(x<0)return uint32_t(-x);
 uint32_t u=uint32_t(x);return u<n?u:2*(n-1)-u;
}
int mosaic_pack_tile(const mosaic_layer *l,uint32_t n0,uint32_t k0,int16_t *out,size_t capacity){
 if(!l||!out||!l->quantized||!l->ci||l->ci>128||!l->co||l->co>128||!l->h||l->h>960||!l->w||l->w>1280||(l->k!=3&&l->k!=9)||(l->stride!=1&&l->stride!=2)||(l->upsample!=1&&l->upsample!=2)||k0)return -EINVAL;
 uint32_t inner=l->ci*l->k*l->k,virtual_h=l->h*l->upsample,virtual_w=l->w*l->upsample;
 if(inner>4096||l->kpad!=(inner+31)/32*32||virtual_h<=l->k/2||virtual_w<=l->k/2)return -EINVAL;
 uint32_t oh=(virtual_h+l->stride-1)/l->stride,ow=(virtual_w+l->stride-1)/l->stride;
 if(oh>960||ow>1280||n0>=oh*ow||n0%8||capacity<size_t(l->kpad)*8||l->quantized_count!=size_t(l->ci)*l->h*l->w)return -ERANGE;
 uint32_t valid=oh*ow-n0;if(valid>8)valid=8;
 /* These 576 bytes cache geometry shared by every input channel. The maximum
  * source offset is 128*960*1280-1, so all index arithmetic also fits uint32. */
 uint32_t row[9][8],column[9][8];
 for(uint32_t n=0;n<valid;++n){
  uint32_t pos=n0+n;
  int32_t y=int32_t(pos/ow*l->stride)-int32_t(l->k/2);
  int32_t x=int32_t(pos%ow*l->stride)-int32_t(l->k/2);
  for(uint32_t d=0;d<l->k;++d){
   row[d][n]=reflect_bounded(y+int32_t(d),virtual_h)/l->upsample*l->w;
   column[d][n]=reflect_bounded(x+int32_t(d),virtual_w)/l->upsample;
  }
 }
 uint32_t kk=0;
 for(uint32_t c=0;c<l->ci;++c){
  uint32_t channel=c*l->h*l->w;
  for(uint32_t ky=0;ky<l->k;++ky)for(uint32_t kx=0;kx<l->k;++kx,++kk){
   for(uint32_t n=0;n<valid;++n)out[size_t(kk)*8+n]=l->quantized[channel+row[ky][n]+column[kx][n]];
   for(uint32_t n=valid;n<8;++n)out[size_t(kk)*8+n]=0;
  }
 }
 for(size_t i=size_t(inner)*8;i<size_t(l->kpad)*8;++i)out[i]=0;
 return 0;
}

int mosaic_pack_tile_lane(const mosaic_layer *l,uint32_t n0,uint32_t k0,int16_t *out,size_t capacity,uint32_t lane){
 // Admission must finish before any store, including zero-filled tail lanes.
 if(lane>3||!l||!out||!l->quantized||!l->ci||l->ci>128||!l->co||l->co>128||!l->h||l->h>960||!l->w||l->w>1280||(l->k!=3&&l->k!=9)||(l->stride!=1&&l->stride!=2)||(l->upsample!=1&&l->upsample!=2)||k0)return -EINVAL;
 uint32_t inner=l->ci*l->k*l->k,virtual_h=l->h*l->upsample,virtual_w=l->w*l->upsample;
 if(inner>4096||l->kpad!=(inner+31)/32*32||virtual_h<=l->k/2||virtual_w<=l->k/2)return -EINVAL;
 uint32_t oh=(virtual_h+l->stride-1)/l->stride,ow=(virtual_w+l->stride-1)/l->stride;
 if(oh>960||ow>1280||n0>=oh*ow||n0%8||capacity<size_t(l->kpad)*8||l->quantized_count!=size_t(l->ci)*l->h*l->w)return -ERANGE;
 uint32_t valid=oh*ow-n0;if(valid>8)valid=8;
 const uint32_t first=lane*2;
 uint32_t row[9][2],column[9][2];
 for(uint32_t local=0;local<2;++local){
  uint32_t n=first+local;if(n>=valid)continue;
  uint32_t pos=n0+n;
  int32_t y=int32_t(pos/ow*l->stride)-int32_t(l->k/2);
  int32_t x=int32_t(pos%ow*l->stride)-int32_t(l->k/2);
  for(uint32_t d=0;d<l->k;++d){
   row[d][local]=reflect_bounded(y+int32_t(d),virtual_h)/l->upsample*l->w;
   column[d][local]=reflect_bounded(x+int32_t(d),virtual_w)/l->upsample;
  }
 }
 uint32_t kk=0;
 for(uint32_t c=0;c<l->ci;++c){
  uint32_t channel=c*l->h*l->w;
  for(uint32_t ky=0;ky<l->k;++ky)for(uint32_t kx=0;kx<l->k;++kx,++kk){
   for(uint32_t local=0;local<2;++local){
    uint32_t n=first+local;
    out[size_t(kk)*8+n]=n<valid?l->quantized[channel+row[ky][local]+column[kx][local]]:0;
   }
  }
 }
 for(uint32_t tail=inner;tail<l->kpad;++tail)
  for(uint32_t local=0;local<2;++local)out[size_t(tail)*8+first+local]=0;
 // Each invocation owns distinct int16 objects. Caller agrees errors and
 // synchronizes all four owner-warp lanes before any consumer reads the tile.
 return 0;
}
