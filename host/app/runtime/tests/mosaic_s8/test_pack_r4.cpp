#include "graph.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <errno.h>
#include <limits>
#include <set>
#include <vector>

namespace {
void require(bool ok, const char *what) {
 if (!ok) { std::fprintf(stderr,"FAIL %s\n",what); std::exit(1); }
}

// R3 reference: general signed64 periodic reflection, deliberately independent
// of the production coordinate/loop implementation and never used by it.
uint32_t old_reflect(int64_t coordinate, uint32_t extent) {
 int64_t period=2*(int64_t(extent)-1);
 coordinate%=period;
 if(coordinate<0)coordinate+=period;
 return uint32_t(coordinate<int64_t(extent)?coordinate:period-coordinate);
}
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((noipa))
#endif
int oracle(const mosaic_layer *l, uint32_t n0, uint32_t k0,
           int16_t *out, size_t capacity) {
 if(!l||!out||!l->quantized||!l->ci||l->ci>128||!l->co||l->co>128||!l->h||l->h>960||!l->w||l->w>1280||(l->k!=3&&l->k!=9)||(l->stride!=1&&l->stride!=2)||(l->upsample!=1&&l->upsample!=2)||k0)return -EINVAL;
 uint32_t inner=l->ci*l->k*l->k,virtual_h=l->h*l->upsample,virtual_w=l->w*l->upsample;
 if(inner>4096||l->kpad!=(inner+31)/32*32||virtual_h<=l->k/2||virtual_w<=l->k/2)return -EINVAL;
 uint32_t oh=(virtual_h+l->stride-1)/l->stride,ow=(virtual_w+l->stride-1)/l->stride;
 if(oh>960||ow>1280||n0>=oh*ow||n0%8||capacity<size_t(l->kpad)*8||l->quantized_count!=size_t(l->ci)*l->h*l->w)return -ERANGE;
 for(uint32_t kk=0;kk<l->kpad;++kk) {
  for(unsigned n=0;n<8;++n) {
   int16_t value=0;uint32_t pos=n0+n;
   if(kk<inner&&pos<oh*ow) {
    uint32_t c=kk/(l->k*l->k),ky=(kk/l->k)%l->k,kx=kk%l->k;
    uint32_t iy=old_reflect(int64_t(pos/ow)*l->stride+ky-l->k/2,virtual_h)/l->upsample;
    uint32_t ix=old_reflect(int64_t(pos%ow)*l->stride+kx-l->k/2,virtual_w)/l->upsample;
    value=l->quantized[(size_t(c)*l->h+iy)*l->w+ix];
   }
   out[size_t(kk)*8+n]=value;
  }
 }
 return 0;
}

std::vector<int16_t> input_for(uint32_t ci,uint32_t h,uint32_t w) {
 std::vector<int16_t> input(size_t(ci)*h*w);
 for(size_t i=0;i<input.size();++i)
  input[i]=int16_t(int32_t((i*UINT64_C(4051)+i/7)%65536)-32768);
 if(input.size()>1) { input[0]=-32768;input[1]=32767; }
 return input;
}
mosaic_layer geometry(uint32_t ci,uint32_t h,uint32_t w,uint32_t k,
                      uint32_t stride,uint32_t up,const std::vector<int16_t>& input) {
 uint32_t inner=ci*k*k;
 return {ci,128,h,w,k,stride,up,(inner+31)/32*32,input.data(),input.size()};
}

uint64_t tiles_checked=0,elements_checked=0,geometries_checked=0;
void compare_tile(const mosaic_layer& l,uint32_t n0) {
 constexpr int16_t guard=-23456;
 size_t count=size_t(l.kpad)*8;
 std::vector<int16_t> expected(count,12345),actual(count+16,guard);
 require(oracle(&l,n0,0,expected.data(),count)==0,"reference accepts valid geometry");
 require(mosaic_pack_tile(&l,n0,0,actual.data()+8,count)==0,"candidate accepts valid geometry");
 if(std::memcmp(expected.data(),actual.data()+8,count*sizeof(int16_t))) {
  for(size_t i=0;i<count;++i)if(expected[i]!=actual[i+8]) {
   std::fprintf(stderr,"MISMATCH ci=%u h=%u w=%u k=%u stride=%u up=%u n0=%u kk=%zu lane=%zu expected=%d actual=%d\n",
     l.ci,l.h,l.w,l.k,l.stride,l.upsample,n0,i/8,i%8,expected[i],actual[i+8]);
   std::exit(1);
  }
 }
 for(size_t i=0;i<8;++i)require(actual[i]==guard&&actual[count+8+i]==guard,"tile output guards");
 ++tiles_checked;elements_checked+=count;
}
void compare_geometry(uint32_t ci,uint32_t h,uint32_t w,uint32_t k,uint32_t stride,uint32_t up,bool all) {
 auto input=input_for(ci,h,w);auto l=geometry(ci,h,w,k,stride,up,input);
 uint32_t vh=h*up,vw=w*up,oh=(vh+stride-1)/stride,ow=(vw+stride-1)/stride;
 require(vh>k/2&&vw>k/2&&oh<=960&&ow<=1280&&ci*k*k<=4096,"test geometry is valid");
 uint32_t spatial=oh*ow;
 std::set<uint32_t> starts;
 if(all)for(uint32_t n=0;n<spatial;n+=8)starts.insert(n);
 else {
  starts={0,(spatial-1)/8*8,(spatial/2)/8*8};
  for(uint32_t row: {0u,1u,oh/2,oh-1}) {
   uint32_t begin=row*ow,end=(row+1)*ow-1;
   for(uint32_t pos: {begin,end,begin+ow/2}) {
    starts.insert(pos/8*8);
    if(pos>=8)starts.insert((pos-8)/8*8);
    if(pos+8<spatial)starts.insert((pos+8)/8*8);
   }
  }
 }
 for(uint32_t n:starts)compare_tile(l,n);
 ++geometries_checked;
}
void literal_layout() {
 const std::vector<int16_t> input={10,20,30,40,50,60};
 auto l=geometry(1,2,3,3,1,1,input);
 std::vector<int16_t> out(32*8,-1);
 require(mosaic_pack_tile(&l,0,0,out.data(),out.size())==0,"minimum k3 literal accepted");
 const int16_t first_lane[]={50,40,50,20,10,20,50,40,50};
 for(size_t kk=0;kk<9;++kk)require(out[kk*8]==first_lane[kk],"literal reflect101 K-major first lane");
 for(size_t kk=0;kk<32;++kk)for(size_t n=0;n<8;++n)
  if(kk>=9||n>=6)require(out[kk*8+n]==0,"literal Kpad and spatial tails are zero");
 // Minimum k9 accepted extent is five. Both sides reach the full period:
 // offset -4 reflects to 4; bottom/right +4 reflects back to 0.
 auto nine_input=input_for(1,5,5);auto nine=geometry(1,5,5,9,1,1,nine_input);
 std::vector<int16_t> nine_out(96*8,-1);
 require(mosaic_pack_tile(&nine,0,0,nine_out.data(),nine_out.size())==0,"minimum k9 literal accepted");
 require(nine_out[0]==nine_input[24]&&nine_out[(4*9+4)*8]==nine_input[0],"minimum k9 negative edges");
 require(mosaic_pack_tile(&nine,24,0,nine_out.data(),nine_out.size())==0,"minimum k9 tail accepted");
 require(nine_out[(8*9+8)*8]==nine_input[0],"minimum k9 positive edge equals period");
}
void rejections() {
 auto input=input_for(1,8,9);auto base=geometry(1,8,9,3,1,1,input);
 std::vector<int16_t> out(4096*8+16,12345),before=out;
 unsigned rejected=0;
 auto check=[&](const mosaic_layer* l,uint32_t n0,uint32_t k0,size_t capacity,int expected,const char *name) {
  int ref=oracle(l,n0,k0,out.data(),capacity),got=mosaic_pack_tile(l,n0,k0,out.data(),capacity);
  if(ref!=expected||got!=expected) {
   std::fprintf(stderr,"REJECTION %s expected=%d oracle=%d actual=%d\n",name,expected,ref,got);std::exit(1);
  }
  require(out==before,"rejection leaves output unchanged");++rejected;
 };
 check(nullptr,0,0,out.size(),-EINVAL,"null layer");
 require(mosaic_pack_tile(&base,0,0,nullptr,out.size())==-EINVAL,"null output");++rejected;
 auto invalid=[&](mosaic_layer l,const char* name) { check(&l,0,0,out.size(),-EINVAL,name); };
 auto l=base;l.quantized=nullptr;invalid(l,"null input");
 for(uint32_t v: {0u,129u,UINT32_MAX}) {l=base;l.ci=v;invalid(l,"ci");l=base;l.co=v;invalid(l,"co");}
 for(uint32_t v: {0u,961u,UINT32_MAX}) {l=base;l.h=v;invalid(l,"height");}
 for(uint32_t v: {0u,1281u,UINT32_MAX}) {l=base;l.w=v;invalid(l,"width");}
 for(uint32_t v: {0u,1u,5u,UINT32_MAX}) {l=base;l.k=v;invalid(l,"kernel");}
 for(uint32_t v: {0u,3u,UINT32_MAX}) {l=base;l.stride=v;invalid(l,"stride");l=base;l.upsample=v;invalid(l,"upsample");}
 l=base;l.ci=51;l.k=9;l.kpad=4160;invalid(l,"inner beyond 4096");
 for(uint32_t v: {0u,9u,31u,64u,UINT32_MAX}) {l=base;l.kpad=v;invalid(l,"kpad");}
 l=base;l.h=1;invalid(l,"minimum k3 height");l=base;l.w=1;invalid(l,"minimum k3 width");
 l=base;l.k=9;l.kpad=96;l.h=4;invalid(l,"minimum k9 height");
 l=base;l.k=9;l.kpad=96;l.w=4;invalid(l,"minimum k9 width");
 check(&base,0,1,out.size(),-EINVAL,"nonzero k0");
 for(uint32_t v: {1u,7u,71u,72u,80u,UINT32_MAX})check(&base,v,0,out.size(),-ERANGE,"n0");
 check(&base,0,0,size_t(base.kpad)*8-1,-ERANGE,"capacity");
 l=base;l.quantized_count--;check(&l,0,0,out.size(),-ERANGE,"input too short");
 l=base;l.quantized_count++;check(&l,0,0,out.size(),-ERANGE,"input too long");
 l=base;l.h=481;l.upsample=2;check(&l,0,0,out.size(),-ERANGE,"output height limit");
 l=base;l.w=641;l.upsample=2;check(&l,0,0,out.size(),-ERANGE,"output width limit");
 std::printf("REJECTIONS_PASS cases=%u output_unchanged=1\n",rejected);
}
void correctness() {
 literal_layout();rejections();
 // Exhaust every tile for minimum, odd, row crossing, and short final tiles.
 for(uint32_t k:{3u,9u})for(uint32_t up:{1u,2u})for(uint32_t stride:{1u,2u})
  for(uint32_t h:{1u,2u,3u,4u,5u,7u,8u,9u,13u,17u})
   for(uint32_t w:{1u,2u,3u,4u,5u,7u,8u,9u,13u,17u})
    if(h*up>k/2&&w*up>k/2)
     for(uint32_t ci:{1u,2u,3u})compare_geometry(ci,h,w,k,stride,up,true);
 // ci=32 has no Kpad tail for either kernel; max accepted K9 is ci=50.
 for(uint32_t k:{3u,9u})for(uint32_t ci:{31u,32u,k==3?128u:50u})
  for(uint32_t stride:{1u,2u})for(uint32_t up:{1u,2u})
   compare_geometry(ci,19,23,k,stride,up,false);
 // Maximum source and virtual extents; samples cover all four edges and center.
 for(uint32_t k:{3u,9u}) {
  uint32_t ci=k==3?128:50;
  compare_geometry(ci,960,1280,k,1,1,false);
  compare_geometry(ci,960,1280,k,2,1,false);
  compare_geometry(ci,960,1280,k,2,2,false);
  compare_geometry(3,479,639,k,1,2,false);
  compare_geometry(3,479,639,k,2,2,false);
 }
 std::printf("MOSAIC_PACK_R4_EXACT_PASS_HOST_NOT_GPU geometries=%llu tiles=%llu elements=%llu int16_bytes=%llu\n",
  (unsigned long long)geometries_checked,(unsigned long long)tiles_checked,
  (unsigned long long)elements_checked,(unsigned long long)(elements_checked*2));
}
using pack_fn=int(*)(const mosaic_layer*,uint32_t,uint32_t,int16_t*,size_t);
volatile uint64_t sink=0;
uint64_t timed(pack_fn fn,const mosaic_layer& l,const std::vector<uint32_t>& tiles,unsigned repeats) {
 std::vector<int16_t> out(size_t(l.kpad)*8);
 auto begin=std::chrono::steady_clock::now();
 uint64_t checksum=0;
 for(unsigned i=0;i<repeats;++i)for(uint32_t n0:tiles) {
  require(fn(&l,n0,0,out.data(),out.size())==0,"benchmark pack accepted");
  checksum+=uint16_t(out[(i*13)%out.size()]);
 }
 auto end=std::chrono::steady_clock::now();sink+=checksum;
 return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count());
}
bool benchmark(double minimum_speedup) {
 struct Case {const char *name;uint32_t ci,h,w,k,stride,up;};
 const Case cases[]={
  {"frozen_input_k9",3,960,1280,9,1,1},
  {"deep_k3",128,240,320,3,1,1},
  {"upsample_k3",128,240,320,3,1,2}
 };
 bool pass=true;
 for(const auto& c:cases) {
  auto input=input_for(c.ci,c.h,c.w);auto l=geometry(c.ci,c.h,c.w,c.k,c.stride,c.up,input);
  uint32_t oh=(c.h*c.up+c.stride-1)/c.stride,ow=(c.w*c.up+c.stride-1)/c.stride;
  std::vector<uint32_t> tiles={0,((oh/2*ow+ow/2)/8)*8,((oh*ow-1)/8)*8};
  for(uint32_t n:tiles)compare_tile(l,n);
  timed(oracle,l,tiles,8);timed(mosaic_pack_tile,l,tiles,8);
  std::vector<uint64_t> old_times,new_times;
  for(unsigned sample=0;sample<7;++sample) {
   if(sample%2) {new_times.push_back(timed(mosaic_pack_tile,l,tiles,512));old_times.push_back(timed(oracle,l,tiles,512));}
   else {old_times.push_back(timed(oracle,l,tiles,512));new_times.push_back(timed(mosaic_pack_tile,l,tiles,512));}
  }
  std::printf("HOST_TIMING_RAW case=%s old_ns=",c.name);
  for(auto n:old_times)std::printf("%llu,",(unsigned long long)n);
  std::printf(" candidate_ns=");for(auto n:new_times)std::printf("%llu,",(unsigned long long)n);std::puts("");
  std::sort(old_times.begin(),old_times.end());std::sort(new_times.begin(),new_times.end());
  double speedup=double(old_times[3])/double(new_times[3]);
  std::printf("HOST_TIMING case=%s samples=7 tiles_per_sample=1536 old_median_ns=%llu candidate_median_ns=%llu speedup=%.4f minimum=%.4f\n",
   c.name,(unsigned long long)old_times[3],(unsigned long long)new_times[3],speedup,minimum_speedup);
  pass&=speedup>=minimum_speedup;
 }
 std::printf("MOSAIC_PACK_R4_HOST_COST_%s_NOT_GPU checksum=%llu\n",pass?"PASS":"FAIL",(unsigned long long)sink);
 return pass;
}
}
int main(int argc,char **argv) {
 if(argc==3&&std::strcmp(argv[1],"--benchmark")==0) {
  char *end=nullptr;double minimum=std::strtod(argv[2],&end);
  require(end&&*end=='\0'&&std::isfinite(minimum)&&minimum>0,"positive benchmark speedup");
  return benchmark(minimum)?0:1;
 }
 require(argc==1,"usage: test_pack_r4 [--benchmark minimum_speedup]");
 correctness();return 0;
}
