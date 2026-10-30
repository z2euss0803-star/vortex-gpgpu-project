#pragma once
#include <vxunified/types.hpp>
#include "device/vxnn/mosaic_wire_reuse_v4.h"
#include "device/vxnn/mosaic_profile_ops.h"
#include <array>
#include <functional>
#include <cstring>
namespace vxunified {
using MosaicProfileRecords=std::array<MosaicProfileRecordV1,4>;
inline int mosaic_profile_layout(const VxnnMosaicArgs&a,bool relocated,VxnnMosaicShape*shape,MosaicWorkLayoutV1*layout){
 int rc=a.abi==4?vxnn_mosaic_validate_reuse_v4(&a,relocated,shape):a.abi==3?vxnn_mosaic_validate_coop_v3(&a,relocated,shape):-EINVAL;
 if(rc)return rc;
 return a.abi==4?mosaic_reuse_work_layout_v4(a.opcode,a.kpad,shape->norm_elements,layout):mosaic_work_layout_v1(a.opcode,a.kpad,shape->norm_elements,layout);
}

// Call only inside the owner's post-matching-ACK and post-guard boundary.
// Readback changes no device state and never retries/retires/releases a job.
inline Result<MosaicProfileRecords>mosaic_profile_read(const VxnnMosaicArgs&a,BufferRef work,const std::function<Result<void>(BufferRef,vxnn::MutableBytes)>&read){
 using R=Result<MosaicProfileRecords>;auto fail=[](int e,const char*s){return R::failure({e,"MOSAIC_PROFILE",s});};
 VxnnMosaicShape shape{};MosaicWorkLayoutV1 layout{};
 if(a.reserved!=1||mosaic_profile_layout(a,false,&shape,&layout)||!read||!work.handle||!work.object_generation||!work.device_generation||!(work.access&1)||work.bytes!=layout.total_work_bytes||work.offset%64||work.offset>UINT64_MAX-work.bytes)return fail(EINVAL,"instrumented explicit ABI3/4 exact owned WORK required");
 MosaicProfileRecords records{};
 for(unsigned c=0;c<4;++c){
  BufferRef ref=work;ref.offset+=64+uint64_t(c)*layout.core_stride+layout.profile_offset;ref.bytes=256;ref.access=1;
  auto got=read(ref,{reinterpret_cast<uint8_t*>(&records[c]),256});if(!got)return R::failure(*got.error);
  const auto&h=records[c].header;
  if(h.magic!=MOSAIC_PROFILE_MAGIC_V1||h.version!=MOSAIC_PROFILE_VERSION_V1||h.bytes!=256||h.layout_version!=layout.layout_version||h.opcode!=a.opcode||h.core!=c||h.first!=a.first||h.count!=a.count||h.options!=1||h.timer_kind!=MOSAIC_TIMER_RV32_CSR64_V1||h.schedule_version!=layout.schedule_version||h.weight_layout_version!=layout.weight_layout_version||h.status!=MOSAIC_PROFILE_COMPLETE_V1||h.error_code||h.argument_tag!=mosaic_profile_argument_tag(a)||h.valid_stages&~63u)return fail(EPROTO,"profile header/op/core/range/geometry/timer identity");
  uint32_t owned=0,packs=0,weight_prepares=0,last_nb=UINT32_MAX,last_mb=UINT32_MAX;
  const uint32_t spatial=((a.h*a.upsample+a.stride-1)/a.stride)*((a.w*a.upsample+a.stride-1)/a.stride),mblocks=(a.co+7)/8,nblocks=(spatial+7)/8;
  for(uint32_t i=a.first;i<a.first+a.count;++i){
   const uint32_t nb=a.opcode==2?(a.abi==4?i/mblocks:i%nblocks):0,mb=a.opcode==2?(a.abi==4?i%mblocks:i/nblocks):0;
   const uint32_t owner=a.opcode==2&&a.abi==4?nb%4:i%4;if(owner!=c)continue;++owned;
   if(a.opcode==2){if(a.abi==3||nb!=last_nb){++packs;last_nb=nb;}if(mb!=last_mb){++weight_prepares;last_mb=mb;}}
  }
  for(unsigned stage=0;stage<6;++stage){const auto&v=records[c].stage[stage];if(v.calls&&!v.cycles)return fail(ENODATA,"CSR timer did not advance; timing unavailable, not zero elapsed");if(bool(h.valid_stages&(1u<<stage))!=bool(v.calls)||(!v.calls&&(v.cycles||v.units||v.logical_bytes))||(!owned&&v.calls))return fail(EPROTO,"stage presence or inactive-core counters");}
  if(owned&&a.opcode==2){const auto&s=records[c].stage;if(s[0].calls!=packs||s[1].calls!=weight_prepares||s[2].calls!=owned||s[2].units!=uint64_t(owned)*a.ci*a.k*a.k||s[3].calls!=owned||s[3].units!=uint64_t(owned)*24||s[4].calls!=owned||s[5].calls)return fail(EPROTO,"exact convolution tiles/K/page work counters");}
  if(owned&&a.opcode!=2){if(h.valid_stages!=(1u<<5)||records[c].stage[5].calls!=1)return fail(EPROTO,"point/stat core phase counters");}
 }
 return R::success(records);
}
}
