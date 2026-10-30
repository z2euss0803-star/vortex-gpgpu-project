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
int main(int argc,char**argv){
 require(argc==2,"asset argument");std::ifstream file(argv[1],std::ios::binary);require(bool(file),"asset read");std::vector<unsigned char>blob((std::istreambuf_iterator<char>(file)),{});
 Fixture old(blob);old.ctx.diagnostics=true;require(!mosaic_run_graph(&old.ctx)&&old.callbacks==16,"frozen v3 sixteen-layer reference");
 for(bool separate:{false,true})for(uint32_t budget:{3u,4u,16u,64u}){
  Fixture candidate(blob);uint64_t jobs=0;
  for(unsigned phase=0;phase<77;phase++){
   auto described=vxnn::describe_mosaic_phase(12,8,phase);require(bool(described),"phase description");auto coverage=vxnn::bounded_tiles(*described.value,budget);require(bool(coverage),"bounded coverage");unsigned index=described.value->layer;mosaic_layer g{};require(!mosaic_layer_geometry(index,12,8,&g),"frozen geometry reference");auto&p=candidate.ctx.parameters[index];
   const auto opcode=described.value->opcode;std::vector<float>phase_output;
   if(separate&&opcode>=4)phase_output.assign(3088,321.25f);
   for(uint64_t batch=0;batch<coverage.value->jobs();batch++){
    auto range=coverage.value->at(batch);require(bool(range),"batch bounds");VxnnMosaicPrimitive primitive{opcode,range.value->first,range.value->count,{g.ci,g.co,g.h,g.w,g.k,g.stride,g.upsample,g.kpad},p.sa,opcode==1&&index>=3&&index<=11&&(index&1),separate&&opcode>=3};
    for(unsigned core=0;core<4;core++){
     VxnnMosaicBuffers b;b.input={candidate.fp[index%2].data(),3072};b.output={candidate.fp[(index+1)%2].data(),3072};b.quant={candidate.quant.data(),candidate.quant.size()};b.skip={candidate.skip.data(),candidate.skip.size()};b.stats={candidate.stats.data(),candidate.stats.size()};auto&l=candidate.layer[index];b.weights={l.w.data(),l.w.size()};b.coeff={l.coeff.data(),l.coeff.size()};b.gamma={l.gamma.data(),l.gamma.size()};b.beta={l.beta.data(),l.beta.size()};b.a={candidate.a[core].data(),candidate.a[core].size()};b.b={candidate.b[core].data(),candidate.b[core].size()};b.acc={candidate.acc[core],64};b.norm=candidate.ctx.memory.norm[core];
     if(separate&&opcode>=3){b.input={candidate.fp[(index+1)%2].data(),3072};b.output=opcode==3?VxnnMosaicSpan<float>{}:VxnnMosaicSpan<float>{phase_output.data(),3072};}
     int rc=vxnn_mosaic_primitive(&primitive,&b,core,&candidate.ctx.io[core]);if(rc){std::fprintf(stderr,"phase=%u layer=%u opcode=%u first=%u core=%u rc=%d\n",phase,index,primitive.opcode,primitive.first,core,rc);}require(!rc,"generic primitive phase");
    }
    jobs++;
   }
   if(separate&&opcode>=4)candidate.fp[(index+1)%2].swap(phase_output); // host fixture swaps ownership; no simulated GPU copy job
   if(phase==76||vxnn::describe_mosaic_phase(12,8,phase+1).value->layer!=index){const auto&expected=old.stages[index];require(!expected.empty()&&!memcmp(candidate.fp[(index+1)%2].data(),expected.data(),expected.size()*4),"sixteen frozen intermediate outputs bit exact");}
  }
  for(unsigned core=0;core<4;core++){require(candidate.fake[core].begins==candidate.fake[core].releases&&candidate.fake[core].begins==old.fake[core].begins,"PE tile coverage and retirement");for(unsigned i=3072;i<3088;i++)require(candidate.fp[0][i]==321.25f&&candidate.fp[1][i]==321.25f,"host tail guard retained");}
  std::printf("U13_MOSAIC_16_LAYER_GENERIC_PROPAGATION_PASS_HOST separate=%u budget=%u jobs=%llu gpu_raw_cycles=null\n",unsigned(separate),budget,(unsigned long long)jobs);
 }
 std::puts("U13_MOSAIC_PROPAGATION_NOT_FORMAL21_QUALITY_NOT_GPU_NOT_BOARD");
}
