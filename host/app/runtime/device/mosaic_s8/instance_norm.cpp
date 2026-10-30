#include "graph.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
static void two_sum(float a,float b,float &hi,float &lo){
 volatile float s=a+b,bp=s-a,t=s-bp,u=a-t,v=b-bp,e=u+v;
 hi=s;lo=e;
}
static int tree_sum(mosaic_norm_workspace *ws,size_t count,float *out){
 for(size_t span=count;span>1;span/=2){
  for(size_t i=0;i<span/2;++i){
   float h,e,hh,ll;two_sum(ws->hi[i*2],ws->hi[i*2+1],h,e);
   volatile float lsum=ws->lo[i*2]+ws->lo[i*2+1],l=lsum+e;
   two_sum(h,l,hh,ll);ws->hi[i]=hh;ws->lo[i]=ll;
  }
 }
 volatile float total=ws->hi[0]+ws->lo[0];if(!isfinite(total))return -ERANGE;*out=total;return 0;
}
static bool overlap(const float *a,size_t na,const float *b,size_t nb){
 uintptr_t x=reinterpret_cast<uintptr_t>(a),y=reinterpret_cast<uintptr_t>(b);
 if(na>UINTPTR_MAX/sizeof(float)||nb>UINTPTR_MAX/sizeof(float)||x>UINTPTR_MAX-na*sizeof(float)||y>UINTPTR_MAX-nb*sizeof(float))return true;
 return x<y+nb*sizeof(float)&&y<x+na*sizeof(float);
}
int mosaic_norm_stats(const float *x,size_t n,mosaic_norm_workspace *ws,float stats[3]){
 if(!x||!n||n>1280u*960u||!ws||!ws->hi||!ws->lo||!stats)return -EINVAL;
 size_t count=1;while(count<n)count*=2;
 if(ws->count<count||overlap(ws->hi,count,ws->lo,count)||overlap(ws->hi,count,x,n)||overlap(ws->lo,count,x,n)||overlap(stats,3,x,n)||overlap(stats,3,ws->hi,count)||overlap(stats,3,ws->lo,count))return -EINVAL;
 for(size_t i=0;i<n;++i)if(!isfinite(x[i]))return -EDOM;
 for(size_t i=0;i<count;++i){ws->hi[i]=i<n?x[i]:0.f;ws->lo[i]=0.f;}
 float sum;int rc=tree_sum(ws,count,&sum);if(rc)return rc;
 volatile float mean=sum/float(n);
 for(size_t i=0;i<count;++i){
  volatile float centered=i<n?x[i]-mean:0.f,square=centered*centered;
  if(!isfinite(square))return -ERANGE;
  ws->hi[i]=square;ws->lo[i]=0.f;
 }
 rc=tree_sum(ws,count,&sum);if(rc)return rc;
 volatile float variance=sum/float(n),adjusted=variance+1e-5f,root=sqrtf(adjusted),inv=1.f/root;
 if(!isfinite(mean)||!isfinite(variance)||variance<0||!isfinite(inv)||inv<=0)return -ERANGE;
 stats[0]=mean;stats[1]=variance;stats[2]=inv;return 0;
}
int mosaic_norm_apply(const float *x,size_t n,const float stats[3],float gamma,float beta,float *out){
 if(!x||!out||!stats||!n||n>1280u*960u||!isfinite(gamma)||!isfinite(beta))return -EINVAL;
 float mean=stats[0],variance=stats[1],inv=stats[2];
 if(!isfinite(mean)||!isfinite(variance)||variance<0||!isfinite(inv)||inv<=0)return -EINVAL;
 for(size_t i=0;i<n;++i){
  if(!isfinite(x[i]))return -EDOM;
  volatile float centered=x[i]-mean,normalized=centered*inv,weighted=normalized*gamma,value=weighted+beta;
  if(!isfinite(value))return -ERANGE;
  out[i]=value;
 }
 return 0;
}
