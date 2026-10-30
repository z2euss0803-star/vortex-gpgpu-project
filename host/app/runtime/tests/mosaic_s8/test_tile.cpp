#include "fake_s8.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
static void test_signed48_all_pages(){
 uint32_t raw[96]={};int64_t want[64],out[64]={};
 for(unsigned n=0;n<64;++n){want[n]=(n%3==0)?-(INT64_C(1)<<47):(n%3==1)?(INT64_C(1)<<47)-1:-int64_t(n);uint64_t v=uint64_t(want[n])&0xffffffffffffULL;for(unsigned b=0;b<48;++b)if((v>>b)&1)raw[(n*48+b)/32]|=1u<<((n*48+b)%32);}
 assert(mosaic_unpack48(raw,out)==0);for(unsigned n=0;n<64;++n)assert(out[n]==want[n]);
 assert(mosaic_unpack48(nullptr,out)==-EINVAL);assert(mosaic_unpack48(raw,nullptr)==-EINVAL);
}
static void exact_k(uint32_t k){
 uint32_t seed=0x419872ab;std::vector<int16_t>a(k*8),b(k*8);int64_t want[64]={},out[64];
 for(uint32_t t=0;t<k*8;++t){seed=1664525*seed+1013904223;a[t]=int16_t(seed>>16);seed=1664525*seed+1013904223;b[t]=int16_t(seed>>16);}
 a[0]=-32768;b[0]=-32768; // Arithmetic boundary only, not production quantization.
 for(uint32_t t=0;t<k;++t)for(unsigned r=0;r<8;++r)for(unsigned c=0;c<8;++c)want[r*8+c]+=int64_t(a[t*8+r])*b[t*8+c];
 fake_s8 f;auto ops=f.ops();assert(mosaic_tile8(a.data(),b.data(),k,out,&ops)==0);
 for(unsigned n=0;n<64;++n)assert(out[n]==want[n]);
 assert(f.begins==1&&f.steps==k&&f.releases==1&&f.bitmap==0xffffff&&f.reads==29);
 printf("EXACT_K=%u seed=0x419872ab first=%lld last=%lld\n",k,(long long)out[0],(long long)out[63]);
 printf("HOST_RAW64 k=%u values=",k);for(auto v:out)printf("%lld,",(long long)v);puts("");
}
static void zero_tails_and_core_isolation(){
 int16_t a[24]={},b[24]={};int64_t out[64];
 for(unsigned t=0;t<3;++t){a[t*8]=int16_t(t+1);b[t*8]=int16_t(2*t+1);}
 for(unsigned core=0;core<4;++core){fake_s8 f;auto ops=f.ops();ops.core=core;assert(mosaic_tile8(a,b,3,out,&ops)==0);for(unsigned n=0;n<64;++n)assert(out[n]==(n==0?22:0));assert(f.releases==1);}
 std::vector<int16_t> extremes(4096*8,-32768);fake_s8 f;auto ops=f.ops();assert(mosaic_tile8(extremes.data(),extremes.data(),4096,out,&ops)==0);
 for(auto v:out)assert(v==INT64_C(4398046511104));
}
static void repeated_tiles_keep_independent_results(){
 int16_t a[8]={1},b[8]={2};int64_t out[64];fake_s8 f;auto ops=f.ops();
 assert(mosaic_tile8(a,b,1,out,&ops)==0);assert(out[0]==2);
 a[0]=-3;assert(mosaic_tile8(a,b,1,out,&ops)==0);assert(out[0]==-6);
 assert(f.begins==2&&f.releases==2&&f.steps==2);
}
static void faults(){
 int16_t a[24]={1},b[24]={2};int64_t out[64];
 for(unsigned which=0;which<9;++which){fake_s8 f;auto ops=f.ops();for(auto &v:out)v=0x1234567;
  if(which==0)f.fail_begin=1;
  if(which==1)f.bad_begin=true;
  if(which==2)f.fail_step=1;
  if(which==3)f.never_ready=true;
  if(which==4)f.bad_status=true;
  if(which==5)f.fail_page=23;
  if(which==6)f.fail_release=1;
  if(which==7)f.bad_idle=true;
  if(which==8){f.never_ready=true;ops.deadline_ticks=2;}
  assert(mosaic_tile8(a,b,3,out,&ops)<0);for(auto v:out)assert(v==0x1234567);
  assert(f.releases==(which==6||which==7?1u:0u));assert(f.polls<=ops.poll_limit+1);
 }
 fake_s8 f;auto ops=f.ops();assert(mosaic_tile8(a,b,0,out,&ops)==-EINVAL);assert(mosaic_tile8(a,b,4097,out,&ops)==-EINVAL);
 assert(f.begins==0);ops.warp=1;assert(mosaic_tile8(a,b,3,out,&ops)==-EINVAL);
}
int main(){test_signed48_all_pages();for(uint32_t k:{1u,2u,3u,2592u,4096u})exact_k(k);zero_tails_and_core_isolation();repeated_tiles_keep_independent_results();faults();puts("HOST_S8_PROTOCOL_MODEL_NOT_RTL_NOT_BOARD raw_gpu_cycles=null");}
