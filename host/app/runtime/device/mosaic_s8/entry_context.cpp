#include "entry.h"
#include <cstring>
#include <cmath>
#include <cerrno>
int mosaic_entry_layout(uint32_t w,uint32_t h,mosaic_layout *out){
 if(!out||w<8||w>1280||h<8||h>960||w%4||h%4)return -EINVAL;
 *out={};out->width=w;out->height=h;
 uint64_t spatial=uint64_t(w)*h,pow2=1;while(pow2<spatial)pow2*=2;
 uint64_t scratch=((2ULL*8*2592*2+64*8+384+32+63)/64*64)*4;
 const uint64_t size[9]={0x800000,0x1000000,spatial*128,spatial*128,spatial*64,spatial*32,pow2*32,1536,scratch};
 uint64_t cursor=0x82000000;
 for(unsigned i=0;i<9;i++){
  uint64_t bytes=(size[i]+63)/64*64;
  out->region[i]={i<2?(i?0x80800000ULL:0x80000000ULL):cursor+64,bytes,i<2?0u:64u,i<2?1u:0u};
  if(i>=2)cursor+=bytes+128;
 }
 out->end=cursor;return mosaic_layout_check(out);
}
static bool weights_span(const void *p,size_t count,size_t unit){
 uintptr_t address=reinterpret_cast<uintptr_t>(p);
 return p&&address%unit==0&&address>=0x80800000ULL&&address<0x81800000ULL&&count<=(0x81800000ULL-address)/unit;
}
int mosaic_entry_prepare(const mosaic_job_v1 *job,const mosaic_entry_catalog *catalog,size_t count,uint32_t core,mosaic_device_context *c){
 if(!job||!catalog||!count||count>32||core>3||!c)return -EINVAL;
 unsigned binding=0;for(unsigned i=0;i<16;i++)binding|=job->binding_tag[i];
 if(!binding||!job->sequence||!job->tile_count||job->tile_count>4096)return -EINVAL;
 const mosaic_entry_catalog *found=nullptr;
 for(size_t i=0;i<count;i++)if(!memcmp(job->layout_tag,catalog[i].layout_tag,16)&&!memcmp(job->graph_tag,catalog[i].graph_tag,16)){
  if(found)return -EINVAL;
  found=&catalog[i];
 }
 if(!found)return -ESTALE;
 if(found->layout.width!=found->width||found->layout.height!=found->height||mosaic_layout_check(&found->layout))return -EINVAL;
 uint32_t phase,items=0;
 for(phase=0;phase<77;phase++){
  uint32_t layer,op;int rc=mosaic_phase_description(phase,found->width,found->height,&layer,&op,&items);if(rc)return rc;
  if(layer==job->layer&&op==job->op)break;
 }
 if(phase==77)return -EINVAL;
 mosaic_ctx trusted={*job,items,&found->layout,nullptr};int rc=mosaic_validate_job(&trusted,job);if(rc)return rc;
 for(unsigned i=0;i<16;i++){
  mosaic_layer l;rc=mosaic_layer_geometry(i,found->width,found->height,&l);if(rc)return rc;
  const auto &p=found->parameters[i];
  if(p.weight_count!=size_t(l.co)*l.kpad||p.coeff_count!=l.co*2||!weights_span(p.weights,p.weight_count,2)||!weights_span(p.coeff,p.coeff_count,4)||!std::isfinite(p.sa)||p.sa<=0)return -EINVAL;
  if(i!=15&&(!weights_span(p.gamma,l.co,4)||!weights_span(p.beta,l.co,4)))return -EINVAL;
 }
 *c={};c->width=found->width;c->height=found->height;c->phase_index=phase;c->core=core;c->expected=*job;c->layout=found->layout;
 memcpy(c->parameters,found->parameters,sizeof(c->parameters));auto &m=c->memory;const auto *r=c->layout.region;
 m.fp[0]=reinterpret_cast<float *>(uintptr_t(r[MOSAIC_FP_A].address));m.fp[1]=reinterpret_cast<float *>(uintptr_t(r[MOSAIC_FP_B].address));m.fp_count=size_t(r[MOSAIC_FP_A].bytes/4);
 m.quantized=reinterpret_cast<int16_t *>(uintptr_t(r[MOSAIC_Q_INPUT].address));m.quantized_count=size_t(r[MOSAIC_Q_INPUT].bytes/2);
 m.skip=reinterpret_cast<float *>(uintptr_t(r[MOSAIC_SKIP].address));m.skip_count=size_t(r[MOSAIC_SKIP].bytes/4);
 m.stats=reinterpret_cast<float *>(uintptr_t(r[MOSAIC_STATS].address));m.stats_count=size_t(r[MOSAIC_STATS].bytes/4);m.tile_capacity=8*2592;
 size_t tree=size_t(r[MOSAIC_NORM_TREE].bytes/32),stride=size_t(r[MOSAIC_TILE_SCRATCH].bytes/4);
 for(unsigned i=0;i<4;i++){
  uintptr_t scratch=uintptr_t(r[MOSAIC_TILE_SCRATCH].address)+i*stride;
  m.a[i]=reinterpret_cast<int16_t *>(scratch);m.b[i]=reinterpret_cast<int16_t *>(scratch+8*2592*2);m.tile[i]=reinterpret_cast<int64_t *>(scratch+2*8*2592*2);
  uintptr_t work=uintptr_t(r[MOSAIC_NORM_TREE].address)+i*tree*8;m.norm[i]={reinterpret_cast<float *>(work),reinterpret_cast<float *>(work+tree*4),tree};
 }
 return 0;
}
int mosaic_entry_metadata(const mosaic_job_v1 *j,uint32_t core,unsigned char out[64]){
 if(!j||!out||core>3)return -EINVAL;
 memcpy(out,j->run_id,16);memcpy(out+16,j->binding_tag,16);
 uint32_t fields[8]={j->generation,j->sequence,j->op,j->layer,j->tile_first,j->tile_count,j->input_epoch,core};
 for(unsigned i=0;i<8;i++)for(unsigned b=0;b<4;b++)out[32+i*4+b]=static_cast<unsigned char>(fields[i]>>(b*8));
 return 0;
}
