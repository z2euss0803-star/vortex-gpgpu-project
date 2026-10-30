#include "mosaic_ops.h"
#include "ranges.h"
#include "device/mosaic_s8/graph_internal.h"
#include "device/mosaic_s8/norm_compat.h"
#include <errno.h>
#include <math.h>
#include <string.h>
#if !defined(__riscv)
#include <fenv.h>
#endif
namespace {
bool round_to_nearest(){
#if defined(__riscv)
 uint32_t mode;__asm__ volatile("csrr %0, frm":"=r"(mode));return mode==0;
#else
 return fegetround()==FE_TONEAREST;
#endif
}
struct Range {const void*p;size_t bytes;bool write;};
template<class T>bool add(Range*r,unsigned&n,VxnnMosaicSpan<T>s,size_t count,bool write){
 if(!count||count>SIZE_MAX/sizeof(T)||s.count<count||!vxnn_device::span(s.data,count*sizeof(T),alignof(T)))return false;
 Range next{s.data,count*sizeof(T),write};
 for(unsigned i=0;i<n;i++)if((write||r[i].write)&&vxnn_device::overlap(next.p,next.bytes,r[i].p,r[i].bytes))return false;
 if(n>=16)return false;
 r[n++]=next;return true;
}
}
int vxnn_mosaic_primitive(const VxnnMosaicPrimitive*p,const VxnnMosaicBuffers*b,uint32_t core,const mosaic_s8_ops*io){
 if(!p||!b||core>3||p->opcode<1||p->opcode>8||!p->count||p->count>4096||(p->save_input&&p->opcode!=1&&p->opcode<7)||(p->separate_input&&(p->opcode<3||p->opcode>=7))||!round_to_nearest())return -EINVAL;
 if(!((p->schedule_version==1&&p->weight_layout_version==1)||(p->schedule_version==2&&p->weight_layout_version==2)))return -EINVAL;
 const auto&g=p->geometry;
 if(!g.ci||g.ci>128||!g.co||g.co>128||g.h<2||g.h>960||g.w<2||g.w>1280||(g.k!=3&&g.k!=9)||(g.stride!=1&&g.stride!=2)||(g.upsample!=1&&g.upsample!=2))return -EINVAL;
 const uint32_t inner=g.ci*g.k*g.k,oh=(g.h*g.upsample+g.stride-1)/g.stride,ow=(g.w*g.upsample+g.stride-1)/g.stride;
 if(inner>2592||g.kpad!=(inner+31)/32*32||g.h*g.upsample<=g.k/2||g.w*g.upsample<=g.k/2||oh>960||ow>1280)return -EINVAL;
 const size_t input=size_t(g.ci)*g.h*g.w,spatial=size_t(oh)*ow,output=size_t(g.co)*spatial;
 const size_t items=p->opcode==1?(input+255)/256:p->opcode==2?((g.co+7)/8)*((spatial+7)/8):p->opcode==3?g.co:(output+255)/256;
 if(p->first>=items||p->count>items-p->first)return -ERANGE;
 Range ranges[16];unsigned n=0;
 if(p->opcode>=7){
  if(!isfinite(p->scale)||p->scale<=0||!add(ranges,n,b->input,output,false)||!add(ranges,n,b->quant,output,true)||!add(ranges,n,b->stats,g.co*3,false)||!add(ranges,n,b->gamma,g.co,false)||!add(ranges,n,b->beta,g.co,false)||(p->opcode==8&&!add(ranges,n,b->skip,output,false))||(p->save_input&&!add(ranges,n,b->output,output,true)))return -EINVAL;
  volatile float inverse=1.f/p->scale;
  float values[256];
  for(uint32_t item=p->first;item<p->first+p->count;++item)if(item%4==core){
   size_t begin=size_t(item)*256,count=output-begin;if(count>256)count=256;
   size_t offset=0;
   while(offset<count){
    size_t pos=begin+offset,channel=pos/spatial,part=spatial-pos%spatial;if(part>count-offset)part=count-offset;
    int rc=mosaic_norm_apply_compat_v3(b->input.data+pos,part,b->stats.data+channel*3,b->gamma.data[channel],b->beta.data[channel],spatial,values+offset);if(rc)return rc;
    offset+=part;
   }
   for(size_t i=0;i<count;++i){
    if(!isfinite(values[i]))return -EDOM;
    if(p->opcode==8&&!isfinite(b->skip.data[begin+i]))return -EDOM;
    volatile float value=p->opcode==7?(values[i]>0?values[i]:0.f):values[i]+b->skip.data[begin+i];if(!isfinite(value))return -ERANGE;values[i]=value;
   }
   int rc=mosaic_quantize_input(values,count,inverse,b->quant.data+begin);if(rc)return rc;
   if(p->save_input)memcpy(b->output.data+begin,values,count*4);
  }
  return 0;
 }
 if(p->opcode==1){
  if(!isfinite(p->scale)||p->scale<=0||!add(ranges,n,b->input,input,false)||!add(ranges,n,b->quant,input,true)||(p->save_input&&!add(ranges,n,b->skip,input,true)))return -EINVAL;
  volatile float inverse=1.f/p->scale;
  for(uint32_t item=p->first;item<p->first+p->count;item++)if(item%4==core){size_t begin=size_t(item)*256,count=input-begin;if(count>256)count=256;
   // The frozen quantizer checks RNE and finite input before the skip copy.
   int rc=mosaic_quantize_input(b->input.data+begin,count,inverse,b->quant.data+begin);if(rc)return rc;
   if(p->save_input)memcpy(b->skip.data+begin,b->input.data+begin,count*4);
  }
  return 0;
 }
 const float*source=b->output.data;
 if(p->separate_input){if(!add(ranges,n,b->input,output,false))return -EINVAL;source=b->input.data;}
 if((p->opcode!=3||!p->separate_input)&&!add(ranges,n,b->output,output,true))return -EINVAL;
 if(p->opcode==2){
  if(!io||io->core!=core||io->warp||io->participant>4||!io->begin||!io->step||!io->read||!io->release||!io->fence||!io->now||!io->poll_limit||io->poll_limit>65536||(io->participant!=4&&!io->broadcast)||!isfinite(p->scale)||p->scale<=0)return -EINVAL;
  const size_t weight_elements=p->weight_layout_version==2?size_t((g.co+7)/8)*8*g.kpad:size_t(g.co)*g.kpad;
  if(!add(ranges,n,b->quant,input,false)||!add(ranges,n,b->weights,weight_elements,false)||!add(ranges,n,b->coeff,g.co*2,false)||!add(ranges,n,b->a,g.kpad*8,true)||!add(ranges,n,b->b,g.kpad*8,true)||!add(ranges,n,b->acc,64,true))return -EINVAL;
  for(uint32_t c=0;c<g.co;c++)if(!isfinite(b->coeff.data[c*2])||b->coeff.data[c*2]<=0||!isfinite(b->coeff.data[c*2+1]))return -EINVAL;
  mosaic_layer layer={g.ci,g.co,g.h,g.w,g.k,g.stride,g.upsample,g.kpad,b->quant.data,input};
  mosaic_device_context ctx{};ctx.core=core;ctx.schedule_version=p->schedule_version;ctx.weight_layout_version=p->weight_layout_version;ctx.parameters[0]={b->weights.data,weight_elements,b->coeff.data,g.co*2,p->scale,nullptr,nullptr};ctx.io[core]=*io;
  ctx.memory.quantized=b->quant.data;ctx.memory.fp[1]=b->output.data;ctx.memory.a[core]=b->a.data;ctx.memory.b[core]=b->b.data;ctx.memory.tile[core]=b->acc.data;ctx.memory.tile_capacity=g.kpad*8;
  mosaic_job_v1 job{};job.tile_first=p->first;job.tile_count=p->count;
  // Only the arithmetic primitive is reused. Its synthetic local layer0
  // selects the passed buffers, not model topology or a legacy device job.
  return mosaic_convolve_range(&job,&ctx,&layer);
 }
 if(p->opcode==3){
  size_t tree=1;while(tree<spatial)tree*=2;
  if(!add(ranges,n,b->stats,g.co*3,true)||!add(ranges,n,VxnnMosaicSpan<float>{b->norm.hi,b->norm.count},tree,true)||!add(ranges,n,VxnnMosaicSpan<float>{b->norm.lo,b->norm.count},tree,true))return -EINVAL;
  auto workspace=b->norm;
  for(uint32_t channel=p->first;channel<p->first+p->count;channel++)if(channel%4==core){int rc=mosaic_norm_stats_compat_v3(source+size_t(channel)*spatial,spatial,&workspace,b->stats.data+channel*3);if(rc)return rc;}
  return 0;
 }
 if(p->opcode==4&&(!add(ranges,n,b->stats,g.co*3,false)||!add(ranges,n,b->gamma,g.co,false)||!add(ranges,n,b->beta,g.co,false)))return -EINVAL;
 if(p->opcode==6&&!add(ranges,n,b->skip,output,false))return -EINVAL;
 for(uint32_t item=p->first;item<p->first+p->count;item++)if(item%4==core){
  size_t begin=size_t(item)*256,end=begin+256;if(end>output)end=output;
  if(p->opcode==4){while(begin<end){size_t channel=begin/spatial,count=spatial-begin%spatial;if(count>end-begin)count=end-begin;int rc=mosaic_norm_apply_compat_v3(source+begin,count,b->stats.data+channel*3,b->gamma.data[channel],b->beta.data[channel],spatial,b->output.data+begin);if(rc)return rc;begin+=count;}}
  else for(size_t i=begin;i<end;i++){if(!isfinite(source[i])||(p->opcode==6&&!isfinite(b->skip.data[i])))return -EDOM;volatile float value=p->opcode==5?(source[i]>0?source[i]:0.f):source[i]+b->skip.data[i];if(!isfinite(value))return -ERANGE;b->output.data[i]=value;}
 }
 return 0;
}
