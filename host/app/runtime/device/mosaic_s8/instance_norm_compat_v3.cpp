#include "norm_compat.h"
#include <errno.h>
#include <stdint.h>
#include <math.h>
// Independent candidate. Only the measured 4096-element single-read regime
// changes from V2. This is not a general replacement for cuDNN dispatch rules.
static bool overlaps(const float *a,size_t na,const float *b,size_t nb){
 uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 if(na>UINTPTR_MAX/4||nb>UINTPTR_MAX/4||x>UINTPTR_MAX-na*4||y>UINTPTR_MAX-nb*4)return true;
 return x<y+nb*4&&y<x+na*4;
}
static float reduce512(const float *values){
 float totals[32]={};
 for(unsigned warp=0;warp<16;warp++){
  float lane[32];for(unsigned j=0;j<32;j++)lane[j]=values[warp*32+j];
  for(unsigned offset=16;offset;offset/=2)for(unsigned j=0;j<32-offset;j++)lane[j]=lane[j]+lane[j+offset];
  totals[warp]=lane[0];
 }
 for(unsigned offset=16;offset;offset/=2)for(unsigned j=0;j<32-offset;j++)totals[j]=totals[j]+totals[j+offset];
 return totals[0];
}
int mosaic_norm_stats_compat_v3(const float *x,size_t n,mosaic_norm_workspace *ws,float stats[3]){
 if(n!=4096)return mosaic_norm_stats_compat_v2(x,n,ws,stats);
 if(!x||!ws||!ws->hi||!ws->lo||!stats||ws->count<n)return -EINVAL;
 if(overlaps(ws->hi,n,ws->lo,n)||overlaps(ws->hi,n,x,n)||overlaps(ws->lo,n,x,n)||overlaps(stats,3,x,n)||overlaps(stats,3,ws->hi,n)||overlaps(stats,3,ws->lo,n))return -EINVAL;
 for(size_t i=0;i<n;i++)if(!isfinite(x[i]))return -EDOM;
 for(unsigned t=0;t<512;t++){
  float sum=0.f;for(size_t i=t;i<n;i+=512)sum=sum+x[i];
  if(!isfinite(sum))return -ERANGE;
  ws->hi[t]=sum;
 }
 volatile float mean=reduce512(ws->hi)*(1.f/4096.f);
 for(unsigned t=0;t<512;t++){
  float sum=0.f;for(size_t i=t;i<n;i+=512){volatile float d=x[i]-mean;sum=fmaf(d,d,sum);}
  if(!isfinite(sum))return -ERANGE;
  ws->lo[t]=sum;
 }
 volatile float variance=reduce512(ws->lo)*(1.f/4096.f),adjusted=variance+1e-5f;
 float inv;int rc=mosaic_norm_rsqrt_compat_v2(adjusted,&inv);if(rc)return rc;
 if(!isfinite(mean)||!isfinite(variance)||variance<0||!isfinite(inv)||inv<=0)return -ERANGE;
 stats[0]=mean;stats[1]=variance;stats[2]=inv;return 0;
}
int mosaic_norm_apply_compat_v3(const float *x,size_t count,const float stats[3],float gamma,float beta,size_t channel_spatial,float *out){
 if(!channel_spatial||channel_spatial>1280u*960u||!count||count>channel_spatial)return -EINVAL;
 if(channel_spatial!=4096)return mosaic_norm_apply_compat_v2(x,count,stats,gamma,beta,out);
 if(!x||!out||!stats||!isfinite(gamma)||!isfinite(beta))return -EINVAL;
 if(x!=out&&overlaps(x,count,out,count))return -EINVAL;
 float mean=stats[0],variance=stats[1],inv=stats[2];
 if(!isfinite(mean)||!isfinite(variance)||variance<0||!isfinite(inv)||inv<=0)return -EINVAL;
 volatile float scale=gamma*inv,product=scale*mean,shift=beta-product;
 if(!isfinite(scale)||!isfinite(shift))return -ERANGE;
 for(size_t i=0;i<count;i++){
  if(!isfinite(x[i]))return -EDOM;
  float value=fmaf(scale,x[i],shift);if(!isfinite(value))return -ERANGE;out[i]=value;
 }
 return 0;
}
