#include "norm_compat.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
// Independent IN candidate. Explicit FMA is local to this translation unit.
// V1 uses ordinary FP32 reciprocal/root; it does not claim MUFU bit equivalence.
static bool overlaps(const float *a,size_t na,const float *b,size_t nb){
 uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 if(na>UINTPTR_MAX/4||nb>UINTPTR_MAX/4||x>UINTPTR_MAX-na*4||y>UINTPTR_MAX-nb*4)return true;
 return x<y+nb*4&&y<x+na*4;
}
static unsigned item_count(size_t n,unsigned threads,unsigned thread){
 return thread<n?unsigned((n-1-thread)/threads+1):0;
}
static float weighted_reduce(const mosaic_norm_workspace *ws,size_t n,unsigned threads,bool variance,float mean){
 float totals[32]={};
 for(unsigned warp=0;warp<threads/32;warp++){
  float lane[32]={};
  for(unsigned j=0;j<32;j++){
   unsigned thread=warp*32+j,count=item_count(n,threads,thread);
   if(count){
    if(variance){volatile float d=mean-ws->hi[thread],square=d*d;lane[j]=fmaf(square,float(count),ws->lo[thread]);}
    else lane[j]=ws->hi[thread]*float(count);
   }
  }
  for(unsigned offset=16;offset;offset/=2)for(unsigned j=0;j<32-offset;j++)lane[j]=lane[j]+lane[j+offset];
  totals[warp]=lane[0];
 }
 for(unsigned offset=16;offset;offset/=2)for(unsigned j=0;j<32-offset;j++)totals[j]=totals[j]+totals[j+offset];
 return totals[0];
}
int mosaic_norm_stats_compat_v1(const float *x,size_t n,mosaic_norm_workspace *ws,float stats[3]){
 if(!x||!n||n>1280u*960u||!ws||!ws->hi||!ws->lo||!stats)return -EINVAL;
 size_t count=1;while(count<n)count*=2;
 if(ws->count<count||overlaps(ws->hi,count,ws->lo,count)||overlaps(ws->hi,count,x,n)||overlaps(ws->lo,count,x,n)||overlaps(stats,3,x,n)||overlaps(stats,3,ws->hi,count)||overlaps(stats,3,ws->lo,count))return -EINVAL;
 for(size_t i=0;i<n;i++)if(!isfinite(x[i]))return -EDOM;
 unsigned threads=n<=32?32:n<=128?128:512;
 unsigned active=n<threads?unsigned(n):threads;
 for(unsigned t=0;t<active;t++){
  float mean=0.f,m2=0.f,items=0.f;
  for(size_t i=t;i<n;i+=threads){
   items+=1.f;volatile float delta=x[i]-mean,reciprocal=1.f/items;
   mean=fmaf(delta,reciprocal,mean);volatile float after=x[i]-mean;m2=fmaf(delta,after,m2);
   if(!isfinite(mean)||!isfinite(m2))return -ERANGE;
  }
  ws->hi[t]=mean;ws->lo[t]=m2;
 }
 volatile float reciprocal_n=1.f/float(n),sum=weighted_reduce(ws,n,threads,false,0.f),mean=sum*reciprocal_n;
 volatile float total=weighted_reduce(ws,n,threads,true,mean),variance=total*reciprocal_n;
 float adjusted=fmaf(total,reciprocal_n,1e-5f);
 volatile float root=sqrtf(adjusted),inv=1.f/root;
 if(!isfinite(mean)||!isfinite(variance)||variance<0||!isfinite(inv)||inv<=0)return -ERANGE;
 stats[0]=mean;stats[1]=variance;stats[2]=inv;return 0;
}
int mosaic_norm_apply_compat_v1(const float *x,size_t n,const float stats[3],float gamma,float beta,float *out){
 if(!x||!out||!stats||!n||n>1280u*960u||!isfinite(gamma)||!isfinite(beta))return -EINVAL;
 if(x!=out&&overlaps(x,n,out,n))return -EINVAL;
 float mean=stats[0],variance=stats[1],inv=stats[2];
 if(!isfinite(mean)||!isfinite(variance)||variance<0||!isfinite(inv)||inv<=0)return -EINVAL;
 for(size_t i=0;i<n;i++){
  if(!isfinite(x[i]))return -EDOM;
  volatile float centered=x[i]-mean,weighted=centered*gamma;
  float value=fmaf(weighted,inv,beta);if(!isfinite(value))return -ERANGE;out[i]=value;
 }
 return 0;
}
