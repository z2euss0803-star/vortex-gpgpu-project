#include "norm_compat.h"
#include "norm_compat_lut_v2.h"
#include <string.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
static_assert((1280u*960u+511u)/512u<=4096u,"count reciprocal table bound");
// Independent IN candidate. Explicit FMA is local to this translation unit.
// V2 reproduces primitive mappings from model-independent scalar ISA samples.
static float count_reciprocal(unsigned count){
 const unsigned char *p=mosaic_norm_rcp_count_v2+(count-1)*4;
 uint32_t bits=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);
 float value;memcpy(&value,&bits,4);return value;
}
int mosaic_norm_rsqrt_compat_v2(float x,float *out){
 if(!out)return -EINVAL;
 uint32_t bits;memcpy(&bits,&x,4);unsigned exponent=(bits>>23)&255;
 if((bits>>31)||exponent==0||exponent==255)return -EDOM;
 int e=int(exponent)-127,half=e>=0?e/2:-((-e+1)/2);unsigned parity=unsigned(e-half*2);
 uint32_t normalized=(bits&0x7fffffu)|((127u+parity)<<23),index=normalized-0x3f800000u;
 size_t offset=size_t(index/8)*3;
 const unsigned char *p=mosaic_norm_rsqrt_delta_v2+offset;
 uint32_t word=uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16);
 int correction=int((word>>(3*(index%8)))&7)-3;
 if(correction==4)return -EDOM;
 float value;memcpy(&value,&normalized,4);volatile float root=sqrtf(value),base=1.f/root;
 uint32_t base_bits;float rounded=base;memcpy(&base_bits,&rounded,4);
 int64_t result=int64_t(base_bits)+correction-int64_t(half)*(INT64_C(1)<<23);
 if(result<=0||result>=0x7f800000)return -ERANGE;
 uint32_t result_bits=uint32_t(result);memcpy(out,&result_bits,4);return 0;
}
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
int mosaic_norm_stats_compat_v2(const float *x,size_t n,mosaic_norm_workspace *ws,float stats[3]){
 if(!x||!n||n>1280u*960u||!ws||!ws->hi||!ws->lo||!stats)return -EINVAL;
 size_t count=1;while(count<n)count*=2;
 if(ws->count<count||overlaps(ws->hi,count,ws->lo,count)||overlaps(ws->hi,count,x,n)||overlaps(ws->lo,count,x,n)||overlaps(stats,3,x,n)||overlaps(stats,3,ws->hi,count)||overlaps(stats,3,ws->lo,count))return -EINVAL;
 for(size_t i=0;i<n;i++)if(!isfinite(x[i]))return -EDOM;
 unsigned threads=n<=32?32:n<=128?128:512;
 unsigned active=n<threads?unsigned(n):threads;
 for(unsigned t=0;t<active;t++){
  float mean=0.f,m2=0.f,items=0.f;
  for(size_t i=t;i<n;i+=threads){
   items+=1.f;volatile float delta=x[i]-mean,reciprocal=count_reciprocal(unsigned(items));
   mean=fmaf(delta,reciprocal,mean);volatile float after=x[i]-mean;m2=fmaf(delta,after,m2);
   if(!isfinite(mean)||!isfinite(m2))return -ERANGE;
  }
  ws->hi[t]=mean;ws->lo[t]=m2;
 }
 volatile float reciprocal_n=1.f/float(n),sum=weighted_reduce(ws,n,threads,false,0.f),mean=sum*reciprocal_n;
 volatile float total=weighted_reduce(ws,n,threads,true,mean),variance=total*reciprocal_n;
 float adjusted=fmaf(total,reciprocal_n,1e-5f);
 float inv;int root_rc=mosaic_norm_rsqrt_compat_v2(adjusted,&inv);if(root_rc)return root_rc;
 if(!isfinite(mean)||!isfinite(variance)||variance<0||!isfinite(inv)||inv<=0)return -ERANGE;
 stats[0]=mean;stats[1]=variance;stats[2]=inv;return 0;
}
int mosaic_norm_apply_compat_v2(const float *x,size_t n,const float stats[3],float gamma,float beta,float *out){
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
