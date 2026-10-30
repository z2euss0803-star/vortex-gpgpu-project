#include "device/hunet_fused_ops.h"
#include <cassert>
#include <cstdio>
#include <vector>
#include <algorithm>
#include <cmath>

static float reference(const HunetFusedStageV1&s,const HunetFusedDescriptorV1&d,
                       const float*x,unsigned r,unsigned c){
 if(r>=d.k || c>=d.count)return 0;
 unsigned pos=d.offset+c;
 if(s.flags==2){int ix=int(pos)+int(r)-int(s.pad);return ix>=0&&ix<int(s.iw)?x[ix]:0;}
 unsigned channel=r/(s.kernel_k*s.kernel_k),ky=(r/s.kernel_k)%s.kernel_k,kx=r%s.kernel_k;
 int y=int(pos/s.ow*s.stride+ky)-int(s.pad),xx=int(pos%s.ow*s.stride+kx)-int(s.pad);
 return y>=0&&xx>=0&&y<int(s.ih)&&xx<int(s.iw)?x[(channel*s.ih+y)*s.iw+xx]:0;
}
static void test_pack_matches_frozen_half(){
 const uint32_t bits[]={0,0x80000000,0x3f800000,0x3f801000,0x3f803000,
   0x33000000,0x33800000,0x33c00000,0x387fc000,0x38800000,0x477fe000,0x477ff000};
 const uint16_t half[]={0,0x8000,0x3c00,0x3c00,0x3c02,0,1,2,0x3ff,0x400,0x7bff,0x7c00};
 for(unsigned i=0;i<sizeof(bits)/4;++i)assert(hunet_half(hunet_float(bits[i]))==half[i]);
 for(unsigned flags: {0u,2u})for(unsigned k: {1u,3u})for(unsigned stride:{1u,2u})for(unsigned pad:{0u,1u}){
  HunetFusedStageV1 s{};s.ci=2;s.ih=5;s.iw=7;s.oh=3;s.ow=4;s.kernel_k=k;s.stride=stride;s.pad=pad;s.flags=flags;
  HunetFusedDescriptorV1 d{};d.k=flags==2?k:2*k*k;d.kp=(d.k+7)/8*8;d.np=8;d.count=5;d.offset=2;
  std::vector<float>x(70);for(unsigned i=0;i<x.size();++i)x[i]=hunet_float(bits[i%12]);
  std::vector<uint16_t> y(d.kp*d.np,0xabcd);
  for(unsigned r=0;r<d.kp;++r)for(unsigned lane=0;lane<4;++lane)
    hunet_fused_pack_row(s,d,x.data(),y.data(),r,lane,4);
  for(unsigned r=0;r<d.kp;++r)for(unsigned c=0;c<d.np;++c)
    assert(y[r*d.np+c]==hunet_half(reference(s,d,x.data(),r,c)));
 }
 // Exercise the exact 1x1 direct path as well.
 HunetFusedStageV1 s{};s.ih=s.oh=2;s.iw=s.ow=3;s.kernel_k=s.stride=1;
 HunetFusedDescriptorV1 d{};d.k=2;d.kp=8;d.np=8;d.count=6;
 float x[12];for(unsigned i=0;i<12;++i)x[i]=float(i)-6;
 uint16_t y[64]{};for(unsigned r=0;r<8;++r)hunet_fused_pack_row(s,d,x,y,r,0,1);
 for(unsigned r=0;r<8;++r)for(unsigned c=0;c<8;++c)
  assert(y[r*8+c]==hunet_half(reference(s,d,x,r,c)));
}
static void test_epilogue_lane_mapping(){
 for(unsigned rows:{1u,7u,8u,9u})for(unsigned count:{1u,3u,4u,5u})for(bool bias:{false,true}){
  HunetFusedDescriptorV1 d{};d.m=rows;d.n=11;d.offset=3;d.count=count;
  std::vector<float> out(rows*11,-999),b(rows,1.0f);std::vector<unsigned> seen(rows*11);
  for(unsigned tr=0;tr<rows;tr+=8)for(unsigned tc=0;tc<count;tc+=4)
   for(unsigned lane=0;lane<4;++lane)for(unsigned reg=0;reg<8;++reg){
    unsigned r=hunet_fused_acc_row(tr,lane,reg),c=hunet_fused_acc_col(tc,lane,reg);
    float acc=16777216.f; // RNE after complete accumulation: (acc+bias) differs from a bias-seeded sum.
    hunet_fused_store(d,out.data(),bias?b.data():nullptr,tr,tc,lane,reg,acc);
    if(r<rows&&c<count){++seen[r*11+3+c];assert(out[r*11+3+c]==acc);}
   }
  for(unsigned r=0;r<rows;++r)for(unsigned c=0;c<11;++c){bool valid=c>=3&&c<3+count;
    assert(seen[r*11+c]==unsigned(valid));if(!valid)assert(out[r*11+c]==-999);}
 }
 HunetFusedDescriptorV1 d{};d.m=8;d.n=d.count=4;
 float out[32]{},b[8];std::fill(b,b+8,.25f);
 hunet_fused_store(d,out,b,0,0,0,0,2.f);assert(out[0]==2.25f);
 // No-bias path must copy -0 instead of adding +0.
 hunet_fused_store(d,out,nullptr,0,0,0,0,-0.f);assert(hunet_bits(out[0])==0x80000000);
}
int main(){test_pack_matches_frozen_half();test_epilogue_lane_mapping();
 puts("FUSED_PACK_AND_EPILOGUE_HOST_INDEX_ROUNDING_PASS_NOT_GPU");}
