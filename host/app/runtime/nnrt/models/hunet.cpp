#include <vxnn/model_hunet.hpp>
#include "../package/json.hpp"
#include <cerrno>
#include <cstring>
#include <set>
namespace vxnn {
Result<Plan> prepare_hunet(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&){return Result<Plan>::failure({EINVAL,"HUNET_PLAN","explicit cached backend identity required"});}
Result<Plan> prepare_hunet(const ModelPackage&package,const OperatorRegistry&registry,const DeviceCaps&caps,const vxunified::BackendIdentity&hardware){
 try{
  json::check(vxunified::s8_provider(hardware)&&hardware.device_generation&&caps.gpu_address_bits==32&&caps.ps_dma_bits==32&&caps.pl_address_bits==40&&caps.cycle_counter_bits==32&&caps.max_args_bytes>=128&&caps.max_refs_per_job==8&&caps.max_program_bytes>=65536,"explicit S8 cached identity and eight-ref capabilities required");
  auto asset=[&](const char*name,const char*role,const char*hash)->const VerifiedAsset&{auto it=package.assets().find(name);json::check(it!=package.assets().end()&&it->second.type=="binary"&&it->second.role==role&&digest_hex(it->second.sha)==hash,"frozen historical asset identity");return it->second;};
  const auto&weights=asset("weights-original.bin","provenance-source","674069f415d51ce502dd00e579125c0ca8fe6d7e71a71d08cfae457e293e39bc");
  const auto&history=asset("history.raw","historical-numerics","9311f7071cf3b34e44d77f7bf68f597453e8ff68ce7484a84f4dcc7504fad9e5");
  const auto&reference=asset("reference.f32","reference","58a75990aad6711810065fa7a3c83e81a3eaaba13769f0395eac95dd7fa48654");
  asset("input.f32","input","de5513346d7922032e415b2d5eb40a5862b084a2ed264b73602ab8f00769f0de");
  auto numeric=json::parse(package.numeric_contract_json());auto*n=numeric.get();json::fields(n,{"schema","reference_sha","threshold_sha","semantics","known_failures"});
  json::check(json::u64(json::member(n,"schema"))==1&&json::str(json::member(n,"semantics"))=="HUNET_V5_ORDERED_FP16_FP32_ERF4_FROZEN_HISTORY_V1"&&json::digest(json::member(n,"reference_sha"))==reference.sha&&json::digest(json::member(n,"threshold_sha"))==history.sha,"reference, thresholds and historical failure cannot be waived");
  auto*known=json::member(n,"known_failures");json::check(json::count(known)==1&&json::str(json::at(known,0))=="HISTORICAL_ORT_8105_NOT_NEW_PASS","known ORT failure retained");
  std::set<NodeId>stats_cpu;const std::set<NodeId>allowed_stats={2,9,19,25,32,42,52,55,62,69,76,83,90,97,100,107};
  const auto&binding_bytes=*package.assets().at("binding.json").content;auto binding=json::parse(std::string(binding_bytes.begin(),binding_bytes.end()));
  if(json::has(binding.get(),"cpu_gn_stats_nodes")){auto*ids=json::member(binding.get(),"cpu_gn_stats_nodes");for(size_t i=0;i<json::count(ids,16);i++){auto id=json::u32(json::at(ids,i));json::check(allowed_stats.count(id)&&stats_cpu.insert(id).second,"explicit unique frozen CPU GN_STATS subset");}}
  OptimizationPolicy policy;policy.hardware=hardware;auto result=prepare_graph(package,registry,caps,std::move(policy));if(!result)return result;
  auto&p=*result.value;json::check(p.programs.size()==1&&p.programs[0].namespace_id==1&&p.programs[0].logical_profile==1&&p.programs[0].logical_program==65552&&digest_hex(p.programs[0].schema)=="79319f1611265bc2ad131b91642de7ddbe0c30d4b3621ce3a8060e90e1114c89","only controlled new HUNet program and sealed schema");
  json::check(p.nodes.size()==116&&p.total_jobs==103-stats_cpu.size()&&p.inputs.size()==1&&p.outputs.size()==1,"frozen HUNet graph dimensions");
  unsigned cpu=0,constants=0,weight_views=0;
  for(const auto&node:p.nodes){bool gelu=node.node.key.name=="Hunet.GeluErf4.v1",stats=node.node.key.name=="Hunet.GnStatsCpuOrdered.v1";json::check(stats==bool(stats_cpu.count(node.node.id)),"CPU stats node identity must match declared subset");json::check(node.node.key.name.compare(0,6,"Hunet.")==0&&node.node.key.semantic_version=="1.0.0","HUNet family only");json::check(node.job_count==((gelu||stats)?0u:1u)&&node.node.program_requirements==((gelu||stats)?std::vector<Digest>{}:std::vector<Digest>{p.programs[0].code}),"CPU is a dependency boundary, never a GPU job or GPU program requirement");if(gelu||stats)cpu++;}
  for(const auto&t:p.tensors){if(t.desc.id==p.outputs[0])json::check(t.desc.dtype==DType::FP32&&t.desc.layout=="CHW_F32"&&t.desc.shape==Shape{8105,1,1}&&t.desc.bytes==32420,"actual unpadded final logits");if(!t.desc.constant)continue;constants++;auto it=package.assets().find(t.desc.asset);json::check(it!=package.assets().end(),"constant asset");const auto&a=it->second;
   if(a.path.compare(0,7,"weight-")==0){json::check(a.path.size()>20&&a.path.substr(15,1)=="-"&&a.path.substr(a.path.size()-4)==".bin","weight view path encoding");uint64_t offset=0;for(size_t i=7;i<15;i++){char c=a.path[i];json::check((c>='0'&&c<='9')||(c>='a'&&c<='f'),"weight offset hex");offset=offset*16+unsigned(c<='9'?c-'0':c-'a'+10);}auto count=a.path.substr(16,a.path.size()-20);json::check(!count.empty()&&count[0]!='0'&&count.size()<=10,"weight byte extent encoding");uint64_t bytes=0;for(char c:count){json::check(c>='0'&&c<='9',"weight byte extent");bytes=bytes*10+unsigned(c-'0');}json::check(!(offset%64)&&bytes==a.content->size()&&offset<=weights.content->size()&&bytes<=weights.content->size()-offset&&std::memcmp(a.content->data(),weights.content->data()+offset,bytes)==0,"actual weight slice must equal original frozen bytes");weight_views++;
   }else json::check(a.path.compare(0,5,"desc-")==0&&t.desc.dtype==DType::U8&&t.desc.layout=="DESC64_BYTES","only validated descriptor tables beyond frozen weight views");
  }
  json::check(cpu==13+stats_cpu.size()&&constants==119&&weight_views==84,"complete frozen CPU and constant views");return result;
 }catch(const std::bad_alloc&){return Result<Plan>::failure({ENOMEM,"HUNET_PLAN","allocation"});}
 catch(const std::exception&e){return Result<Plan>::failure({EINVAL,"HUNET_PLAN",e.what()});}
}
}
