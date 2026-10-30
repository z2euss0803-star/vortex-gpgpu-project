#include <vxnn/model_mosaic.hpp>
#include <vxnn/mosaic_coverage.hpp>
#include <vxnn/arena.hpp>
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <cstdlib>
using namespace vxnn;
static void need(bool v,const char*s){if(!v){std::fprintf(stderr,"U13_MOSAIC_MODEL_ASSERT %s\n",s);std::exit(81);}}
int main(int argc,char**argv){
 need(argc==2||argc==3,"immutable actual weight package and optional rejection mode");auto loaded=load_package(Path(argv[1]));if(!loaded)std::fprintf(stderr,"LOAD %s\n",loaded.error->detail.c_str());need(bool(loaded),"real immutable package loader");const auto&p=*loaded.value;
 auto&raw=*p.assets().at("binding.json").content;auto binding=json::parse(std::string(raw.begin(),raw.end()));auto*b=binding.get();MosaicFamily f{json::digest(json::member(b,"code_sha256")),json::digest(json::member(b,"readonly_sha256")),json::digest(json::member(b,"schema_sha256")),json::digest(json::member(b,"implementation_sha256"))};OperatorRegistry registry;need(bool(register_mosaic_operators(registry,f)),"source/CODE/RO/schema registration");
 vxunified::BackendIdentity hw;auto*h=json::member(b,"hardware");hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=1;hw.device_generation=1;
 DeviceCaps caps;caps.ps_base=0x10000000;caps.ps_bytes=1<<20;caps.pl_base=0x80000000;caps.pl_bytes=2ULL<<30;caps.gpu_address_bits=caps.ps_dma_bits=caps.cycle_counter_bits=32;caps.pl_address_bits=40;
 OptimizationPolicy policy;policy.hardware=hw;auto generic=prepare_graph(p,registry,caps,policy);need(bool(generic),"package has a valid generic SSA graph before model-specific gate");
 need(!prepare_mosaic(p,registry,caps),"package cannot invent hardware identity");auto prepared=prepare_mosaic(p,registry,caps,hw);if(!prepared)std::fprintf(stderr,"PREPARE %s\n",prepared.error->detail.c_str());
 if(argc==3){need(!prepared,"semantically refrozen malicious model must fail");std::puts("U13_MOSAIC_MODEL_SEMANTIC_REJECTION_PASS");return 0;}
 need(bool(prepared),"complete original16-layer model gate");const auto&plan=*prepared.value;need(plan.nodes.size()==77&&plan.tensors.size()==130&&plan.inputs.size()==1&&plan.outputs.size()==1,"77 SSA phases and5 separate residual saves plus47 immutable constants");
 uint64_t width=json::u64(json::member(b,"width")),height=json::u64(json::member(b,"height")),budget=json::u64(json::member(b,"budget")),jobs=0;std::map<TensorId,const TensorDesc*> tensors;unsigned constants=0;
 for(const auto&t:plan.tensors){tensors[t.desc.id]=&t.desc;if(t.desc.constant)constants++;}
 for(unsigned i=0;i<77;i++){auto phase=describe_mosaic_phase(width,height,i);need(bool(phase)&&plan.nodes[i].job_count==(phase.value->items+budget-1)/budget,"exact lazy batch cardinality for each frozen phase");jobs+=plan.nodes[i].job_count;}
 need(constants==47&&jobs==plan.total_jobs&&tensors.at(plan.outputs[0])->shape==Shape{3,height,width}&&tensors.at(plan.outputs[0])->bytes==width*height*12,"full native resolution and exact constants/jobs");
 MemoryBudget memory;memory.ps_bytes=caps.ps_bytes;memory.pl_bytes=caps.pl_bytes;memory.pl_reserved=8u<<20;auto arena=plan_arenas(plan,memory);need(arena&&arena.value->object_count==1&&arena.value->pl_peak<(2ULL<<30),"all typed tensors/workspaces and guards fit one checked arena, not130 public objects");
 auto n=json::parse(p.numeric_contract_json());need(json::str(json::member(n.get(),"semantics"))=="MOSAIC_W16A16_EXACT48_FP32_NORM_V3_FROZEN_THRESHOLDS"&&json::count(json::member(n.get(),"known_failures"))==2,"thresholds and first watchdog/history retained");
 for(unsigned i=0;i<6;i++){auto bad=hw;auto c=caps;if(i==0)bad.bit[0]^=1;if(i==1)bad.provider_flags=2;if(i==2)bad.device_generation=0;if(i==3)c.preempt=true;if(i==4)c.max_refs_per_job=7;if(i==5)c.gpu_address_bits=64;need(!prepare_mosaic(p,registry,c,bad),"wrong hardware/provider/capabilities rejected");}
 OperatorRegistry empty;need(!prepare_mosaic(p,empty,caps,hw),"no missing operator fallback");
 std::printf("U13_MOSAIC_16_LAYER77_PHASE_MODEL_PREPARE_PASS_NOT_EXECUTION width=%llu height=%llu jobs=%llu arena=%llu gpu_raw_cycles=null\n",(unsigned long long)width,(unsigned long long)height,(unsigned long long)jobs,(unsigned long long)arena.value->pl_peak);
}
