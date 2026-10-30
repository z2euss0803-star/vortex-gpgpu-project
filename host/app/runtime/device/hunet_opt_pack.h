#pragma once
#include "hunet_fused_ops.h"
#include "hunet_opt_views.h"
static inline uint64_t opt_descriptor_span(const HunetFusedDescriptorV1&d,bool pack){return pack?d.kp:uint64_t(d.mp/8)*(d.np/4);}
static inline uint32_t opt_descriptor_base(const HunetFusedDescriptorV1&d,bool pack){return pack?d.pack_grid_base:d.gemm_grid_base;}
/* Once per bound stage, outside per-grid callbacks. */
static inline bool opt_descriptor_table_valid(const HunetFusedDescriptorV1 *ds,uint32_t count){
 if(!ds||!count||count>32)return false;
 uint64_t pack=0,gemm=0;
 for(uint32_t i=0;i<count;i++){
  const auto &d=ds[i];if(!d.m||!d.n||!d.k||d.mp<d.m||d.mp%8||d.np%4||!d.count||d.np<d.count||d.kp<d.k||d.kp%8||uint64_t(d.offset)+d.count>d.n)return false;
  if(d.pack_grid_base!=pack||d.gemm_grid_base!=gemm)return false;
  pack+=d.kp;gemm+=opt_descriptor_span(d,false);if(pack>UINT32_MAX||gemm>UINT32_MAX)return false;
 }
 return true;
}
/* Tables are authenticated/validated at dispatch. Local neighbour checks prevent
 * accidental gap/overlap use without re-scanning all descriptors per grid. */
static inline int opt_find_descriptor(const HunetFusedDescriptorV1 *ds,uint32_t count,uint32_t grid,bool pack){
 if(!ds||!count||count>32||opt_descriptor_base(ds[0],pack))return -1;
 uint32_t lo=0,hi=count;
 while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(opt_descriptor_base(ds[mid],pack)<=grid)lo=mid+1;else hi=mid;}
 if(!lo)return -1;
 uint32_t i=lo-1,base=opt_descriptor_base(ds[i],pack);uint64_t span=opt_descriptor_span(ds[i],pack),end=uint64_t(base)+span;
 if(!span||end>UINT32_MAX||grid>=end)return -1;
 if(i&&uint64_t(opt_descriptor_base(ds[i-1],pack))+opt_descriptor_span(ds[i-1],pack)!=base)return -1;
 if(i+1<count&&end!=opt_descriptor_base(ds[i+1],pack))return -1;
 return int(i);
}
static inline uint16_t opt_pack_element(const HunetFusedStageV1&s,const HunetFusedDescriptorV1&d,const float*x,uint32_t kk,uint32_t nn){
 float value=0;if(kk<d.k&&nn<d.count){uint32_t pos=d.offset+nn;
  if(s.flags==2){int64_t ix=int64_t(pos)+kk-s.pad;if(ix>=0&&ix<s.iw)value=x[ix];}
  else {uint32_t area=s.kernel_k*s.kernel_k,ic=kk/area,ky=kk/s.kernel_k%s.kernel_k,kx=kk%s.kernel_k;
   int64_t iy=int64_t(pos/s.ow)*s.stride+ky-s.pad,ix=int64_t(pos%s.ow)*s.stride+kx-s.pad;
   if(iy>=0&&ix>=0&&iy<s.ih&&ix<s.iw)value=x[uint64_t(ic)*s.ih*s.iw+uint64_t(iy)*s.iw+ix];}
 }return hunet_half(value);
}
static inline void opt_pack_row(const HunetFusedStageV1&s,const HunetFusedDescriptorV1&d,const float*x,uint16_t*y,uint32_t kk,uint32_t lane,uint32_t step){
 if(!step||kk>=d.kp||lane>=step)return;
 const uint32_t hw=s.ih*s.iw;
 const bool direct=s.flags!=2&&s.kernel_k==1&&s.stride==1&&!s.pad&&s.ih==s.oh&&s.iw==s.ow;
 if(direct){for(uint32_t nn=lane;nn<d.np;nn+=step)y[uint64_t(kk)*d.np+nn]=hunet_half(kk<d.k&&nn<d.count?x[uint64_t(kk)*hw+d.offset+nn]:0.0f);return;}
 if(s.flags==2){for(uint32_t nn=lane;nn<d.np;nn+=step)y[uint64_t(kk)*d.np+nn]=opt_pack_element(s,d,x,kk,nn);return;}
 const uint32_t area=s.kernel_k*s.kernel_k,ic=kk/area,ky=kk/s.kernel_k%s.kernel_k,kx=kk%s.kernel_k;
 uint32_t pos=d.offset+lane,row=pos/s.ow,col=pos%s.ow;
 for(uint32_t nn=lane;nn<d.np;nn+=step){
  float value=0;int64_t iy=int64_t(row)*s.stride+ky-s.pad,ix=int64_t(col)*s.stride+kx-s.pad;
  if(kk<d.k&&nn<d.count&&iy>=0&&ix>=0&&iy<s.ih&&ix<s.iw)value=x[uint64_t(ic)*hw+uint64_t(iy)*s.iw+ix];
  y[uint64_t(kk)*d.np+nn]=hunet_half(value);
  col+=step;if(col>=s.ow){row+=col/s.ow;col%=s.ow;}
 }
}
template<class Read> static inline void opt_pack_row_view(const HunetFusedStageV1&s,const HunetFusedDescriptorV1&d,const OptView&view,Read &read,uint16_t*y,uint32_t kk,uint32_t lane,uint32_t step){
 if(!step||lane>=step||kk>=d.kp)return;
 for(uint32_t nn=lane;nn<d.np;nn+=step){float value=0;
  if(kk<d.k&&nn<d.count){uint32_t pos=d.offset+nn,channel=0;int64_t row=0,col;
   if(s.flags==2)col=int64_t(pos)+kk-s.pad;
   else {uint32_t area=s.kernel_k*s.kernel_k;channel=kk/area;row=int64_t(pos/s.ow)*s.stride+kk/s.kernel_k%s.kernel_k-s.pad;col=int64_t(pos%s.ow)*s.stride+kk%s.kernel_k-s.pad;}
   if(row>=0&&col>=0&&row<s.ih&&col<s.iw){auto address=opt_view_address(view,channel,uint32_t(row),uint32_t(col));if(address.valid)value=read(address.gpu);}
  }y[uint64_t(kk)*d.np+nn]=hunet_half(value);
 }
}
