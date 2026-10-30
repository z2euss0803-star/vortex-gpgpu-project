#include <vxunified/p8.hpp>
#include <vxunified/texture.hpp>
#include <vxl/hunet.h>
#include <cstdio>
#include <cstring>
#include <fstream>
using namespace vxunified;
namespace {
void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U17_P8_TEXTURE_ASSERT %s\n",s);std::exit(1);}}
Digest hex(const char*s){Digest d{};for(unsigned i=0;i<32;i++){unsigned x;need(std::sscanf(s+2*i,"%2x",&x)==1,"digest");d[i]=x;}return d;}
Digest hash(vxnn::Bytes b){Digest d{};vxl_hunet_hash(b.data,b.size,d.data());return d;}
std::vector<uint8_t>read(const std::string&p){std::ifstream f(p,std::ios::binary);need(bool(f),"fixture");return {std::istreambuf_iterator<char>(f),{}};}
}
int main(int argc,char**argv){
 need(argc==3,"package and ID");auto source=import_result(std::string(argv[1])+"/source/__mosaic_result.json");need(bool(source),"original import");auto texture=make_texture(*source.value,{argv[1],hex(argv[2])});need(bool(texture),"original admitted package");auto t=*texture.value;auto assets=P8Assets::load("data/p8");need(bool(assets),"original bank");SceneState scene;scene.startup_remaining=0;scene.remaining=1;scene.theme=t->identity();
 auto custom=(*assets.value)->prepare_texture(scene,t,true);need(bool(custom),"admitted Mosaic texture prepares native frame");
 for(unsigned object=0;object<3;object++){
  scene.object=P8Object(object);scene.gallery=object==2;scene.pose=object==2?100:25;
  auto p=(*assets.value)->prepare_texture(scene,t,true);need(bool(p),"three objects custom texture");auto f=*p.value;need(f.texture==t&&f.scene.theme==t->identity(),"immutable bank and lineage retained");auto lod=t->lod(object==2?1:0);need(f.buffers[2].size()*4==lod.size&&!std::memcmp(f.buffers[2].data(),lod.data,lod.size),"exact native active LOD");
  auto original=scene;original.theme={};auto baseline=(*assets.value)->prepare(original,true);need(bool(baseline)&&baseline.value->input_sha!=f.input_sha,"texture bound into frame identity");need(baseline.value->buffers[0]!=f.buffers[0],"custom texture actually changes CPU reference");
  need(bool((*assets.value)->rebin(f,{reinterpret_cast<const uint8_t*>(f.buffers[4].data()),f.buffers[4].size()*4})),"actual transform rebin retains texture binding");
  auto changed=f;changed.buffers[2][0]^=1;need(!(*assets.value)->rebin(changed,{reinterpret_cast<const uint8_t*>(changed.buffers[4].data()),changed.buffers[4].size()*4}),"texture mutation before raster refused");
 }
 // A paused scene must redraw its displayed pose without consuming rotation.
 scene=SceneState{};scene.startup_remaining=0;scene.paused=true;scene.have_displayed=true;scene.displayed_pose=25;scene.pose=26;scene.remaining=7;scene.theme=t->identity();
 auto redraw=(*assets.value)->prepare_texture(scene,t,true,true);need(bool(redraw)&&redraw.value->scene.pose==25,"texture redraw uses currently displayed pose while paused");auto next=scene_after_frame(scene,25,true,100);need(bool(next)&&next.value->pose==26&&next.value->remaining==7&&next.value->paused,"redraw never advances animation");need(!(*assets.value)->prepare_texture(scene,t,true,false),"paused normal frame not due");auto wrong=scene;wrong.theme[0]^=1;need(!(*assets.value)->prepare_texture(wrong,t,true,true),"wrong package binding refused");need(!(*assets.value)->prepare_texture(scene,nullptr,true,true),"missing immutable bank refused");wrong=scene;wrong.startup_remaining=1;need(!(*assets.value)->prepare_texture(wrong,t,true,true),"startup redraw bypass refused");
 BackendIdentity hw;hw.device_generation=1;hw.provider_flags=2;hw.hardware_contract=hex("22343e02d30978412eeb245474fb2e272bf19fe1da08ec00c0e13e24f005b012");hw.bit=hex("faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc");hw.xsa=hex("08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536");std::string c="vortex,w4-graphics-p8-v1";hw.compatible=hash({reinterpret_cast<const uint8_t*>(c.data()),c.size()});auto raw=read("data/p8/programs/catalog.bin");auto catalog=InstalledCatalog::decode({raw.data(),raw.size()},hw,hash({raw.data(),raw.size()}));need(bool(catalog),"frozen catalog");DeviceCaps caps;caps.ps_base=0x10000000;caps.ps_bytes=1048576;caps.pl_base=0x500000000ULL;caps.pl_bytes=2147483648ULL;caps.ps_dma_bits=32;caps.gpu_address_bits=32;caps.pl_address_bits=40;caps.cycle_counter_bits=32;auto adapter=P8Adapter::create(*catalog.value,caps,*assets.value,vxnn::EvidenceScope::HOST_PROTOCOL_TEST);need(bool(adapter),"exact W4 adapter");auto plan=(*adapter.value)->prepare_texture_frame(scene,t,true);need(bool(plan)&&plan.value->total_jobs==2&&plan.value->programs[0].logical_program==9&&std::get<uint64_t>(plan.value->nodes[0].node.parameters.at("pose"))==25,"current pose and old program identities in plan");need(!(*adapter.value)->prepare_frame(scene),"unbound legacy entry cannot accept texture");need(t->source_scope()==vxnn::EvidenceScope::OFFLINE,"display does not upgrade original Mosaic scope");
 puts("U17_P8_DUAL_LOD_THEME_CURRENT_POSE_AND_PLAN_PASS_CPU_ONLY_NOT_GPU_DP gpu_raw_cycles=null");
}
