/* SPDX-License-Identifier: MIT */
#pragma once
#include <vxl/hunet_fused_wire.h>
#include "hunet_math.h"

/* Same scalar indexing and explicit FP16 RNE as frozen hunet_pack_row.
   Typed pointers also let host tests exercise it without truncating a VA. */
static inline void hunet_fused_pack_row(const HunetFusedStageV1&s,
 const HunetFusedDescriptorV1&d,const float*x,uint16_t*y,
 uint32_t kk,uint32_t lane,uint32_t step){
 uint32_t hw=s.ih*s.iw,ic=0,ky=0,kx=0;
 if(s.flags!=2){ic=kk/(s.kernel_k*s.kernel_k);ky=(kk/s.kernel_k)%s.kernel_k;kx=kk%s.kernel_k;}
 const bool direct=s.flags!=2&&s.kernel_k==1&&s.stride==1&&s.pad==0&&s.ih==s.oh&&s.iw==s.ow;
 for(uint32_t nn=lane;nn<d.np;nn+=step){
  float value=0;
  if(kk<d.k&&nn<d.count){uint32_t pos=d.offset+nn;
   if(s.flags==2){int ix=int(pos)+int(kk)-int(s.pad);if(ix>=0&&ix<int(s.iw))value=x[ix];}
   else if(direct)value=x[kk*hw+pos];
   else{int iy=int((pos/s.ow)*s.stride+ky)-int(s.pad),ix=int((pos%s.ow)*s.stride+kx)-int(s.pad);
    if(iy>=0&&ix>=0&&iy<int(s.ih)&&ix<int(s.iw))value=x[ic*hw+uint32_t(iy)*s.iw+uint32_t(ix)];}
  }
  y[kk*d.np+nn]=hunet_half(value);
 }
}
static inline uint32_t hunet_fused_acc_row(uint32_t row,uint32_t lane,uint32_t reg){return row+lane/2+(reg/2)*2;}
static inline uint32_t hunet_fused_acc_col(uint32_t col,uint32_t lane,uint32_t reg){return col+lane%2+(reg%2)*2;}
static inline void hunet_fused_store(const HunetFusedDescriptorV1&d,float*out,const float*bias,
 uint32_t tile_row,uint32_t tile_col,uint32_t lane,uint32_t reg,float acc){
 uint32_t row=hunet_fused_acc_row(tile_row,lane,reg),col=hunet_fused_acc_col(tile_col,lane,reg);
 if(row<d.m&&col<d.count)out[row*d.n+d.offset+col]=bias?acc+bias[row]:acc;
}
