#include "graph.h"
#if defined(MOSAIC_BASELINE_WITNESS)
#define stats_impl mosaic_norm_stats
#define apply_impl mosaic_norm_apply
#elif defined(MOSAIC_TEST_COMPAT_V2)
#include "norm_compat.h"
#define stats_impl mosaic_norm_stats_compat_v2
#define apply_impl mosaic_norm_apply_compat_v2
#else
#include "norm_compat.h"
#define stats_impl mosaic_norm_stats_compat_v1
#define apply_impl mosaic_norm_apply_compat_v1
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <vector>
static uint32_t bits(float x){uint32_t y;memcpy(&y,&x,4);return y;}
int main(){
 // A real rounding witness: split mul/add gives zero; one FMA gives -2^-46.
 float x=0x1.000002p0f,out=123.f,stats[3]={0.f,1.f,0x1.fffffcp-1f};
 assert(!apply_impl(&x,1,stats,1.f,-1.f,&out));
 assert(bits(out)==bits(-0x1p-46f));
 float in[]={1,2,3,4},hi[4],lo[4],s[3];mosaic_norm_workspace ws={hi,lo,4};
 assert(!stats_impl(in,4,&ws,s));assert(s[0]==2.5f&&s[1]==1.25f);
 float big[]={100000000.f,1.f,-100000000.f,1.f};
 assert(!stats_impl(big,4,&ws,s));assert(s[0]==.5f);
 float constant[]={7,7,7},values[3];assert(!stats_impl(constant,3,&ws,s));
 assert(!apply_impl(constant,3,s,2,3,values));for(float v:values)assert(v==3);
 for(size_t n:{size_t(31),size_t(32),size_t(33),size_t(127),size_t(128),size_t(129),size_t(511),size_t(512),size_t(513),size_t(769)}){
  size_t count=1;while(count<n)count*=2;
  std::vector<float> a(n),h(count),l(count);for(size_t i=0;i<n;i++)a[i]=float(int(i%17)-8)*.125f;
  mosaic_norm_workspace w={h.data(),l.data(),count};assert(!stats_impl(a.data(),n,&w,s));
  double mean=0,var=0;for(float v:a)mean+=v;mean/=n;for(float v:a)var+=(v-mean)*(v-mean);var/=n;
  assert(fabs(double(s[0])-mean)<1e-5&&fabs(double(s[1])-var)<1e-5);
 }
 float bad[]={1,NAN,2};assert(stats_impl(bad,3,&ws,s)<0);
 assert(stats_impl(in,0,&ws,s)<0);assert(stats_impl(in,5,&ws,s)<0);
 mosaic_norm_workspace alias={hi,hi,4};assert(stats_impl(in,4,&alias,s)<0);
 mosaic_norm_workspace source_alias={in,lo,4};assert(stats_impl(in,4,&source_alias,s)<0);
 s[0]=0;s[1]=1;s[2]=NAN;assert(apply_impl(&x,1,s,1,0,&out)<0);
 puts("INSTANCE_NORM_COMPAT_UNIT_PASS_NOT_NETWORK_NOT_GPU raw_gpu_cycles=null");
}
