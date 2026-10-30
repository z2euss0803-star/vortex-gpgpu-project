#include "device/vxnn/mosaic_ops.h"
#include "tests/mosaic_s8/fake_s8.h"
#include "device/mosaic_s8/norm_compat.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cfenv>
#include <climits>
static void check(bool value,const char*why){if(!value){std::fprintf(stderr,"U13_MOSAIC_PRIMITIVE_ASSERT %s\n",why);std::exit(75);}}
struct Fixture {
 VxnnMosaicPrimitive p{2,0,0,{1,1,8,8,3,1,1,32},1.f,false};
 std::vector<int16_t>q,w,a,b;std::vector<float>out,coeff;int64_t acc[64]{};fake_s8 fake[4];VxnnMosaicBuffers v;
 Fixture(uint32_t ci,uint32_t co,uint32_t h,uint32_t width,uint32_t k,uint32_t stride,uint32_t up):q(ci*h*width),w(co*((ci*k*k+31)/32*32)),a(8*2592),b(8*2592),out(co*((h*up+stride-1)/stride)*((width*up+stride-1)/stride),-12345.f),coeff(co*2){
  p.geometry={ci,co,h,width,k,stride,up,(ci*k*k+31)/32*32};  uint32_t spatial=((h*up+stride-1)/stride)*((width*up+stride-1)/stride);p.count=((co+7)/8)*((spatial+7)/8);
  for(size_t i=0;i<q.size();i++)q[i]=int16_t(int(i%17)-8);
  for(uint32_t c=0;c<co;c++){coeff[c*2]=0.125f;coeff[c*2+1]=float(int(c%3)-1);for(uint32_t j=0;j<ci*k*k;j++)w[c*p.geometry.kpad+j]=int16_t(int((j+c)%7)-3);}
  v.quant={q.data(),q.size()};v.weights={w.data(),w.size()};v.coeff={coeff.data(),coeff.size()};v.output={out.data(),out.size()};v.a={a.data(),a.size()};v.b={b.data(),b.size()};v.acc={acc,64};
 }
};
static unsigned mirror(int x,unsigned n){while(x<0||x>=int(n))x=x<0?-x:2*int(n)-2-x;return unsigned(x);}
static std::vector<float>reference(const Fixture&f){
 const auto&g=f.p.geometry;unsigned oh=(g.h*g.upsample+g.stride-1)/g.stride,ow=(g.w*g.upsample+g.stride-1)/g.stride;std::vector<float>expected(g.co*oh*ow);
 for(unsigned c=0;c<g.co;c++)for(unsigned y=0;y<oh;y++)for(unsigned x=0;x<ow;x++){
  int64_t total=0;for(unsigned ci=0;ci<g.ci;ci++)for(unsigned ky=0;ky<g.k;ky++)for(unsigned kx=0;kx<g.k;kx++){
   unsigned iy=mirror(int(y*g.stride+ky)-int(g.k/2),g.h*g.upsample)/g.upsample,ix=mirror(int(x*g.stride+kx)-int(g.k/2),g.w*g.upsample)/g.upsample;
   total+=int64_t(f.q[(ci*g.h+iy)*g.w+ix])*f.w[c*g.kpad+(ci*g.k+ky)*g.k+kx];
  }
  volatile float value=float(total),weighted=value*f.coeff[c*2],scaled=weighted*f.p.scale,bias=scaled+f.coeff[c*2+1];expected[(c*oh+y)*ow+x]=bias;
 }
 return expected;
}
int main(){
 // Seven independent geometries x three chunk budgets =21 math vectors.
 const unsigned cases[7][7]={{1,1,8,8,3,1,1},{3,9,8,12,3,1,1},{2,7,12,8,3,2,1},{1,3,8,8,3,1,2},{3,3,8,8,9,1,1},{32,3,8,8,9,1,1},{128,9,8,8,3,1,1}};
 unsigned vectors=0;
 for(const auto&c:cases)for(unsigned budget:{1u,3u,16u}){
  Fixture f(c[0],c[1],c[2],c[3],c[4],c[5],c[6]);auto expected=reference(f);const unsigned total=f.p.count;
  for(unsigned first=0;first<total;first+=budget){f.p.first=first;f.p.count=total-first<budget?total-first:budget;for(unsigned core=0;core<4;core++){auto io=f.fake[core].ops();io.core=core;check(vxnn_mosaic_primitive(&f.p,&f.v,core,&io)==0,"bounded native convolution");}}
  check(f.out.size()==expected.size()&&!std::memcmp(f.out.data(),expected.data(),expected.size()*4),"signed16 i48 restore reflect/tails exact");
  unsigned done=0;for(auto&pe:f.fake){check(pe.begins==pe.releases&&!pe.active,"all PE results drained and retired");done+=pe.begins;}check(done==total,"no duplicate/missing PE tiles");++vectors;
 }
 Fixture invalid(1,1,8,8,3,1,1);auto io=invalid.fake[0].ops();invalid.p.count=4097;check(vxnn_mosaic_primitive(&invalid.p,&invalid.v,0,&io)<0&&!invalid.fake[0].begins,"original bounded limit before PE");
 invalid.p.count=1;invalid.v.output.data=reinterpret_cast<float*>(invalid.q.data());check(vxnn_mosaic_primitive(&invalid.p,&invalid.v,0,&io)<0&&!invalid.fake[0].begins,"alias rejected before PE");
 Fixture fault(1,1,8,8,3,1,1);auto bad=fault.fake[0].ops();fault.fake[0].fail_step=0;check(vxnn_mosaic_primitive(&fault.p,&fault.v,0,&bad)<0&&fault.fake[0].begins==1&&!fault.fake[0].releases,"PE failure retained without release");
 Fixture round(1,1,8,8,3,1,1);auto round_io=round.fake[0].ops();check(!fesetround(FE_DOWNWARD),"set convolution rounding");check(vxnn_mosaic_primitive(&round.p,&round.v,0,&round_io)<0&&!round.fake[0].begins,"all primitive entries reject nonRNE before PE");check(!fesetround(FE_TONEAREST),"restore convolution rounding");
 float input[]={.5f,1.5f,2.5f,-.5f,-1.5f,-2.5f,1e30f,-1e30f},saved[8]{};int16_t quant[8]{};const int16_t expect[]={0,2,2,0,-2,-2,32767,-32767};
 VxnnMosaicPrimitive p{1,0,1,{1,1,2,4,3,1,1,32},1.f,true};VxnnMosaicBuffers b;b.input={input,8};b.quant={quant,8};b.skip={saved,8};
 check(!vxnn_mosaic_primitive(&p,&b,0,nullptr)&&!memcmp(quant,expect,sizeof(expect))&&!memcmp(input,saved,sizeof(input)),"RNE saturation and explicit skip copy");
 check(!fesetround(FE_DOWNWARD),"set rounding");check(vxnn_mosaic_primitive(&p,&b,0,nullptr)<0,"nonRNE rejected");check(!fesetround(FE_TONEAREST),"restore rounding");
 for(unsigned side:{8u,64u,128u}){
  const unsigned channels=5,spatial=side*side;std::vector<float>x(channels*spatial),old,stats(channels*3),hi(spatial),lo(spatial),gamma(channels),beta(channels),skip(channels*spatial);
  for(size_t i=0;i<x.size();i++)x[i]=float(int(i%113)-56)*.013f+float(i%17)*.00001f;
  old=x;for(unsigned ch=0;ch<channels;ch++){gamma[ch]=float(int(ch)-2)*.625f;beta[ch]=float(ch)*.125f;float reference_stats[3];mosaic_norm_workspace ws{hi.data(),lo.data(),spatial};check(!mosaic_norm_stats_compat_v3(old.data()+ch*spatial,spatial,&ws,reference_stats)&&!mosaic_norm_apply_compat_v3(old.data()+ch*spatial,spatial,reference_stats,gamma[ch],beta[ch],spatial,old.data()+ch*spatial),"frozen v3 norm reference");}
  VxnnMosaicPrimitive np{3,0,channels,{1,channels,side,side,3,1,1,32},1.f,false};VxnnMosaicBuffers nb;nb.output={x.data(),x.size()};nb.stats={stats.data(),stats.size()};nb.norm={hi.data(),lo.data(),spatial};nb.gamma={gamma.data(),gamma.size()};nb.beta={beta.data(),beta.size()};
  for(unsigned core=0;core<4;core++)check(!vxnn_mosaic_primitive(&np,&nb,core,nullptr),"stats original per-channel reduction");
  np.opcode=4;unsigned items=(x.size()+255)/256;
  for(unsigned first=0;first<items;first+=3){np.first=first;np.count=items-first<3?items-first:3;for(unsigned core=0;core<4;core++)check(!vxnn_mosaic_primitive(&np,&nb,core,nullptr),"apply v3 tiled channel-spatial dispatch");}
  check(!memcmp(x.data(),old.data(),x.size()*4),"frozen v3 norm bytes unchanged");
  // SSA graph preparation cannot silently enable in-place tensor aliases.
  // Separate destinations use the same scalar order without a copy GPU job.
  std::vector<float>read(x.size()),destination(x.size(),-555.f),separate_stats(stats.size());
  for(size_t i=0;i<read.size();i++)read[i]=float(int(i%113)-56)*.013f+float(i%17)*.00001f;
  auto source_snapshot=read;np.separate_input=true;np.opcode=3;np.first=0;np.count=channels;nb.input={read.data(),read.size()};nb.output={};nb.stats={separate_stats.data(),separate_stats.size()};
  for(unsigned core=0;core<4;core++){check(!vxnn_mosaic_primitive(&np,&nb,core,nullptr),"separate immutable stats source");}
  check(!memcmp(stats.data(),separate_stats.data(),stats.size()*4),"separate stats unchanged");
  nb.output={destination.data(),destination.size()};np.opcode=4;np.count=items;
  for(unsigned core=0;core<4;core++){check(!vxnn_mosaic_primitive(&np,&nb,core,nullptr),"separate norm output");}
  check(!memcmp(destination.data(),old.data(),old.size()*4)&&!memcmp(read.data(),source_snapshot.data(),read.size()*4),"separate norm exact source preserved");
  nb.output={read.data(),read.size()};check(vxnn_mosaic_primitive(&np,&nb,0,nullptr)<0,"SSA mode rejects aliases");np.separate_input=false;nb.input={};nb.output={x.data(),x.size()};nb.stats={stats.data(),stats.size()};
  for(size_t i=0;i<x.size();i++){skip[i]=float(int(i%5)-2);volatile float add=old[i]+skip[i];old[i]=add;}
  nb.skip={skip.data(),skip.size()};np.opcode=6;np.first=0;np.count=items;for(unsigned core=0;core<4;core++)check(!vxnn_mosaic_primitive(&np,&nb,core,nullptr),"add global item owners");check(!memcmp(x.data(),old.data(),x.size()*4),"residual order unchanged");
  np.opcode=5;for(auto&v:old)v=v>0?v:0.f;for(unsigned core=0;core<4;core++)check(!vxnn_mosaic_primitive(&np,&nb,core,nullptr),"relu global item owners");check(!memcmp(x.data(),old.data(),x.size()*4),"relu exact");
  for(uint32_t opcode:{5u,6u}){
   for(size_t i=0;i<read.size();i++)read[i]=float(int(i%7)-3)*.25f;
   source_snapshot=read;destination.assign(read.size(),-999.f);auto expected=read;
   for(size_t i=0;i<expected.size();i++){volatile float value=opcode==5?(read[i]>0?read[i]:0.f):read[i]+skip[i];expected[i]=value;}
   np.opcode=opcode;np.separate_input=true;np.first=0;np.count=items;nb.input={read.data(),read.size()};nb.output={destination.data(),destination.size()};
   for(unsigned core=0;core<4;core++){check(!vxnn_mosaic_primitive(&np,&nb,core,nullptr),"separate scalar output");}
   check(!memcmp(destination.data(),expected.data(),expected.size()*4)&&!memcmp(read.data(),source_snapshot.data(),read.size()*4),"separate scalar exact source retained");
  }
 }
 std::printf("U13_MOSAIC_PRIMITIVE_21_NATIVE_I16_I48_HOST_MATH_PASS vectors=%u gpu_raw_cycles=null\n",vectors);
}
