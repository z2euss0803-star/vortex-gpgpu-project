#pragma once
#include "hunet_opt_gn_runtime.h"
struct OptGnNormRun {OptGnRun gn;const float *gamma,*beta;float *output;uint32_t hw;};
static inline float opt_gn_affine(float x,float mean,float invstd,float gamma,float beta){return ((x-mean)*invstd)*gamma+beta;}
static inline void opt_gn_norm_worker(OptGnNormRun *r,uint32_t worker){
 if(worker>=r->gn.workers)return;
 auto range=opt_gn_range(r->gn.n,worker,r->gn.workers);uint32_t error=0;
 for(uint32_t i=range.begin;i<range.end;i++){
  float value=opt_gn_affine(r->gn.x[i],r->gn.scratch->mean,r->gn.scratch->invstd,r->gamma[i/r->hw],r->beta[i/r->hw]);
  if(!isfinite(value))error|=2;else r->output[i]=value;
 }
 r->gn.scratch->counts[worker]=error;
}
template<class Port> static int opt_gn_norm_phases(HunetJobV1 job,const float*x,float*out,float*stats,const float*gamma,const float*beta,OptGnScratch*s,Port&p){
 uint32_t n;if(!x||!out||x==out||!gamma||!beta||!s||!stats||!opt_gn_count(job.ci,job.ih,job.iw,&n))return 1;
 int rc=opt_gn_phases(job,x,stats,s,p);if(rc)return rc;
 OptGnNormRun run={{job,x,stats,s,n,opt_gn_workers(n)},gamma,beta,out,job.ih*job.iw};
 s->core_error[p.core()]=p.spawn_norm(&run)?4:0;
 opt_gn_join(p);
 if(p.core()==0){for(unsigned i=0;i<4;i++)s->error|=s->core_error[i];for(uint32_t i=0;i<run.gn.workers;i++)s->error|=s->counts[i];s->error|=opt_gn_guard(s);}
 opt_gn_join(p);return int(s->error);
}
