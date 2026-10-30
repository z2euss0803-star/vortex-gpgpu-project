#include "device/mosaic_s8/tile.h"
#include "tests/mosaic_s8/fake_s8.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
static void ck(bool ok,const char*s){if(!ok){std::fprintf(stderr,"NATIVE_BULK_ASSERT %s\n",s);std::exit(78);}}
struct Bulk:fake_s8 {unsigned bulk_calls=0;};
static int bulk_steps(void*opaque,const int16_t*a,const int16_t*b,uint32_t k){
 auto&sim=*static_cast<Bulk*>(opaque);++sim.bulk_calls;
 for(uint32_t t=0;t<k;t++){mosaic_words4 av{},bv{};for(unsigned lane=0;lane<4;lane++){av.word[lane]=mosaic_lane_word(a+t*8,lane);bv.word[lane]=mosaic_lane_word(b+t*8,lane);}auto single=sim.ops();int rc=single.step(&sim,&av,&bv);if(rc)return rc;}return 0;
}
int main(){
 const int16_t edge[8]={-32768,32767,-1,0,1,12345,-23456,32766};
 for(unsigned k:{1u,2u,31u,243u,1152u,4096u}){
  std::vector<int16_t>a(k*8+8),b(k*8+8);for(size_t i=0;i<k*8;i++){a[i]=edge[i%8];b[i]=int16_t(int((i*1877+11939)%65536)-32768);}
  for(unsigned lane=0;lane<4;lane++){uint32_t expected=uint16_t(a[lane*2])|(uint32_t(uint16_t(a[lane*2+1]))<<16);ck(mosaic_lane_word(a.data(),lane)==expected,"lane exact two INT16 bits");}
  fake_s8 scalar;auto normal=scalar.ops();int64_t expected[64],actual[64];ck(!mosaic_tile8(a.data(),b.data(),k,expected,&normal),"original scalar callback tile");Bulk sim;auto fast=sim.ops();fast.context=&sim;fast.steps=bulk_steps;ck(!mosaic_tile8(a.data(),b.data(),k,actual,&fast),"bulk tile");ck(!std::memcmp(expected,actual,sizeof(actual)),"all64 signed48 decoded sums unchanged");ck(sim.bulk_calls==1&&sim.steps==k&&sim.begins==1&&sim.releases==1&&sim.reads==scalar.reads,"one bulk call, same ordered steps and control/drain");
 }
 std::vector<int16_t>a(256),b(256);Bulk bad;bad.fail_step=3;auto io=bad.ops();io.context=&bad;io.steps=bulk_steps;int64_t out[64];ck(mosaic_tile8(a.data(),b.data(),31,out,&io)==-EIO&&bad.bulk_calls==1&&bad.accepted==3&&bad.releases==0,"first step error no retry or release");
 Bulk align;auto unaligned=align.ops();unaligned.context=&align;unaligned.steps=bulk_steps;ck(mosaic_tile8(a.data()+1,b.data(),2,out,&unaligned)==-EINVAL&&align.begins==0,"unaligned bulk rejected before BEGIN");
 puts("MOSAIC_NATIVE_BULK_VS_ORIGINAL_INT16_I48_ALL64_KTAILS_HANDSHAKE_ERROR_NO_RETRY_HOST_PASS_NOT_GPU");return 0;
}
