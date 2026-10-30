// Bounded scheduling for the controlled FP16/FP32 HUNet program.
#pragma once
#include <vxl/hunet_fused_wire.h>
#include <stdint.h>
#include <errno.h>
struct VxnnHunetGroupPlan {
 uint32_t count,tile_n,total,prefix[33];
};
static inline int vxnn_hunet_group_plan(const HunetFusedDescriptorV1*ds,uint32_t count,uint32_t tile_n,VxnnHunetGroupPlan*out){
 if(!ds||!out||!count||count>32||(tile_n!=1&&tile_n!=2))return -EINVAL;
 VxnnHunetGroupPlan p{};p.count=count;p.tile_n=tile_n;
 for(uint32_t i=0;i<count;i++){
  const auto&d=ds[i];if(!d.mp||d.mp>16384||d.mp%8||!d.np||d.np>65536||d.np%4)return -EINVAL;
  uint64_t n=uint64_t(d.mp/8)*((d.np/4+tile_n-1)/tile_n)+p.prefix[i];if(n>UINT32_MAX)return -EOVERFLOW;
  p.prefix[i+1]=uint32_t(n);
 }
 p.total=p.prefix[count];*out=p;return 0;
}
static inline int vxnn_hunet_group_find(const VxnnHunetGroupPlan&p,uint32_t grid){
 if(!p.count||p.count>32||grid>=p.total)return -1;
 uint32_t lo=0,hi=p.count;
 while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(p.prefix[mid]<=grid)lo=mid+1;else hi=mid;}
 return lo?int(lo-1):-1;
}
template<unsigned Tiles,class Ops> static inline void vxnn_hunet_group_compute(const HunetFusedDescriptorV1&d,uint32_t row,uint32_t column_group,Ops&p){
 static_assert(Tiles==1||Tiles==2,"bounded adjacent N tiles");
 const uint32_t first=column_group*Tiles,ncols=d.np/4;
 if(row>=d.mp||row%8||first>=ncols)return;
 uint32_t live=ncols-first;if(live>Tiles)live=Tiles;
 // Keep the single-tile tail outside the full-pair K loop. The Vortex target
 // pass must not add a live-tile split/join at every K8 iteration.
 if constexpr(Tiles==2)if(live==1){vxnn_hunet_group_compute<1>(d,row,first,p);return;}
 const uint32_t k_end=d.kp;
 typename Ops::A a;typename Ops::B b;typename Ops::C acc[Tiles];
 p.fill(acc[0]);if constexpr(Tiles==2)p.fill(acc[1]);
 // Each output retains its original increasing-K chain. Only A is shared.
 uint32_t k=0;
 if constexpr(Tiles==2)for(;k+16<=k_end;k+=16){
  p.load_a(a,row,k);p.load_b(b,k,first*4);p.mma(acc[0],a,b,k);
  p.load_b(b,k,(first+1)*4);p.mma(acc[1],a,b,k);
  // The second output keeps the fixed hardware accumulator bank for the next
  // K8 step. Output order is independent; each output's K order is unchanged.
  p.load_a(a,row,k+8);p.load_b(b,k+8,(first+1)*4);p.mma(acc[1],a,b,k+8);
  p.load_b(b,k+8,first*4);p.mma(acc[0],a,b,k+8);
 }
 for(;k<k_end;k+=8){
  p.load_a(a,row,k);
  p.load_b(b,k,first*4);p.mma(acc[0],a,b,k);
  if constexpr(Tiles==2){p.load_b(b,k,(first+1)*4);p.mma(acc[1],a,b,k);}
 }
 p.store(acc[0],row,first*4,(row/8)*ncols+first);
 if constexpr(Tiles==2)p.store(acc[1],row,(first+1)*4,(row/8)*ncols+first+1);
}
