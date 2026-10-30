// Frozen instruction/shared-PC model, never GPU timing, WSPAWN, RTL or DP proof.
#include <vxunified/p8.hpp>
#include <vxl/hunet.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
using namespace vxunified;
#define CM_RAW 0x15000000u
#define CM_PROJECTED 0x15100000u
#define CM_PRETRI 0x15300000u
#define CM_DIR 0x15b10000u
#define CM_REFS 0x15b40000u
#define CM_BG 0x17c00000u
#define FB64_BASE 0x17500000u
#define V3_DEPTH 0x17610000u
#define V3_TEX 0x19400000u
static uint32_t active_slots,pixels,cm_sizes[10],cm_active_refs;
#define CM_ACTIVE_SLOTS active_slots
#define CM_PIXELS pixels
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#include "vendor/p8/rv32_complex_model.h"
#pragma GCC diagnostic pop
static void need(bool yes,const char*s){if(!yes){std::fprintf(stderr,"U16_INSTRUCTION_ASSERT %s\n",s);std::exit(1);}}
static std::vector<uint32_t>words(const std::string&p){std::ifstream f(p,std::ios::binary|std::ios::ate);need(bool(f),"immutable code readable");auto n=f.tellg();need(n>0&&n<2048&&n%4==0,"code extent");std::vector<uint32_t>v(size_t(n)/4);f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);need(bool(f),"whole code");return v;}
int main(int argc,char**argv){
 need(argc==2,"asset directory");auto loaded=P8Assets::load(argv[1]);need(bool(loaded),"frozen asset SHA bank");std::vector<uint32_t>memory(0xa500000u/4u);const uint32_t bases[10]={FB64_BASE,V3_DEPTH,V3_TEX,CM_RAW,CM_PROJECTED,0x15200000u,CM_DIR,CM_REFS,CM_PRETRI,CM_BG};
 auto at=[&](uint32_t address){need(address>=0x10000000u&&address<0x1a500000u,"bounded host address");return memory.data()+(address-0x10000000u)/4u;};
 const uint32_t poses[8]={0,100,25,101,50,102,75,103};unsigned frames=0;
 for(unsigned object=0;object<3;object++)for(unsigned view=0;view<(object==2?8u:1u);view++){
  SceneState scene;scene.object=P8Object(object);scene.pose=poses[view];scene.startup_remaining=0;scene.remaining=1;scene.gallery=true;auto prepared=(*loaded.value)->prepare(scene,true);need(bool(prepared),"portable independent triangle reference");const auto&frame=*prepared.value;active_slots=frame.geometry.active_slots;pixels=frame.geometry.width*frame.geometry.width;cm_active_refs=frame.active_refs;std::fill(memory.begin(),memory.end(),0);
  for(unsigned i=0;i<10;i++){cm_sizes[i]=frame.buffers[i].size()*4;if(i!=0&&i!=1&&i!=4){std::memcpy(at(bases[i]),frame.buffers[i].data(),cm_sizes[i]);}}
  std::fill_n(at(CM_PROJECTED),8*active_slots,0xdeadbeef);std::array<uint32_t,16>args{};args[0]=CM_RAW;args[1]=CM_PROJECTED;args[2]=active_slots;std::copy(frame.matrix.begin(),frame.matrix.end(),args.begin()+3);std::memcpy(at(0x10010000),args.data(),64);
  auto transform=words(std::string(argv[1])+(object==2?"/cm_transform_high_kernel.bin":"/cm_transform_kernel.bin"));uint64_t instructions=0;need(!rv32_model_run(transform.data(),transform.size(),memory.data(),0,&instructions),"unchanged transform instruction model");need(!std::memcmp(at(CM_PROJECTED),frame.buffers[4].data(),frame.buffers[4].size()*4),"exact projected all words including inactive padding");
  auto raster=words(std::string(argv[1])+(object==2?"/cm_raster_high_kernel.bin":"/cm_raster_kernel.bin"));
  for(unsigned reverse=0;reverse<(object==0?2u:1u);reverse++){
   std::fill_n(at(FB64_BASE),pixels,0xdeadbeef);std::fill_n(at(V3_DEPTH),pixels,0xdeadbeef);args={FB64_BASE,V3_DEPTH,CM_PRETRI,CM_DIR,V3_TEX,CM_REFS,0xffece4dc,reverse,frame.geometry.faces,CM_BG};std::memcpy(at(0x10010000),args.data(),64);
   need(!rv32_model_run(raster.data(),raster.size(),memory.data(),1,&instructions),"unchanged raster instruction model");need(!std::memcmp(at(FB64_BASE),frame.buffers[0].data(),pixels*4)&&!std::memcmp(at(V3_DEPTH),frame.buffers[1].data(),pixels*4),"exact color/depth against independent whole-triangle CPU reference");
  }
  frames++;std::printf("U16_P8_INSTRUCTION_FRAME object=%u pose=%u width=%u input_vertices=%u faces=%u host_model_instructions=%llu gpu_raw_cycles=null\n",object,scene.pose,frame.geometry.width,frame.geometry.vertices,frame.geometry.faces,static_cast<unsigned long long>(instructions));std::fflush(stdout);
 }
 need(frames==10,"three meshes plus eight warrior views");puts("U16_FROZEN_RV32_P8_10_FRAMES_AND_REVERSE_EXACT_PASS_HOST_NOT_GPU_RTL_DP gpu_raw_cycles=null");
}
