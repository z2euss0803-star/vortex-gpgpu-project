#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>
#include "../../device/hunet_opt_pack.h"
template<class T> static std::vector<T> read(const char *p){std::ifstream f(p,std::ios::binary|std::ios::ate);assert(f);auto n=f.tellg();assert(n>0&&n%sizeof(T)==0);std::vector<T> v(size_t(n)/sizeof(T));f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);assert(f);return v;}
/* Independent frozen scalar indexing, no new row cursor or lookup used. */
static uint16_t golden(const HunetFusedStageV1&s,const HunetFusedDescriptorV1&d,const float*x,uint32_t kk,uint32_t nn){
 float v=0;if(kk<d.k&&nn<d.count){uint32_t pos=d.offset+nn;
  if(s.flags==2){int ix=int(pos)+int(kk)-int(s.pad);if(ix>=0&&ix<int(s.iw))v=x[ix];}
  else {uint32_t ic=kk/(s.kernel_k*s.kernel_k),ky=kk/s.kernel_k%s.kernel_k,kx=kk%s.kernel_k;
   int iy=int(pos/s.ow*s.stride+ky)-int(s.pad),ix=int(pos%s.ow*s.stride+kx)-int(s.pad);
   if(iy>=0&&ix>=0&&iy<int(s.ih)&&ix<int(s.iw))v=x[ic*s.ih*s.iw+uint32_t(iy)*s.iw+uint32_t(ix)];}
 }return hunet_half(v);
}
int main(){
 auto jobs=read<HunetFusedStageV1>("generated/hunet_fused/jobs.bin");auto ds=read<HunetFusedDescriptorV1>("generated/hunet_fused/descriptors.bin");
 unsigned shapes=0,descriptors=0;uint64_t values=0;
 const float special[]={0.0f,-0.0f,65504.0f,-65504.0f,1.00048828125f,1.00146484375f,0.000000059604644775390625f,-0.0000000298023223876953125f};
 for(const auto&s:jobs){if(s.op!=15)continue;shapes++;
  const auto *table=ds.data()+(s.stage_descriptors_gpu-0x86d0df80)/64;assert(opt_descriptor_table_valid(table,s.descriptor_count));
  std::vector<float>x(uint64_t(s.ci)*s.ih*s.iw);for(size_t i=0;i<x.size();i++)x[i]=i%13<8?special[i%13]:float(int(i%997)-498)/31;
  for(unsigned i=0;i<s.descriptor_count;i++){const auto&d=table[i];descriptors++;
   std::vector<uint16_t> old(uint64_t(d.kp)*d.np),now(old.size());
   for(uint32_t row=0;row<d.kp;row++)for(uint32_t lane=0;lane<4;lane++){hunet_fused_pack_row(s,d,x.data(),old.data(),row,lane,4);opt_pack_row(s,d,x.data(),now.data(),row,lane,4);}
   assert(old==now);
   for(uint32_t row=0;row<d.kp;row++)for(uint32_t col=0;col<d.np;col++){
    auto ref=golden(s,d,x.data(),row,col);assert(now[uint64_t(row)*d.np+col]==ref);
    if(col==0||col==d.count-1||col==d.np-1)assert(opt_pack_element(s,d,x.data(),row,col)==ref);
   }values+=old.size();
  }
 }
 assert(shapes==35&&descriptors==314);
 HunetFusedStageV1 s={};s.ci=2;s.ih=5;s.iw=7;s.oh=3;s.ow=4;s.kernel_k=3;s.stride=2;s.pad=1;
 HunetFusedDescriptorV1 d={};d.k=18;d.kp=24;d.n=12;d.count=11;d.np=12;d.offset=1;
 std::vector<float>x(70);for(unsigned i=0;i<70;i++)x[i]=special[i%8];std::vector<uint16_t> y(24*12);
 for(unsigned row=0;row<24;row++)for(unsigned lane=0;lane<4;lane++)opt_pack_row(s,d,x.data(),y.data(),row,lane,4);
 for(unsigned row=0;row<24;row++)for(unsigned col=0;col<12;col++)assert(y[row*12+col]==golden(s,d,x.data(),row,col));
 std::printf("PACK_EXACT_ALL_%u_SHAPES_%u_DESCRIPTORS_%llu_VALUES_PLUS_SYNTHETIC_STRIDE2_PAD_PASS_NOT_GPU\n",shapes,descriptors,(unsigned long long)values);
}
