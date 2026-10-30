#include <vxnn/model_hunet.hpp>
#include "nnrt/package/json.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace vxnn;
static void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U12_MODEL_ASSERT %s\n",s);std::exit(69);}}
static const VerifiedAsset&asset(const ModelPackage&p,const char*n){auto it=p.assets().find(n);need(it!=p.assets().end(),"asset exists");return it->second;}
int main(int argc,char**argv){
 need(argc==2||argc==3,"readonly actual weight package path and optional semantic-reject mode");auto loaded=load_package(Path(argv[1]));if(!loaded)std::fprintf(stderr,"LOAD %s\n",loaded.error->detail.c_str());need(bool(loaded),"secure real package loader");const auto&p=*loaded.value;
 auto&raw=*asset(p,"binding.json").content;auto binding=json::parse(std::string(raw.begin(),raw.end()));auto*b=binding.get();HunetFamily family{json::digest(json::member(b,"code_sha256")),json::digest(json::member(b,"implementation_sha256")),json::digest(json::member(b,"schema_sha256"))};OperatorRegistry registry;need(bool(register_hunet_operators(registry,family)),"actual source-bound registration");
 vxunified::BackendIdentity hw;auto*h=json::member(b,"hardware");hw.hardware_contract=json::digest(json::member(h,"hardware_contract"));hw.bit=json::digest(json::member(h,"bit_sha256"));hw.xsa=json::digest(json::member(h,"xsa_sha256"));hw.compatible=content_digest(json::str(json::member(h,"compatible")));hw.provider_flags=1;hw.device_generation=1;
 DeviceCaps caps;caps.ps_base=0x10000000;caps.ps_bytes=1<<20;caps.pl_base=0x80000000;caps.pl_bytes=2ULL<<30;caps.gpu_address_bits=caps.ps_dma_bits=caps.cycle_counter_bits=32;caps.pl_address_bits=40;
 need(!prepare_hunet(p,registry,caps),"no package-invented cached hardware identity");auto prepared=prepare_hunet(p,registry,caps,hw);if(!prepared)std::fprintf(stderr,"PREPARE %d %s\n",prepared.error->code,prepared.error->detail.c_str());if(argc==3){need(!std::strcmp(argv[2],"semantic-reject")&&!prepared,"valid immutable malformed package rejected semantically");puts("U12_MODEL_SEMANTIC_REJECT_PASS");return 0;}need(bool(prepared),"complete actual frozen graph prepares");const auto&plan=*prepared.value;
 need(plan.nodes.size()==116&&plan.total_jobs==103&&plan.programs.size()==1&&plan.programs[0].logical_program==65552,"116 nodes are103 GPU jobs and13 CPU nodes");need(!plan.policy.hardware.board_identity_verified,"offline preparation is not board identity pass");
 std::map<TensorId,const TensorDesc*> tensors;for(const auto&t:plan.tensors)tensors.emplace(t.desc.id,&t.desc);std::set<TensorId> ready(plan.inputs.begin(),plan.inputs.end());for(const auto&t:plan.tensors)if(t.desc.constant)ready.insert(t.desc.id);
 unsigned cpu=0,gpu=0,stats=0,constants=0;for(const auto&t:plan.tensors){if(t.desc.constant)constants++;if(t.desc.layout=="GN_MEAN_INVSTD_F32")stats++;}
 for(const auto&n:plan.nodes){for(auto id:n.node.inputs)need(ready.count(id),"each producer completes before dependent GPU or CPU");bool gelu=n.node.key.name=="Hunet.GeluErf4.v1";if(gelu){cpu++;need(n.job_count==0&&n.node.program_requirements.empty()&&n.workspace_bytes==0,"CPU commit boundary not GPU work");}else{gpu++;need(n.job_count==1&&n.node.program_requirements==std::vector<Digest>{family.program_sha},"GPU one bounded job");}for(auto id:n.node.outputs)need(ready.insert(id).second,"no reused GN scratch tensor identities");}
 need(cpu==13&&gpu==103&&stats==16&&constants==119,"frozen graph counts and SSA statistics");need(tensors.at(plan.outputs[0])->bytes==8105*4,"unpadded actual final output");
 need(digest_hex(asset(p,"weights-original.bin").sha)=="674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc","actual original weights unchanged");need(asset(p,"reference.f32").content->size()==8105*4,"historical output retained");need(!json::boolean(json::member(b,"diagnostics")),"diagnostics default false");
 for(unsigned i=0;i<7;i++){auto bad=hw;auto c=caps;if(i==0)bad.bit[0]^=1;if(i==1)bad.device_generation=0;if(i==2)bad.provider_flags=2;if(i==3)c.preempt=true;if(i==4)c.max_refs_per_job=7;if(i==5)c.max_args_bytes=64;if(i==6)c.cycle_counter_bits=64;need(!prepare_hunet(p,registry,c,bad),"wrong cached hardware/capability fails closed");}
 OperatorRegistry empty;need(!prepare_hunet(p,empty,caps,hw),"no unregistered fallback");
 puts("U12_FULL_REAL_WEIGHT_116_NODE_GRAPH_PREPARE_PASS_NOT_EXECUTION gpu_raw_cycles=null");
}
