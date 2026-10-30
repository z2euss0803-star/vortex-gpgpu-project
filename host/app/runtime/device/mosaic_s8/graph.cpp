#include "graph_internal.h"
#include <stdint.h>
#include <string.h>
#include <math.h>
int mosaic_layer_geometry(uint32_t index,uint32_t width,uint32_t height,mosaic_layer *out){
 if(!out||index>=16||width<8||width>1280||height<8||height>960||width%4||height%4)return -EINVAL;
 const uint32_t co[16]={32,64,128,128,128,128,128,128,128,128,128,128,128,64,32,3};
 uint32_t h=height,w=width,ci=3;
 for(uint32_t i=0;i<=index;++i){
  uint32_t k=i==0||i==15?9:3,s=i==1||i==2?2:1,u=i==13||i==14?2:1;
  if(i==index){*out={ci,co[i],h,w,k,s,u,(ci*k*k+31)/32*32,nullptr,0};return 0;}
  h=(h*u+s-1)/s;w=(w*u+s-1)/s;ci=co[i];
 }
 return -EINVAL;
}
int mosaic_phase_description(uint32_t phase,uint32_t width,uint32_t height,uint32_t *layer,uint32_t *op,uint32_t *items){
 if(!layer||!op||!items)return -EINVAL;
 uint32_t cursor=0;
 for(uint32_t i=0;i<16;++i){
  mosaic_layer l;int rc=mosaic_layer_geometry(i,width,height,&l);if(rc)return rc;
  uint32_t ops[6]={1,2},count=2;
  if(i!=15){ops[count++]=3;ops[count++]=4;}
  if(mosaic_relu(i))ops[count++]=5;
  if(mosaic_residual(i))ops[count++]=6;
  for(uint32_t j=0;j<count;++j,++cursor)if(cursor==phase){
   *layer=i;*op=ops[j];uint32_t spatial=mosaic_oh(l)*mosaic_ow(l);
   *items=*op==1?(l.ci*l.h*l.w+255)/256:*op==2?((l.co+7)/8)*((spatial+7)/8):*op==3?l.co:(l.co*spatial+255)/256;
   return 0;
  }
 }
 return -ERANGE;
}
static int context_check(mosaic_device_context *c){
 if(c->core>3||c->layout.width!=c->width||c->layout.height!=c->height||mosaic_layout_check(&c->layout))return -EINVAL;
 auto &m=c->memory;auto &io=c->io[c->core];
 if(io.core!=c->core||io.warp||io.participant>4||!io.begin||!io.step||!io.read||!io.release||!io.fence||!io.now||(io.participant!=4&&!io.broadcast))return -EINVAL;
#if defined(__riscv)
 if(io.participant==4)return -EINVAL;
#endif
 if(m.fp_count<c->layout.region[MOSAIC_FP_A].bytes/4||m.quantized_count<c->layout.region[MOSAIC_Q_INPUT].bytes/2||m.skip_count<c->layout.region[MOSAIC_SKIP].bytes/4||m.stats_count<384||m.tile_capacity<8*2592)return -ERANGE;
 struct range{uintptr_t address;size_t bytes;};range ranges[25];unsigned count=0;
 auto add=[&](const void *p,size_t bytes,size_t alignment)->bool{
  uintptr_t address=reinterpret_cast<uintptr_t>(p);
  if(!p||address%alignment||address>UINTPTR_MAX-bytes)return false;
  for(unsigned i=0;i<count;++i)if(address<ranges[i].address+ranges[i].bytes&&ranges[i].address<address+bytes)return false;
  ranges[count++]={address,bytes};return true;
 };
 if(!add(m.fp[0],size_t(c->layout.region[MOSAIC_FP_A].bytes),4)||!add(m.fp[1],size_t(c->layout.region[MOSAIC_FP_B].bytes),4)||!add(m.quantized,size_t(c->layout.region[MOSAIC_Q_INPUT].bytes),2)||!add(m.skip,size_t(c->layout.region[MOSAIC_SKIP].bytes),4)||!add(m.stats,384*4,4))return -EINVAL;
 size_t tree=1;while(tree<size_t(c->width)*c->height)tree*=2;
 for(unsigned i=0;i<4;++i){
  if(m.norm[i].count<tree||!add(m.a[i],8*2592*2,2)||!add(m.b[i],8*2592*2,2)||!add(m.tile[i],64*8,8)||!add(m.norm[i].hi,tree*4,4)||!add(m.norm[i].lo,tree*4,4))return -EINVAL;
 }
 return 0;
}
int mosaic_execute_job(const mosaic_job_v1 *job,mosaic_device_context *c){
 if(!c||!job)return -EINVAL;
 if(c->failed)return c->failed;
 int rc=context_check(c);uint32_t layer=0,op=0,items=0;
 if(!rc)rc=mosaic_phase_description(c->phase_index,c->width,c->height,&layer,&op,&items);
 if(!rc&&(job->layer!=layer||job->op!=op))rc=-ESTALE;
 if(!rc){mosaic_ctx trusted={c->expected,items,&c->layout,nullptr};rc=mosaic_validate_job(&trusted,job);}
 mosaic_layer l;
 if(!rc)rc=mosaic_layer_geometry(layer,c->width,c->height,&l);
 if(!rc){
  auto &p=c->parameters[layer];
  if(!p.weights||p.weight_count!=size_t(l.co)*l.kpad||!p.coeff||p.coeff_count!=l.co*2||!isfinite(p.sa)||p.sa<=0||(layer!=15&&(!p.gamma||!p.beta)))rc=-EINVAL;
 }
 if(!rc)rc=op==2?mosaic_convolve_range(job,c,&l):mosaic_point_range(job,c,&l);
 if(rc)c->failed=rc;
 return rc;
}
#if !defined(__riscv)
int mosaic_run_graph(mosaic_device_context *c){
 if(!c)return -EINVAL;
 if(c->failed)return c->failed;
 if(c->phase_index)return -EBUSY;
 const uint32_t batch=c->batch_items?c->batch_items:MOSAIC_MAX_BATCH;
 if(batch>MOSAIC_MAX_BATCH){c->failed=-ERANGE;return c->failed;}
 mosaic_job_v1 base=c->expected;
 uint32_t sequence=base.sequence;
 for(uint32_t phase=0;phase<77;++phase){
  uint32_t layer,op,items;int rc=mosaic_phase_description(phase,c->width,c->height,&layer,&op,&items);if(rc){c->failed=rc;return rc;}
  for(uint32_t first=0;first<items;){
   if(sequence==UINT32_MAX){c->failed=-EOVERFLOW;return c->failed;}
   uint32_t count=items-first;if(count>batch)count=batch;
   mosaic_job_v1 job=base;job.magic=0x31434d56;job.version=1;job.bytes=128;job.sequence=sequence;job.layer=layer;job.op=op;job.tile_first=first;job.tile_count=count;job.core_mask=15;job.flags=c->diagnostics?1:0;memset(job.reserved,0,16);job.crc=mosaic_job_crc(&job);
   c->phase_index=phase;c->expected=job;++c->logical_jobs;
   for(uint32_t core=0;core<4;++core){c->core=core;rc=mosaic_execute_job(&job,c);if(rc)return rc;}
   first+=count;++sequence;
  }
  uint32_t next_layer=16,next_op=0,next_items=0;
  if(phase<76)mosaic_phase_description(phase+1,c->width,c->height,&next_layer,&next_op,&next_items);
  if(next_layer!=layer&&c->diagnostics&&c->on_stage){
   mosaic_layer l;mosaic_layer_geometry(layer,c->width,c->height,&l);
   rc=c->on_stage(c->callback_context,layer,c->memory.fp[layer%2],size_t(l.ci)*l.h*l.w,c->memory.fp[(layer+1)%2],size_t(l.co)*mosaic_oh(l)*mosaic_ow(l));
   if(rc){c->failed=rc;return rc;}
  }
 }
 c->phase_index=77;return 0;
}
#endif
