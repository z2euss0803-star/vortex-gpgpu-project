#include "graph_internal.h"
#include "norm_compat.h"
// Optional V3 candidate: compile instead of point_ops.cpp; default target unchanged.
#include <string.h>
#include <math.h>
int mosaic_point_range(const mosaic_job_v1 *job,mosaic_device_context *c,const mosaic_layer *l){
 auto &m=c->memory;auto &p=c->parameters[job->layer];int rc=0;
 if(mosaic_leader(c)){
  float *input=m.fp[job->layer%2],*output=m.fp[(job->layer+1)%2];uint32_t spatial=mosaic_oh(*l)*mosaic_ow(*l);
  uint32_t elements=job->op==1?l->ci*l->h*l->w:l->co*spatial;
  for(uint32_t item=job->tile_first;item<job->tile_first+job->tile_count&&!rc;++item){
   if(item%4!=c->core)continue;
   if(job->op==3){rc=mosaic_norm_stats_compat_v3(output+size_t(item)*spatial,spatial,&m.norm[c->core],m.stats+item*3);continue;}
   uint32_t begin=item*256,end=begin+256;if(end>elements)end=elements;
   if(job->op==1){
    if(mosaic_saved(job->layer))memcpy(m.skip+begin,input+begin,(end-begin)*4);
    volatile float inverse=1.f/p.sa;rc=mosaic_quantize_input(input+begin,end-begin,inverse,m.quantized+begin);
    if(!rc)c->quantized_elements+=end-begin;
   }else if(job->op==4){
    while(begin<end&&!rc){uint32_t channel=begin/spatial,count=spatial-begin%spatial;if(count>end-begin)count=end-begin;rc=mosaic_norm_apply_compat_v3(output+begin,count,m.stats+channel*3,p.gamma[channel],p.beta[channel],spatial,output+begin);begin+=count;}
   }else if(job->op==5||job->op==6){
    for(uint32_t i=begin;i<end;++i){
     if(!isfinite(output[i])){rc=-EDOM;break;}
     volatile float value=job->op==5?(output[i]>0?output[i]:0.f):output[i]+m.skip[i];
     if(!isfinite(value)){rc=-ERANGE;break;}output[i]=value;
    }
   }else rc=-EINVAL;
  }
 }
 return mosaic_share(c,rc);
}
