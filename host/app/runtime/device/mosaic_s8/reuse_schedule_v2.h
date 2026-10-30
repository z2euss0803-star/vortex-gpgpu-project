#pragma once
// GPU-safe integer mapping only. This header changes no existing wire ABI.
#include <stdint.h>
#include <errno.h>
namespace mosaic_reuse {
enum class ScheduleId:uint32_t {LegacyChannelMajorV1=1,SpatialMajorV1=2};
struct ScheduleShape {uint32_t co=0,spatial=0,mblocks=0,nblocks=0,tile_count=0;ScheduleId id=ScheduleId::LegacyChannelMajorV1;};
struct TileAddress {uint32_t tile=0,mb=0,nb=0,m0=0,n0=0,owner=0;};
inline int make_schedule(uint32_t co,uint32_t spatial,ScheduleId id,ScheduleShape*out){
 if(!out||!co||co>128||!spatial||spatial>1280u*960u||(id!=ScheduleId::LegacyChannelMajorV1&&id!=ScheduleId::SpatialMajorV1))return -EINVAL;
 const uint32_t m=(co+7u)/8u,n=(spatial+7u)/8u;
 // At most 16*153600 real 8x8 TCU tiles under this explicit S8 bound.
 *out=ScheduleShape{co,spatial,m,n,m*n,id};return 0;
}
inline int validate_shape(const ScheduleShape&s){
 ScheduleShape expected{};if(make_schedule(s.co,s.spatial,s.id,&expected))return -EINVAL;
 return s.mblocks==expected.mblocks&&s.nblocks==expected.nblocks&&s.tile_count==expected.tile_count?0:-EINVAL;
}
namespace detail {
inline TileAddress map_valid_tile(const ScheduleShape&s,uint32_t t){
 const uint32_t mb=s.id==ScheduleId::SpatialMajorV1?t%s.mblocks:t/s.nblocks;
 const uint32_t nb=s.id==ScheduleId::SpatialMajorV1?t/s.mblocks:t%s.nblocks;
 return TileAddress{t,mb,nb,mb*8u,nb*8u,s.id==ScheduleId::SpatialMajorV1?nb%4u:t%4u};
}
}
inline int map_tile(const ScheduleShape&s,uint32_t t,TileAddress*out){
 if(!out||validate_shape(s))return -EINVAL;
 if(t>=s.tile_count)return -ERANGE;
 *out=detail::map_valid_tile(s,t);return 0;
}
inline int validate_job(const ScheduleShape&s,uint32_t first,uint32_t count){
 if(validate_shape(s)||!count||count>4096u)return -EINVAL;
 return first<s.tile_count&&count<=s.tile_count-first?0:-ERANGE;
}
// A B window depends on nb, input/layer identity and geometry. All of those
// identities must stay frozen for this one call. Cache state lives on this
// call's stack and cannot cross a job, input, layer, epoch or schedule change.
// Pack(nb,n0) and Consume(TileAddress) return int; no allocation or exception
// machinery is introduced here. Counts remain individual TCU tile invocations.
template<class Pack,class Consume>
inline int visit_job(const ScheduleShape&s,uint32_t first,uint32_t count,uint32_t core,Pack&&pack,Consume&&consume){
 const int valid=validate_job(s,first,count);if(valid)return valid;
 if(core>=4u)return -EINVAL;
 bool have_b=false;uint32_t last_nb=0;
 const uint32_t end=first+count;
 for(uint32_t t=first;t<end;++t){
  const TileAddress address=detail::map_valid_tile(s,t);
  if(address.owner!=core)continue;
  if(!have_b||last_nb!=address.nb){
   const int rc=pack(address.nb,address.n0);if(rc)return rc;
   last_nb=address.nb;have_b=true;
  }
  const int rc=consume(address);if(rc)return rc;
 }
 return 0;
}
}
