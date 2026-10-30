#include "graph.h"
#include "fake_s8.h"
#include <vxl/mosaic_assets.h>
#include "mosaic_fixture.h"
#include "mosaic_contract_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <string>
#include <vector>
static std::vector<unsigned char> bytes(const std::string &path){std::ifstream f(path,std::ios::binary);assert(f);return std::vector<unsigned char>(std::istreambuf_iterator<char>(f),{});}
static uint32_t u32(const unsigned char *p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
static uint64_t u64(const unsigned char *p){return uint64_t(u32(p))|(uint64_t(u32(p+4))<<32);}
static std::vector<float> floats(const unsigned char *p,size_t n){std::vector<float> out(n);for(size_t i=0;i<n;++i){uint32_t b=u32(p+i*4);memcpy(&out[i],&b,4);}return out;}
struct layer_storage {std::vector<int16_t>q;std::vector<float>coeff,gamma,beta;};
struct fixture {
 mosaic_device_context ctx={};fake_s8 fake[4];layer_storage layer[16];
 std::vector<float>fp[2],skip,stats,hi[4],lo[4],input,golden[16];
 std::vector<int16_t>quant,a[4],b[4];int64_t tile[4][64];
 std::string output;unsigned callbacks=0;
 fixture(const char *assets,const char *reference,const char *out):output(out){
  auto blob=bytes(std::string(assets)+"/weights.ms8");assert(!mosaic_assets_validate(blob.data(),blob.size(),&fixture_manifest));
  auto raw=bytes(std::string(reference)+"/input.f32");input=floats(raw.data(),raw.size()/4);
  ctx.width=12;ctx.height=8;ctx.layout=fixture_small_layout;
  fp[0].resize(3088,321.25f);fp[1].resize(3088,321.25f);quant.resize(3072);skip.resize(768);stats.resize(384);
  for(unsigned i=0;i<16;++i){
   auto *record=blob.data()+256+i*384;unsigned co=u32(record+4),kpad=u32(record+20);
   auto *q=blob.data()+u64(record+72);layer[i].q.resize(co*kpad);
   for(size_t x=0;x<co*kpad;++x){uint32_t v=uint32_t(q[2*x])|(uint32_t(q[2*x+1])<<8);layer[i].q[x]=int16_t(v<32768?int32_t(v):int32_t(v)-65536);}
   layer[i].coeff=floats(blob.data()+u64(record+64+64+8),co*2);
   auto sa=floats(blob.data()+u64(record+64+128+8),1);
   if(i!=15){layer[i].gamma=floats(blob.data()+u64(record+64+192+8),co);layer[i].beta=floats(blob.data()+u64(record+64+256+8),co);}
   ctx.parameters[i]={layer[i].q.data(),layer[i].q.size(),layer[i].coeff.data(),layer[i].coeff.size(),sa[0],layer[i].gamma.data(),layer[i].beta.data()};
   char name[40];snprintf(name,sizeof(name),"/stage%02u.f32",i);raw=bytes(std::string(reference)+name);golden[i]=floats(raw.data(),raw.size()/4);
  }
  auto &m=ctx.memory;m.fp[0]=fp[0].data();m.fp[1]=fp[1].data();m.fp_count=3072;
  m.quantized=quant.data();m.quantized_count=quant.size();m.skip=skip.data();m.skip_count=skip.size();m.stats=stats.data();m.stats_count=stats.size();m.tile_capacity=8*2592;
  for(unsigned i=0;i<4;++i){a[i].resize(8*2592);b[i].resize(8*2592);hi[i].resize(128);lo[i].resize(128);m.a[i]=a[i].data();m.b[i]=b[i].data();m.tile[i]=tile[i];m.norm[i]={hi[i].data(),lo[i].data(),128};ctx.io[i]=fake[i].ops();ctx.io[i].core=i;}
  memcpy(fp[0].data(),input.data(),input.size()*4);memcpy(&ctx.expected,fixture_job,128);
  ctx.callback_context=this;ctx.on_stage=stage;
 }
 static int stage(void *p,uint32_t index,const float *previous,size_t previous_count,const float *out,size_t count){
  auto &f=*static_cast<fixture*>(p);assert(index==f.callbacks++);
  const auto &prior=index?f.golden[index-1]:f.input;
  assert(previous_count==prior.size());assert(!memcmp(previous,prior.data(),previous_count*4));
  assert(count==f.golden[index].size());
  char name[40];snprintf(name,sizeof(name),"/candidate-stage%02u.f32",index);std::ofstream file(f.output+name,std::ios::binary);file.write(reinterpret_cast<const char*>(out),count*4);assert(file);
  if(memcmp(out,f.golden[index].data(),count*4)){for(size_t i=0;i<count;++i)if(out[i]!=f.golden[index][i]){printf("FIRST_SMALL_REFERENCE_DIFFERENCE stage=%u index=%zu got=%.9g expected=%.9g\n",index,i,out[i],f.golden[index][i]);break;}fflush(stdout);return -EDOM;}
  return 0;
 }
};
int main(int argc,char **argv){
 for(unsigned phase=0;phase<77;++phase){uint32_t l,op,n;assert(!mosaic_phase_description(phase,12,8,&l,&op,&n));assert(l==fixture_small_phase_spec[phase][0]&&op==fixture_small_phase_spec[phase][1]&&n==fixture_small_phase_spec[phase][2]);assert(!mosaic_phase_description(phase,1280,960,&l,&op,&n));assert(l==fixture_full_phase_spec[phase][0]&&op==fixture_full_phase_spec[phase][1]&&n==fixture_full_phase_spec[phase][2]);}
 assert(argc==4);fixture f(argv[1],argv[2],argv[3]);f.ctx.diagnostics=true;
 assert(mosaic_run_graph(&f.ctx)==0);
 assert(f.callbacks==16&&f.ctx.logical_jobs==77&&f.ctx.quantized_elements>0);
 uint64_t quantized=0;for(uint32_t i=0;i<16;++i){mosaic_layer l;assert(!mosaic_layer_geometry(i,12,8,&l));quantized+=uint64_t(l.ci)*l.h*l.w;}
 assert(f.ctx.quantized_elements==quantized);
 unsigned expected_tiles[4]={};
 for(auto &spec:fixture_small_phase_spec)if(spec[1]==2)for(unsigned tile=0;tile<spec[2];++tile)++expected_tiles[tile%4];
 unsigned total=0;for(unsigned i=0;i<4;++i){assert(f.fake[i].begins==f.fake[i].releases&&f.fake[i].begins==expected_tiles[i]);total+=expected_tiles[i];for(unsigned j=3072;j<3088;++j)assert(f.fp[0][j]==321.25f&&f.fp[1][j]==321.25f);}
 assert(f.ctx.tiles==total);
 assert(f.ctx.weight_packs==260&&f.ctx.weight_packs<f.ctx.tiles);
 fixture plain(argv[1],argv[2],argv[3]);assert(!plain.ctx.diagnostics);assert(!mosaic_run_graph(&plain.ctx));assert(plain.callbacks==0);
 assert(!memcmp(plain.fp[0].data(),f.fp[0].data(),288*4));
 fixture split(argv[1],argv[2],argv[3]);split.ctx.batch_items=3;assert(!mosaic_run_graph(&split.ctx));
 assert(split.ctx.logical_jobs>77&&split.ctx.quantized_elements==quantized&&split.ctx.tiles==total&&split.callbacks==0);
 assert(!memcmp(split.fp[0].data(),f.fp[0].data(),288*4));
 fixture fault(argv[1],argv[2],argv[3]);fault.fake[0].fail_step=0;
 assert(mosaic_run_graph(&fault.ctx)<0);assert(fault.ctx.failed&&fault.callbacks==0&&fault.ctx.logical_jobs==2);
 for(unsigned i=1;i<4;++i)assert(fault.fake[i].begins==0);
 unsigned attempts=fault.fake[0].begins;assert(mosaic_run_graph(&fault.ctx)<0);assert(fault.fake[0].begins==attempts);
 for(unsigned which=0;which<4;++which){
  fixture bad(argv[1],argv[2],argv[3]);mosaic_job_v1 job=bad.ctx.expected;job.tile_count=2;job.crc=mosaic_job_crc(&job);bad.ctx.expected=job;
  if(which==0)job.op=2;
  if(which==1)job.generation=2;
  if(which==2)job.tile_count=3;
  if(which==3)bad.ctx.memory.fp[1]=bad.ctx.memory.fp[0];
  job.crc=mosaic_job_crc(&job);
  assert(mosaic_execute_job(&job,&bad.ctx)<0&&bad.ctx.failed);for(auto &s:bad.fake)assert(!s.begins);
 }
 printf("MOSAIC_SMALL16_SOFTWARE_PROPAGATION_PASS_NOT_FULL_QUALITY_NOT_GPU jobs=%llu quantized=%llu tiles=%llu weight_packs=%llu raw_gpu_cycles=null\n",(unsigned long long)f.ctx.logical_jobs,(unsigned long long)f.ctx.quantized_elements,(unsigned long long)f.ctx.tiles,(unsigned long long)f.ctx.weight_packs);
}
