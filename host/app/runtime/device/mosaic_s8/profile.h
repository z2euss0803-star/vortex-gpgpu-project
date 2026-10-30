#ifndef MOSAIC_S8_PROFILE_H
#define MOSAIC_S8_PROFILE_H
#include "tile.h"
#include "../vxnn/mosaic_work_layout_v1.h"
static inline uint64_t mosaic_profile_start(const mosaic_s8_ops&io){
 return io.profile&&io.participant==0&&(io.profile->header.options&MOSAIC_OPTION_CYCLES_V1)?io.now(io.context):0;
}
static inline void mosaic_profile_stop(const mosaic_s8_ops&io,unsigned stage,uint64_t start,uint64_t units,uint64_t bytes){
 if(!io.profile||io.participant!=0)return;
 auto&v=io.profile->stage[stage];if(io.profile->header.options&MOSAIC_OPTION_CYCLES_V1)v.cycles+=io.now(io.context)-start;
 v.units+=units;v.logical_bytes+=bytes;++v.calls;io.profile->header.valid_stages|=1u<<stage;
}
#endif
