#pragma once
#include "hunet_opt_gn.h"
#include "../vendor/hunet/v5/include/hunet_job.h"
struct OptGnRun {HunetJobV1 job;const float *x;float *out;OptGnScratch *scratch;uint32_t n,workers;};
static inline uint32_t opt_gn_guard(const OptGnScratch *s){
 for(unsigned i=0;i<64;i++)if(s->guard_left[i]!=0xa5||s->guard_right[i]!=0xa5)return 1;
 for(unsigned i=0;i<sizeof(s->reserved);i++)if(s->reserved[i])return 1;
 for(unsigned i=0;i<sizeof(s->scalar_reserved);i++)if(s->scalar_reserved[i])return 1;
 return 0;
}
static inline void opt_gn_worker(OptGnRun *r,uint32_t worker,bool variance){
 if(worker>=r->workers)return;
 OptRange range=opt_gn_range(r->n,worker,r->workers);
 float value=opt_gn_partial(r->x,range,r->scratch->mean,variance);
 if(variance)r->scratch->variance_partial[worker]=value;
 else {r->scratch->mean_partial[worker]=value;r->scratch->counts[worker]=range.end-range.begin;}
}
template<class Port> static inline void opt_gn_join(Port &p){p.fence();p.barrier(0x80000000,4);}
template<class Port> static int opt_gn_phases(HunetJobV1 job,const float *x,float *out,OptGnScratch *s,Port &p){
 uint32_t n,core=p.core();if(core>=4||!x||!out||!s||!opt_gn_count(job.ci,job.ih,job.iw,&n))return 1;
 OptGnRun r={job,x,out,s,n,opt_gn_workers(n)};
 if(core==0){
  s->error=opt_gn_guard(s);s->workers=r.workers;s->count=n;s->mean=s->invstd=0;
  for(unsigned i=0;i<64;i++)s->counts[i]=0,s->mean_partial[i]=s->variance_partial[i]=0;
 }
 s->core_error[core]=0;
 opt_gn_join(p);
 uint64_t begin=p.cycles();
 if(!s->error)s->core_error[core]=p.spawn(&r,false)?4:0;
 opt_gn_join(p);
 s->core_cycles[core][0]=p.cycles()-begin;begin=p.cycles();
 if(core==0&&!s->error){
  uint64_t count=0;for(unsigned i=0;i<4;i++)s->error|=s->core_error[i];
  for(unsigned i=0;i<r.workers;i++)count+=s->counts[i];
  if(count!=n)s->error|=4;
  s->mean=opt_gn_merge(s->mean_partial,r.workers)/float(n);if(!isfinite(s->mean))s->error|=2;
 }
 opt_gn_join(p);
 s->core_cycles[core][1]=p.cycles()-begin;begin=p.cycles();
 if(!s->error)s->core_error[core]=p.spawn(&r,true)?4:0;
 opt_gn_join(p);
 s->core_cycles[core][2]=p.cycles()-begin;begin=p.cycles();
 if(core==0&&!s->error){
  for(unsigned i=0;i<4;i++)s->error|=s->core_error[i];
  float epsilon;__builtin_memcpy(&epsilon,&job.epsilon_bits,4);
  s->invstd=opt_gn_invstd(opt_gn_merge(s->variance_partial,r.workers),n,epsilon);
  if(!isfinite(s->invstd))s->error|=2;
  s->error|=opt_gn_guard(s);
 }
 opt_gn_join(p);
 s->core_cycles[core][3]=p.cycles()-begin;
 if(core==0&&!s->error){out[0]=s->mean;out[1]=s->invstd;}
 opt_gn_join(p);
 return int(s->error);
}
