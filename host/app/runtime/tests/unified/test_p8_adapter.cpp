#include <vxunified/p8.hpp>
#include <vxl/hunet.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
using namespace vxunified;
static void check(bool b,const char*s){if(!b){std::fprintf(stderr,"U16_P8_ADAPTER_ASSERT %s\n",s);std::exit(1);}}
static Digest hex(const char*s){Digest d{};for(size_t i=0;i<32;i++){unsigned x=0;check(std::sscanf(s+2*i,"%2x",&x)==1,"hex literal");d[i]=x;}return d;}
static Digest hash(const std::vector<uint8_t>&v){Digest d{};vxl_hunet_hash(v.data(),v.size(),d.data());return d;}
static std::vector<uint8_t>read(const std::string&p){std::ifstream f(p,std::ios::binary);check(bool(f),"frozen catalog readable");return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char**argv){
 check(argc==2,"asset directory");auto bank=P8Assets::load(argv[1]);check(bool(bank),"immutable bank load");
 BackendIdentity hw;hw.device_generation=1;hw.provider_flags=2;hw.hardware_contract=hex("22343e02d30978412eeb245474fb2e272bf19fe1da08ec00c0e13e24f005b012");hw.bit=hex("faeaddb98d5d39a4e787c6b2aaf559d6a6163191a1cc8eb18331624b896290dc");hw.xsa=hex("08183adeb93ec2ffe3c8c5643813daae12365f61efb95a0d985f04660c340536");std::string compatible="vortex,w4-graphics-p8-v1";vxl_hunet_hash(compatible.data(),compatible.size(),hw.compatible.data());
 auto raw=read(std::string(argv[1])+"/programs/catalog.bin");auto sum=hash(raw);check(sum==hex("25fd3c002e0ad6d6c218b1f9ee4719fdaead7aeacb513cf6be73ecd1cc3acd79"),"independent frozen schema catalog SHA");auto catalog=InstalledCatalog::decode({raw.data(),raw.size()},hw,sum);check(bool(catalog)&&(*catalog.value)->size()==4,"real C W4 catalog checks");
 DeviceCaps caps;caps.ps_base=0x10000000;caps.ps_bytes=1048576;caps.pl_base=0x500000000ULL;caps.pl_bytes=2147483648ULL;caps.ps_dma_bits=32;caps.gpu_address_bits=32;caps.pl_address_bits=40;caps.cycle_counter_bits=32;
 auto adapter=P8Adapter::create(*catalog.value,caps,*bank.value,vxnn::EvidenceScope::HOST_PROTOCOL_TEST);check(bool(adapter),"exact P8 adapter admission");
 for(unsigned i=0;i<3;i++){SceneState s;s.object=P8Object(i);s.startup_remaining=0;s.remaining=1;s.gallery=true;auto plan=(*adapter.value)->prepare_frame(s);check(bool(plan)&&plan.value->total_jobs==2&&plan.value->nodes.size()==4&&plan.value->programs.size()==2,"transform CPU bins raster transfer logical chain");check(plan.value->programs[0].namespace_id==2&&plan.value->programs[0].logical_profile==5&&plan.value->programs[0].logical_program==(i==2?11u:9u)&&plan.value->programs[1].logical_program==(i==2?12u:10u),"W4 full keys and original program IDs");}
 auto insufficient=caps;insufficient.max_refs_per_job=6;check(!P8Adapter::create(*catalog.value,insufficient,*bank.value,vxnn::EvidenceScope::HOST_PROTOCOL_TEST),"seven data refs separate args mandatory");check(!P8Adapter::create(*catalog.value,caps,*bank.value,vxnn::EvidenceScope::REAL_BOARD),"unverified boot cannot claim board");SceneState invalid;invalid.object=P8Object::TERRACOTTA;invalid.pose=1;check(!(*adapter.value)->prepare_frame(invalid),"warrior only eight frozen fixed poses");invalid.pose=0;invalid.theme[0]=1;check(!(*adapter.value)->prepare_frame(invalid),"unbound texture identity refused");
 SceneState paused;paused.startup_remaining=0;paused.paused=true;check(!(*adapter.value)->prepare_frame(paused),"paused frame cannot be admitted");
 puts("U16_P8_CATALOG_AND_PLAN_PASS_NOT_GPU_DP gpu_raw_cycles=null");
}
