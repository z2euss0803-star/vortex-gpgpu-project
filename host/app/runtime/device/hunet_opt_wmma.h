#pragma once
#include "hunet_opt_pack.h"
static inline uint32_t opt_group_tile(uint32_t group,uint32_t lane_tile,uint32_t tile_n){
 if((tile_n!=1&&tile_n!=2&&tile_n!=4)||lane_tile>=tile_n||uint64_t(group)*tile_n+lane_tile>UINT32_MAX)return UINT32_MAX;
 return group*tile_n+lane_tile;
}
/* Exact contiguous-core group distribution in frozen vx_spawn.c, group_size4. */
static inline uint32_t opt_expected_core(uint32_t group,uint32_t grid_groups){
 if(!grid_groups||group>=grid_groups)return UINT32_MAX;
 uint32_t active=uint32_t((uint64_t(grid_groups)+3)/4);if(active>4)active=4;
 uint32_t base=grid_groups/active,extra=grid_groups%active,offset=0;
 for(uint32_t core=0;core<active;core++){uint32_t count=base+(core<extra);if(group-offset<count)return core;offset+=count;}
 return UINT32_MAX;
}
static inline uint32_t opt_gemm_groups(const HunetFusedDescriptorV1&d,uint32_t tile_n){
 if((tile_n!=1&&tile_n!=2&&tile_n!=4)||!d.mp||d.mp%8||!d.np||d.np%4)return 0;
 uint64_t value=uint64_t(d.mp/8)*((uint64_t(d.np/4)+tile_n-1)/tile_n);return value>UINT32_MAX?0:uint32_t(value);
}
static inline uint32_t opt_group_prefixes(const HunetFusedDescriptorV1*ds,uint32_t count,uint32_t tile_n,uint32_t base[33]){
 if(!ds||!base||!count||count>32)return 0;
 base[0]=0;for(uint32_t i=0;i<count;i++){uint32_t groups=opt_gemm_groups(ds[i],tile_n);if(!groups||uint64_t(base[i])+groups>UINT32_MAX)return 0;base[i+1]=base[i]+groups;}
 return base[count];
}
static inline int opt_find_group(const uint32_t *base,uint32_t count,uint32_t grid){
 if(!base||!count||count>32||base[0]||grid>=base[count])return -1;
 uint32_t lo=0,hi=count;while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(base[mid]<=grid)lo=mid+1;else hi=mid;}
 return lo?int(lo-1):-1;
}
template<unsigned Tiles,class Ops> static inline void opt_wmma_group(const HunetFusedDescriptorV1&d,uint32_t row,uint32_t column_group,uint32_t lane,Ops &p){
 static_assert(Tiles==1||Tiles==2||Tiles==4,"bounded adjacent N tiles");
 typename Ops::A fw;typename Ops::B fb;typename Ops::C acc[Tiles];
 uint32_t first=opt_group_tile(column_group,0,Tiles),ncols=d.np/4;
 if(row>=d.mp||row%8||first>=ncols)return;
 uint32_t live=ncols-first;if(live>Tiles)live=Tiles;
 p.loop_begin();
 for(unsigned t=0;t<Tiles;t++)p.fill(acc[t]);
 for(uint32_t k=0;k<d.kp;k+=8){
  p.load_a(fw,row,k); // one weight fragment per K step, shared by all live tiles
  for(unsigned t=0;t<Tiles;t++)if(t<live){p.load_b(fb,k,(first+t)*4);p.mma(acc[t],fw,fb,t,k);}
 }
 p.loop_end();p.store_begin();
 for(unsigned t=0;t<Tiles;t++)if(t<live)p.store(acc[t],row,(first+t)*4,(row/8)*ncols+first+t);
 p.store_end();(void)lane;
}
/* Internal timing has per-core wall phases and separate per-warp activity.
 * Warp durations overlap; never sum them as exclusive core/job wall time. */
struct OptFusedTimingPage {
 uint8_t guard_left[64];uint64_t core_wall[4][2],warp_activity[4][4][2];
 uint32_t core_rc[4][2],scalar_error[4][4][4];uint8_t reserved[3360],guard_right[64];
};
static_assert(sizeof(OptFusedTimingPage)==4096,"timing page");
