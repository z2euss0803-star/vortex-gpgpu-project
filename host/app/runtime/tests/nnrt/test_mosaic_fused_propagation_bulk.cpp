#include "device/vxnn/mosaic_ops.h"
#include "tests/mosaic_s8/fake_s8.h"
#include <vxnn/mosaic_coverage.hpp>
#include <vxl/mosaic_assets.h>
#include "mosaic_fixture.h"
#include "mosaic_contract_fixture.h"
#include <vector>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cmath>
static void require(bool ok,const char*why){if(!ok){std::fprintf(stderr,"U13_MOSAIC_PROPAGATION_ASSERT %s\n",why);std::exit(76);}}
static uint32_t u32(const unsigned char*p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
static uint64_t u64(const unsigned char*p){return uint64_t(u32(p))|uint64_t(u32(p+4))<<32;}
static std::vector<float>floats(const unsigned char*p,size_t n){std::vector<float>r(n);for(size_t i=0;i<n;i++){auto bits=u32(p+i*4);memcpy(&r[i],&bits,4);}return r;}
struct Layer {std::vector<int16_t>w;std::vector<float>coeff,gamma,beta;};
struct Fixture {
 mosaic_device_context ctx{};Layer layer[16];fake_s8 fake[4];std::vector<float>fp[2],skip,stats,hi[4],lo[4],stages[16];std::vector<int16_t>quant,a[4],b[4];int64_t acc[4][64]{};unsigned callbacks=0;
 explicit Fixture(const std::vector<unsigned char>&blob){
  require(!mosaic_assets_validate(blob.data(),blob.size(),&fixture_manifest),"immutable original W16A16 assets");
  ctx.width=12;ctx.height=8;ctx.layout=fixture_small_layout;std::memcpy(&ctx.expected,fixture_job,128);
  fp[0].resize(3088,321.25f);fp[1].resize(3088,321.25f);quant.resize(3072);skip.resize(768);stats.resize(384);
  for(size_t i=0;i<288;i++)fp[0][i]=float(int(i%37)-18)*.025f;
  for(unsigned i=0;i<16;i++){
   const auto*row=blob.data()+256+i*384;unsigned co=u32(row+4),kp=u32(row+20);const auto*q=blob.data()+u64(row+72);auto&l=layer[i];l.w.resize(co*kp);
   for(size_t j=0;j<l.w.size();j++){uint32_t v=uint32_t(q[j*2])|uint32_t(q[j*2+1])<<8;l.w[j]=int16_t(v<32768?int32_t(v):int32_t(v)-65536);}
   l.coeff=floats(blob.data()+u64(row+136),co*2);auto sa=floats(blob.data()+u64(row+200),1);
   if(i!=15){l.gamma=floats(blob.data()+u64(row+264),co);l.beta=floats(blob.data()+u64(row+328),co);}
   ctx.parameters[i]={l.w.data(),l.w.size(),l.coeff.data(),l.coeff.size(),sa[0],l.gamma.data(),l.beta.data()};
  }
  auto&m=ctx.memory;m.fp[0]=fp[0].data();m.fp[1]=fp[1].data();m.fp_count=3072;m.quantized=quant.data();m.quantized_count=quant.size();m.skip=skip.data();m.skip_count=skip.size();m.stats=stats.data();m.stats_count=stats.size();m.tile_capacity=8*2592;
  for(unsigned core=0;core<4;core++){a[core].resize(8*2592);b[core].resize(8*2592);hi[core].resize(128);lo[core].resize(128);m.a[core]=a[core].data();m.b[core]=b[core].data();m.tile[core]=acc[core];m.norm[core]={hi[core].data(),lo[core].data(),128};ctx.io[core]=fake[core].ops();ctx.io[core].core=core;}
  ctx.callback_context=this;ctx.on_stage=[](void*opaque,uint32_t index,const float*,size_t,const float*out,size_t count){auto*f=static_cast<Fixture*>(opaque);require(index==f->callbacks++,"reference layer order");f->stages[index].assign(out,out+count);return 0;};
 }
};
static int r6_host_steps(void*context,const int16_t*a,const int16_t*b,uint32_t k){
 auto*f=static_cast<fake_s8*>(context);auto original=f->ops();for(uint32_t t=0;t<k;t++){mosaic_words4 av{},bv{};for(uint32_t lane=0;lane<4;lane++){av.word[lane]=mosaic_lane_word(a+t*8,lane);bv.word[lane]=mosaic_lane_word(b+t*8,lane);}int rc=original.step(context,&av,&bv);if(rc)return rc;}return 0;
}

int main(int argc,char**argv){
 require(argc==2,"asset argument");std::ifstream file(argv[1],std::ios::binary);require(bool(file),"asset read");std::vector<unsigned char>blob((std::istreambuf_iterator<char>(file)),{});
 Fixture old(blob);old.ctx.diagnostics=true;require(!mosaic_run_graph(&old.ctx)&&old.callbacks==16,"original sixteen-layer scalar contract reference");
 for(uint32_t budget:{3u,16u,64u,4096u}){
  Fixture candidate(blob);std::vector<float>saved[2]={std::vector<float>(784,321.25f),std::vector<float>(784,321.25f)};int active=-1;uint64_t jobs=0;unsigned nodes=0,save_count=0;
  auto phase=[&](unsigned layer,unsigned opcode,float scale,bool save){
   mosaic_layer g{};require(!mosaic_layer_geometry(layer,12,8,&g),"original geometry");const size_t spatial=size_t((g.h*g.upsample+g.stride-1)/g.stride)*((g.w*g.upsample+g.stride-1)/g.stride),elements=size_t(g.co)*spatial;
   uint32_t items=opcode==1?(g.ci*g.h*g.w+255)/256:opcode==2?((g.co+7)/8)*((spatial+7)/8):opcode==3?g.co:(elements+255)/256;int destination=save?(active==0?1:0):-1;++nodes;
   for(uint32_t first=0;first<items;){uint32_t count=items-first;if(count>budget)count=budget;VxnnMosaicPrimitive p{opcode,first,count,{g.ci,g.co,g.h,g.w,g.k,g.stride,g.upsample,g.kpad},scale,save,opcode==3};
    for(unsigned core=0;core<4;core++){
     auto&l=candidate.layer[layer];VxnnMosaicBuffers b;b.input={candidate.fp[layer%2].data(),3072};b.output={candidate.fp[(layer+1)%2].data(),3072};b.quant={candidate.quant.data(),3072};b.weights={l.w.data(),l.w.size()};b.coeff={l.coeff.data(),l.coeff.size()};b.stats={candidate.stats.data(),384};b.gamma={l.gamma.data(),l.gamma.size()};b.beta={l.beta.data(),l.beta.size()};b.a={candidate.a[core].data(),candidate.a[core].size()};b.b={candidate.b[core].data(),candidate.b[core].size()};b.acc={candidate.acc[core],64};b.norm=candidate.ctx.memory.norm[core];
     if(opcode==3||opcode>=7){b.input={candidate.fp[(layer+1)%2].data(),3072};b.output=save?VxnnMosaicSpan<float>{saved[destination].data(),768}:VxnnMosaicSpan<float>{};}
     if(opcode==8){require(active>=0,"old residual exists before fused add");b.skip={saved[active].data(),768};}
     auto native_bulk=candidate.ctx.io[core];native_bulk.steps=r6_host_steps;int rc=vxnn_mosaic_primitive(&p,&b,core,&native_bulk);if(rc)std::fprintf(stderr,"layer=%u opcode=%u core=%u rc=%d\n",layer,opcode,core,rc);require(!rc,"actual new fused primitive full propagation");
    }first+=count;++jobs;
   }
   if(save){active=destination;++save_count;require(!memcmp(saved[active].data(),old.stages[layer].data(),old.stages[layer].size()*4),"all five FP32 residual producer bytes exact");}
  };
  phase(0,1,candidate.ctx.parameters[0].sa,false);
  for(unsigned i=0;i<16;i++){
   phase(i,2,candidate.ctx.parameters[i].sa,false);
   if(i==15){require(!memcmp(candidate.fp[0].data(),old.stages[i].data(),old.stages[i].size()*4),"final FP32 output exact");break;}
   phase(i,3,0,false);const float next_scale=candidate.ctx.parameters[i+1].sa;phase(i,(i>=4&&i<=12&&!(i&1))?8:7,next_scale,i+1>=3&&i+1<=11&&((i+1)&1));
   std::vector<int16_t>expected(old.stages[i].size());volatile float inverse=1.f/next_scale;require(!mosaic_quantize_input(old.stages[i].data(),expected.size(),inverse,expected.data()),"independent original quantizer");require(!memcmp(candidate.quant.data(),expected.data(),expected.size()*2),"all fifteen inter-layer INT16 boundaries bit exact");
  }
  require(nodes==47&&save_count==5,"47 actual arithmetic nodes and five residual saves");for(unsigned core=0;core<4;core++)require(candidate.fake[core].begins==candidate.fake[core].releases&&candidate.fake[core].begins==old.fake[core].begins,"full sixteen convolution tile coverage and retirement");
  for(unsigned i=3072;i<3088;i++){require(candidate.fp[0][i]==321.25f&&candidate.fp[1][i]==321.25f,"FP32 guards unchanged");}for(const auto&s:saved)for(unsigned i=768;i<784;i++){require(s[i]==321.25f,"residual save guards unchanged");}
  std::printf("MOSAIC_FUSED47_FULL16_PROPAGATION_EXACT_HOST_PASS budget=%u jobs=%llu gpu_raw_cycles=null\n",budget,(unsigned long long)jobs);
 }
 puts("SMALL_SYNTHETIC_NOT_FORMAL21_NOT_GPU_NOT_BOARD");return 0;
}
