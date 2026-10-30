#include "mosaic_coop.h"
#include "mosaic_point_lane.h"
#include "device/mosaic_s8/profile.h"
#include <errno.h>
int vxnn_mosaic_coop_primitive(const VxnnMosaicPrimitive*p,const VxnnMosaicBuffers*b,uint32_t core,const mosaic_s8_ops*io){
 if(!io||!io->agree||!io->synchronize||io->participant>3)return -EINVAL;
 int rc=!p||!b||core>3||io->core!=core||!p->count||p->count>4096||p->opcode<1||p->opcode>8?-EINVAL:0;
 rc=io->agree(io->context,rc);if(rc)return rc;
 rc=!((p->schedule_version==1&&p->weight_layout_version==1)||(p->schedule_version==2&&p->weight_layout_version==2))?-EINVAL:0;
 rc=io->agree(io->context,rc);if(rc)return rc;
 const auto&g=p->geometry;
 rc=!g.ci||g.ci>128||!g.co||g.co>128||g.h<2||g.h>960||g.w<2||g.w>1280||(g.k!=3&&g.k!=9)||(g.stride!=1&&g.stride!=2)||(g.upsample!=1&&g.upsample!=2)?-EINVAL:0;
 rc=io->agree(io->context,rc);if(rc)return rc;
 const uint32_t oh=(g.h*g.upsample+g.stride-1)/g.stride,ow=(g.w*g.upsample+g.stride-1)/g.stride;
 const uint32_t input=g.ci*g.h*g.w,spatial=oh*ow,output=g.co*spatial,inner=g.ci*g.k*g.k;
 rc=oh>960||ow>1280||inner>2592||g.kpad!=(inner+31)/32*32?-EINVAL:0;
 const uint32_t elements=p->opcode==1?input:output,items=p->opcode==2?((g.co+7)/8)*((spatial+7)/8):p->opcode==3?g.co:(elements+255)/256;
 if(p->first>=items||p->count>items-p->first)rc=-ERANGE;
 rc=io->agree(io->context,rc);if(rc)return rc;
 if(p->opcode==2){rc=vxnn_mosaic_primitive(p,b,core,io);return io->agree(io->context,rc);}
 uint64_t start=mosaic_profile_start(*io),units=0;
 if(p->opcode==3){
  if(!io->participant)rc=vxnn_mosaic_primitive(p,b,core,nullptr);
  rc=io->agree(io->context,rc);if(rc)return rc;
  for(uint32_t channel=p->first;channel<p->first+p->count;++channel)if(channel%4==core)units+=spatial;
 }else for(uint32_t item=p->first;item<p->first+p->count;++item)if(item%4==core){
  const uint32_t first=item*256,count=elements-first<256?elements-first:256;
  mosaic_point_lane_args a{p->opcode,io->participant,p->opcode==1?g.h*g.w:spatial,elements,first,count,p->scale,p->save_input};
  a.input=p->opcode==1||p->opcode>=7||p->separate_input?b->input.data:b->output.data;
  a.skip=b->skip.data;a.stats=b->stats.data;a.gamma=b->gamma.data;a.beta=b->beta.data;a.output=b->output.data;a.saved_input=b->skip.data;a.quant=b->quant.data;
  mosaic_point_lane_prepared prepared;
  rc=io->agree(io->context,mosaic_point_lane_prepare(&a,&prepared));if(rc)return rc;
  if(p->opcode==1||p->opcode>=7){mosaic_point_lane_commit(&prepared,0,MOSAIC_POINT_COMMIT_QUANT);rc=io->agree(io->context,io->synchronize(io->context));if(rc)return rc;}
  mosaic_point_lane_commit(&prepared,0,MOSAIC_POINT_COMMIT_FLOAT);
  rc=io->agree(io->context,io->synchronize(io->context));if(rc)return rc;units+=count;
 }
 if(units)mosaic_profile_stop(*io,MOSAIC_STAGE_POINT_STATS_V1,start,units,units*4);
 return io->agree(io->context,0);
}
