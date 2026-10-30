#pragma once
#include <stdint.h>
#include <math.h>
struct OptRange {uint32_t begin,end;bool valid;};
static inline bool opt_gn_count(uint32_t ci,uint32_t ih,uint32_t iw,uint32_t *out){
 if(!out||!ci||!ih||!iw||uint64_t(ci)*ih>UINT32_MAX)return false;
 uint64_t n=uint64_t(ci)*ih*iw;if(n>UINT32_MAX)return false;*out=uint32_t(n);return true;
}
static inline uint32_t opt_gn_workers(uint32_t n){return !n?0:n<4096?1:n<65536?16:64;}
static inline OptRange opt_gn_range(uint32_t n,uint32_t worker,uint32_t workers){
 if(!n||(workers!=1&&workers!=16&&workers!=64)||worker>=workers)return {0,0,false};
 return {uint32_t(uint64_t(n)*worker/workers),uint32_t(uint64_t(n)*(worker+1)/workers),true};
}
static inline void opt_gn_kahan(float value,float &sum,float &carry){float a=value-carry,b=sum+a;carry=(b-sum)-a;sum=b;}
static inline float opt_gn_partial(const float *x,OptRange r,float mean,bool variance){
 if(!x||!r.valid||r.end<r.begin)return NAN;
 float sum=0,carry=0;
 for(uint32_t i=r.begin;i<r.end;i++){float a=x[i];if(variance){a-=mean;a*=a;}opt_gn_kahan(a,sum,carry);}
 return sum;
}
static inline float opt_gn_merge(const float *p,uint32_t workers){
 if(!p||(workers!=1&&workers!=16&&workers!=64))return NAN;
 float sum=0,carry=0;for(uint32_t w=0;w<workers;w++)opt_gn_kahan(p[w],sum,carry);return sum;
}
static inline float opt_gn_invstd(float variance_sum,uint32_t n,float epsilon){return 1.0f/sqrtf(variance_sum/float(n)+epsilon);}
/* Fixed 4096B GPU scratch, explicit offsets independent of host ABI padding. */
struct OptGnScratch {
 uint8_t guard_left[64];float mean_partial[64],variance_partial[64];uint32_t counts[64];
 float mean,invstd;uint32_t error,workers,count,core_error[4];uint8_t scalar_reserved[28];
 uint64_t core_cycles[4][4];uint8_t reserved[3008],guard_right[64];
};
static_assert(sizeof(OptGnScratch)==4096,"GN scratch wire");
static_assert(__builtin_offsetof(OptGnScratch,mean_partial)==64&&__builtin_offsetof(OptGnScratch,variance_partial)==320&&__builtin_offsetof(OptGnScratch,counts)==576&&__builtin_offsetof(OptGnScratch,mean)==832&&__builtin_offsetof(OptGnScratch,core_cycles)==896&&__builtin_offsetof(OptGnScratch,reserved)==1024&&__builtin_offsetof(OptGnScratch,guard_right)==4032,"GN scratch offsets");
