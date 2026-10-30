#include <vxnn/model_hunet.hpp>
#include <vxnn/hunet_lowering.hpp>
#include <fstream>
#include <cstring>
#include <vxnn/arena_storage.hpp>
#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
namespace {
void need(bool yes,const char*s){if(!yes){std::fprintf(stderr,"U12_LOWERING_ASSERT %s\n",s);std::exit(71);}}
struct Transport:vxunified::IoctlTransport {
 void*bridge;explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}
 int call(unsigned long c,void*p)override{return backend_bridge_call(bridge,c,p);}uint64_t now_ns()override{return 100;}
};
}
int main(int argc,char**argv){
 need(argc==3,"actual package and C catalog fixture");auto loaded=load_package(Path(argv[1]));need(bool(loaded),"secure frozen package");const auto&package=*loaded.value;
 const auto&raw=*package.assets().at("binding.json").content;auto binding=json::parse(std::string(raw.begin(),raw.end()));auto*b=binding.get();
 HunetFamily family{json::digest(json::member(b,"code_sha256")),json::digest(json::member(b,"implementation_sha256")),json::digest(json::member(b,"schema_sha256"))};OperatorRegistry registry;need(bool(register_hunet_operators(registry,family)),"actual operator registry");
 vxunified::BackendIdentity hw;auto*h=json::member(b,"hardware");hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=1;hw.device_generation=1;
 Transport io(argv[2]);vxunified::ComputeV2Backend backend(io);auto actual_caps=backend.capabilities();need(bool(actual_caps),"actual C CAPS physical pool domains");DeviceCaps caps=*actual_caps.value;
 auto prepared=prepare_hunet(package,registry,caps,hw);need(bool(prepared),"actual116-node plan");const auto&plan=*prepared.value;MemoryBudget budget;budget.pl_bytes=caps.pl_bytes;budget.ps_bytes=caps.ps_bytes;budget.pl_reserved=8<<20;
 auto planned=plan_arenas(plan,budget);need(bool(planned),"complete guarded memory plan");auto storage_result=ArenaStorage::create(plan,*planned.value,1,backend);need(bool(storage_result),"full graph storage metadata");auto storage=*storage_result.value;const auto baseline_live=backend_bridge_live(io.bridge);need(bool(storage->allocate()),"actual full data root with internal prefix");const auto&arena=storage->layout();const auto&owned=storage->allocations();
 auto resolved=bind_arenas(arena,owned,1);need(bool(resolved),"actual C handles, not invented references");const auto&refs=*resolved.value;

 need(!lower_hunet_node(plan,family,*storage,0),"unknown node rejected");
 std::ifstream old("generated/hunet_fused/jobs.bin",std::ios::binary);need(bool(old),"independent frozen128B jobs");
 unsigned gpu=0,cpu=0;
 for(const auto&pn:plan.nodes){
  auto lowered=lower_hunet_node(plan,family,*storage,pn.node.id);
  if(!pn.job_count){need(!lowered,"CPU GELU cannot lower to GPU");++cpu;continue;}
  if(!lowered)std::fprintf(stderr,"LOWER %s\n",lowered.error->detail.c_str());
  need(bool(lowered),"trusted GPU lowering must succeed");const auto&v=*lowered.value;
  uint32_t words[32]{};old.read(reinterpret_cast<char*>(words),128);need(old.gcount()==128,"one old job per GPU node");
  // Independently translate the OLD scalar/fused ABI, not the registry or
  // graph parameters under test. Historical fixed pointers never reach refs.
  VxnnHunetArgs golden{};golden.abi=1;golden.bytes=128;golden.op=words[3];
  uint32_t shape[12]{};
  if(words[3]==15){for(unsigned i=0;i<9;i++)shape[i]=words[18+i];golden.flags=words[27]|(words[10]?16:0);}
  else {for(unsigned i=0;i<7;i++)shape[i]=words[16+i];for(unsigned i=0;i<5;i++)shape[7+i]=words[24+i];}
  std::memcpy(&golden.ci,shape,sizeof(shape));uint64_t nx=uint64_t(shape[0])*shape[2]*shape[3],ny=uint64_t(shape[1])*shape[4]*shape[5];
  for(unsigned i=0;i<6;i++)golden.extent[i]=nx*4;
  golden.extent[6]=ny*4;golden.extent[7]=16;
  if(words[3]==15){
   std::ifstream ds("generated/hunet_fused/descriptors.bin",std::ios::binary);ds.seekg(words[14]-0x86d0df80u);uint64_t packed=0,tiles=0;
   for(unsigned i=0;i<words[7];i++){HunetFusedDescriptorV1 d{};ds.read(reinterpret_cast<char*>(&d),64);need(ds.gcount()==64,"old descriptor bytes");if(!i){golden.extent[1]=d.mp*d.kp*2;if(words[10])golden.extent[2]=d.m*4;}packed+=uint64_t(d.kp)*d.np*2;tiles+=uint64_t(d.mp/8)*(d.np/4);}
   golden.extent[5]=words[7]*64;golden.extent[7]=((packed+63)/64)*64+tiles*4;
  }
  if(words[3]==4)golden.extent[6]=8;
  if(words[3]==5){golden.extent[3]=golden.extent[4]=shape[0]*4;golden.extent[5]=8;}
  if(words[3]==8)golden.extent[1]=ny*4;
  if(words[3]==11)golden.extent[1]=shape[1]*4;
  if(words[3]==13)golden.extent[1]=(shape[1]-shape[0])*shape[2]*shape[3]*4;
  need(!std::memcmp(&golden,&v.args,128),"103 new128B packets match independently decoded original scalar/fused jobs");
  need(v.node==pn.node.id,"node identity");
  for(unsigned k=0;k<8;k++){
   const auto&bref=v.references[k];need(bref.handle==storage->receipt().root->handle&&bref.object_generation==storage->receipt().root->object_generation&&bref.device_generation==1,"actual owned handle/generation");
   need(bref.bytes==v.args.extent[k]&&bref.offset>=(8u<<20)&&bref.offset%4==0,"exact byte extents and bounded relative offsets");
   need(bref.access==(k==6?2u:(k==7?3u:1u)),"typed minimal access");
   if(k<6){bool found=false;for(auto id:pn.node.inputs){const auto&q=refs.tensors.at(id);found|=q.offset==bref.offset&&q.bytes==bref.bytes;}need(found,"read role comes from declared operand");}
  }
  need(v.references[6].offset==refs.tensors.at(pn.node.outputs[0]).offset&&v.references[7].offset==refs.workspaces.at(pn.node.id).offset,"Y/work owned planned views");++gpu;
 }
 need(old.peek()==std::char_traits<char>::eof()&&gpu==103&&cpu==13,"full frozen job count, no trailing input");
 auto bad=plan;bad.plan_id[0]^=1;need(!lower_hunet_node(bad,family,*storage,plan.nodes[0].node.id),"plan identity mismatch");
 bad=plan;bad.policy.hardware.device_generation=2;need(!lower_hunet_node(bad,family,*storage,plan.nodes[0].node.id),"stale epoch");
 bad=plan;bad.policy.hardware.provider_flags=2;need(!lower_hunet_node(bad,family,*storage,plan.nodes[0].node.id),"P8 cannot reuse S8");
 bad=plan;bad.programs[0].logical_program=9;need(!lower_hunet_node(bad,family,*storage,plan.nodes[0].node.id),"legacy program never inferred");
 auto wrong=family;wrong.program_sha[0]^=1;need(!lower_hunet_node(plan,wrong,*storage,plan.nodes[0].node.id),"wrong code identity");
 wrong=family;wrong.implementation_sha[0]^=1;need(!lower_hunet_node(plan,wrong,*storage,plan.nodes[0].node.id),"wrong host operator identity");
 bad=plan;bad.nodes[0].node.outputs[0]=bad.nodes[0].node.inputs[0];need(!lower_hunet_node(bad,family,*storage,bad.nodes[0].node.id),"no writable alias");
 bad=plan;bad.nodes[0].node.parameters["descriptors"]=std::string(128,'0');need(!lower_hunet_node(bad,family,*storage,bad.nodes[0].node.id),"descriptor mismatch");
 bad=plan;bad.tensors[0].desc.bytes+=4;need(!lower_hunet_node(bad,family,*storage,plan.nodes[0].node.id),"tensor extent mismatch");
 need(backend_bridge_starts(io.bridge)==0,"pure lowering never starts GPU");need(bool(storage->release()),"explicit root release");
 need(!lower_hunet_node(plan,family,*storage,plan.nodes[0].node.id),"released storage cannot produce refs");
 need(backend_bridge_live(io.bridge)==baseline_live,"no leaked test objects");
 puts("U12_HUNET_LOWERING_103_FROZEN_PACKETS_13_CPU_REJECTIONS_PASS_HOST_NO_GPU gpu_raw_cycles=null");
}
