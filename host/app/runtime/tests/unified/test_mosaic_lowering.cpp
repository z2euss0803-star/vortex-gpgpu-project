#include <vxnn/model_mosaic.hpp>
#include <vxnn/mosaic_lowering.hpp>
#include <vxnn/mosaic_coverage.hpp>
#include <vxunified/backend.hpp>
#include "backend_bridge.h"
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace vxnn;
extern "C" int mosaic_prepare_alias_probe(const uint64_t[8],const uint64_t[8],const uint64_t[8],const uint32_t[8],unsigned*);
namespace {
void need(bool yes,const char*why){if(!yes){std::fprintf(stderr,"U13_MOSAIC_LOWER_ASSERT %s\n",why);std::exit(82);}}
struct Transport:vxunified::IoctlTransport {void*bridge;unsigned calls=0;explicit Transport(const char*p):bridge(backend_bridge_create(p)){}~Transport(){backend_bridge_destroy(bridge);}int call(unsigned long c,void*p)override{calls++;return backend_bridge_call(bridge,c,p);}uint64_t now_ns()override{return 100;}};
}
int main(int argc,char**argv){
 need(argc==3,"frozen small model and actual C allocator fixture");auto loaded=load_package(Path(argv[1]));need(bool(loaded),"secure model package");const auto&package=*loaded.value;const auto&raw=*package.assets().at("binding.json").content;auto binding=json::parse(std::string(raw.begin(),raw.end()));auto*b=binding.get();
 MosaicFamily family{json::digest(json::member(b,"code_sha256")),json::digest(json::member(b,"readonly_sha256")),json::digest(json::member(b,"schema_sha256")),json::digest(json::member(b,"implementation_sha256"))};OperatorRegistry registry;need(bool(register_mosaic_operators(registry,family)),"source-bound six operators");vxunified::BackendIdentity hw;auto*h=json::member(b,"hardware");hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=1;hw.device_generation=1;
 Transport io(argv[2]);vxunified::ComputeV2Backend backend(io);auto caps=backend.capabilities();need(bool(caps),"actual C CAPS");auto plan_result=prepare_mosaic(package,registry,*caps.value,hw);need(bool(plan_result),"complete frozen77 phase model");const auto&plan=*plan_result.value;
 MemoryBudget budget;budget.pl_bytes=caps.value->pl_bytes;budget.ps_bytes=caps.value->ps_bytes;budget.pl_reserved=8<<20;auto arenas=plan_arenas(plan,budget);need(bool(arenas),"all exact tensor/workspace guards fit");auto created=ArenaStorage::create(plan,*arenas.value,1,backend);need(bool(created),"owned storage metadata");auto storage=*created.value;need(!lower_mosaic_batch(plan,family,*storage,1,0),"no refs before actual allocation");const auto base=backend_bridge_live(io.bridge);need(bool(storage->allocate()),"actual C data root");const auto&bound=*storage->bindings();auto calls=io.calls;uint64_t jobs=0;
 const uint32_t access[8]={1,2,3,1,1,3,3,3};
 for(const auto&pn:plan.nodes){
  uint32_t op=0;for(unsigned v=1;v<=6;v++){auto k=mosaic_operator_key(v,family);if(!(k<pn.node.key)&&!(pn.node.key<k))op=v;}need(op,"registered operator identity");
  std::vector<unsigned>inputs=op==1?std::vector<unsigned>{VXM_INPUT}:op==2?std::vector<unsigned>{VXM_QUANT,VXM_WEIGHTS,VXM_COEFF}:op==4?std::vector<unsigned>{VXM_INPUT,VXM_COEFF,VXM_STATS}:op==6?std::vector<unsigned>{VXM_INPUT,VXM_SKIP}:std::vector<unsigned>{VXM_INPUT};
  std::vector<unsigned>outputs=op==1?(pn.node.outputs.size()==2?std::vector<unsigned>{VXM_QUANT,VXM_SKIP}:std::vector<unsigned>{VXM_QUANT}):op==3?std::vector<unsigned>{VXM_STATS}:std::vector<unsigned>{VXM_OUTPUT};
  for(uint64_t index=0;index<pn.job_count;index++){
   auto lowered=lower_mosaic_batch(plan,family,*storage,pn.node.id,index);if(!lowered)std::fprintf(stderr,"LOWER %s\n",lowered.error->detail.c_str());need(bool(lowered),"each bounded batch lowers");const auto&v=*lowered.value;need(v.node==pn.node.id&&v.batch==index,"node/batch identity");VxnnMosaicShape shape{};need(!vxnn_mosaic_validate(&v.args,false,&shape),"canonical zero-address private packet");const uint64_t budget=std::get<uint64_t>(pn.node.parameters.at("budget"));need(v.args.first==index*budget&&v.args.count==std::min<uint64_t>(budget,shape.items-v.args.first),"global first and exact tail count");
   for(unsigned role=0;role<8;role++){const auto&r=v.references[role];need(r.handle==storage->receipt().root->handle&&r.object_generation==storage->receipt().root->object_generation&&r.device_generation==1&&r.offset>=(8u<<20)&&r.bytes==v.args.extent[role]&&r.access==access[role],"actual bounded owned root and exact schema permissions");
    for(unsigned other=0;other<8;other++)if(r.access==1&&(v.references[other].access&2)){const auto&w=v.references[other];need(!(r.offset<w.offset+w.bytes&&w.offset<r.offset+r.bytes),"ALL readonly refs including inactive slots disjoint from every writable ref");}
   }
   for(size_t i=0;i<inputs.size();i++){need(v.references[inputs[i]].offset==bound.tensors.at(pn.node.inputs[i]).offset,"declared input-role mapping");}for(size_t i=0;i<outputs.size();i++){need(v.references[outputs[i]].offset==bound.tensors.at(pn.node.outputs[i]).offset,"declared output-role mapping");}need(v.references[7].offset==bound.workspaces.at(pn.node.id).offset,"full per-core owned workspace");
   uint64_t handles[8],offsets[8],lengths[8];for(unsigned role=0;role<8;role++){handles[role]=v.references[role].handle;offsets[role]=v.references[role].offset;lengths[role]=v.references[role].bytes;}unsigned guards=0;
   need(mosaic_prepare_alias_probe(handles,offsets,lengths,access,&guards)==-EAGAIN&&guards==1,"actual production C prepare accepts all lowered refs through alias gate and deliberately stops at guard");
   unsigned inactive_ro=8;for(unsigned role:{0u,3u,4u})if(!(shape.used_mask&(1u<<role)))inactive_ro=role;if(inactive_ro<8){offsets[inactive_ro]=offsets[VXM_WORK];need(mosaic_prepare_alias_probe(handles,offsets,lengths,access,&guards)==-EINVAL&&!guards,"original inactive-RO/WORK alias still rejected before guard by unchanged production gate");}
   // Only a unit-test proof of offset geometry: no kernel argument install,
   // address query, I/O, CDMA or actual device execution occurs here.
   auto relocated=v.args;for(unsigned role=0;role<8;role++)relocated.address[role]=uint32_t(0x80000000ULL+v.references[role].offset);need(!vxnn_mosaic_validate(&relocated,true,&shape),"typed reference geometry agrees with target disjointness and range contract");jobs++;
  }
  need(!lower_mosaic_batch(plan,family,*storage,pn.node.id,pn.job_count),"batch tail refused before multiplication");
 }
 need(jobs==plan.total_jobs&&jobs==547&&io.calls==calls&&!backend_bridge_starts(io.bridge),"all547 pure batches perform no ioctl/start");
 auto bad=plan;bad.plan_id[0]^=1;need(!lower_mosaic_batch(bad,family,*storage,1,0),"wrong arena plan");bad=plan;bad.policy.hardware.device_generation=2;need(!lower_mosaic_batch(bad,family,*storage,1,0),"stale epoch");bad=plan;bad.policy.hardware.provider_flags=2;need(!lower_mosaic_batch(bad,family,*storage,1,0),"P8 mismatch");bad=plan;bad.programs[0].logical_program=12;need(!lower_mosaic_batch(bad,family,*storage,1,0),"old program not reused");
 auto wrong=family;wrong.readonly_sha[0]^=1;need(!lower_mosaic_batch(plan,wrong,*storage,1,0),"readonly identity");wrong=family;wrong.implementation_sha[0]^=1;need(!lower_mosaic_batch(plan,wrong,*storage,1,0),"host operator identity");bad=plan;bad.nodes[0].node.outputs[0]=bad.nodes[0].node.inputs[0];need(!lower_mosaic_batch(bad,family,*storage,1,0),"unplanned writable alias");bad=plan;bad.nodes[0].job_count++;need(!lower_mosaic_batch(bad,family,*storage,1,0),"declared job count");
 need(bool(storage->release())&&backend_bridge_live(io.bridge)==base,"explicit exact root release");need(!lower_mosaic_batch(plan,family,*storage,1,0),"retired storage cannot lower");std::puts("U13_MOSAIC_547_BATCH_TYPED_REFS_LOWERING_PASS_HOST_NO_GPU gpu_raw_cycles=null");
}
