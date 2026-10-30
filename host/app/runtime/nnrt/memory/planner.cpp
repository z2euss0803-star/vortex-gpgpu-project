#include <vxnn/graph.hpp>
#include "../package/json.hpp"
namespace vxnn {namespace {
struct Slot {AddressDomain domain;uint64_t offset,bytes;std::vector<std::pair<uint64_t,uint64_t>> lives;};
bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<json::add(b,m)&&b<json::add(a,n);}
bool domain_valid(AddressDomain d){return d==AddressDomain::CPU||d==AddressDomain::PS||d==AddressDomain::PL;}
}
Result<MemoryPlan> plan_memory(const Plan&plan,const MemoryBudget&budget){try{
 json::check(budget.resident_models&&budget.resident_models<=2&&budget.max_objects&&budget.max_objects<=64&&plan.caps.max_objects&&plan.caps.max_objects<=64&&budget.existing_objects<=std::min(budget.max_objects,plan.caps.max_objects),"resident/object budget");json::check(plan.tensors.size()<=4096&&plan.nodes.size()<=4096&&plan.resident_bytes<=budget.cpu_bytes&&budget.cpu_bytes<=(1ULL<<30),"CPU/package/tensor budget");
 uint64_t ps_capacity=std::min(budget.ps_bytes,plan.caps.ps_bytes),pl_capacity=std::min(budget.pl_bytes,plan.caps.pl_bytes);json::check(budget.ps_reserved<=ps_capacity&&budget.pl_reserved<=pl_capacity,"reserved region exceeds capacity");json::add(plan.caps.ps_base,ps_capacity);json::add(plan.caps.pl_base,pl_capacity);
 auto capacity=[&](AddressDomain d){return d==AddressDomain::CPU?budget.cpu_bytes-plan.resident_bytes:d==AddressDomain::PS?ps_capacity:pl_capacity;};auto reserved=[&](AddressDomain d){return d==AddressDomain::PS?budget.ps_reserved:d==AddressDomain::PL?budget.pl_reserved:0;};
 for(auto&h:budget.held){json::check(domain_valid(h.domain),"invalid held domain");if(!h.retired)json::check(h.bytes&&json::add(h.offset,h.bytes)<=capacity(h.domain),"held range outside domain");}
 struct Request {PlannedTensor tensor;bool workspace;NodeId node;};std::vector<Request> requests;std::set<TensorId> ids;
 for(auto&t:plan.tensors){json::check(domain_valid(t.desc.domain)&&t.desc.id&&ids.insert(t.desc.id).second&&t.first<=t.last&&t.desc.bytes&&t.desc.alignment>=64&&t.desc.alignment<=(1ULL<<20)&&!(t.desc.alignment&(t.desc.alignment-1)),"tensor allocation metadata");auto copy=t;if(copy.desc.constant)copy.last=UINT64_MAX;requests.push_back({copy,false,0});}
 for(size_t i=0;i<plan.nodes.size();i++)if(plan.nodes[i].workspace_bytes){PlannedTensor w;w.desc.bytes=plan.nodes[i].workspace_bytes;w.desc.alignment=plan.caps.alignment;w.desc.domain=AddressDomain::PL;w.first=w.last=i+1;requests.push_back({w,true,plan.nodes[i].node.id});}
 std::stable_sort(requests.begin(),requests.end(),[](const Request&a,const Request&b){return a.tensor.first<b.tensor.first;});std::vector<Slot> slots;std::map<TensorId,TensorBinding> bindings;MemoryPlan out;out.plan_id=plan.plan_id;out.object_count=budget.existing_objects;
 for(auto&r:requests){auto&t=r.tensor;uint64_t bytes=json::align(t.desc.bytes,t.desc.alignment),index=slots.size();
  for(size_t i=0;i<slots.size();i++){auto&s=slots[i];if(s.domain!=t.desc.domain||s.bytes<bytes||s.offset%t.desc.alignment)continue;bool live=std::any_of(s.lives.begin(),s.lives.end(),[&](const auto&life){return !(life.second<t.first||t.last<life.first);});if(!live){index=i;break;}}
  if(index==slots.size()){
   uint64_t offset=json::align(reserved(t.desc.domain),t.desc.alignment);bool move=true;
   while(move){move=false;for(auto&s:slots)if(s.domain==t.desc.domain&&overlap(offset,bytes,s.offset,s.bytes)){offset=json::align(json::add(s.offset,s.bytes),t.desc.alignment);move=true;}for(auto&h:budget.held)if(!h.retired&&h.domain==t.desc.domain&&overlap(offset,bytes,h.offset,h.bytes)){offset=json::align(json::add(h.offset,h.bytes),t.desc.alignment);move=true;}}
   json::check(json::add(offset,bytes)<=capacity(t.desc.domain),"tensor/workspace capacity exhausted");slots.push_back({t.desc.domain,offset,bytes,{}});if(t.desc.domain!=AddressDomain::CPU){++out.object_count;json::check(out.object_count<=std::min(budget.max_objects,plan.caps.max_objects),"64 live device objects exceeded");}
  }
  auto&s=slots[index];s.lives.emplace_back(t.first,t.last);TensorBinding b{t.desc.id,t.desc.domain,s.offset,t.desc.bytes,index+1};if(r.workspace)json::check(out.workspace_bindings.emplace(r.node,b).second,"workspace node duplicated");else bindings.emplace(t.desc.id,b);
 }
 for(auto&t:plan.tensors){out.bindings.push_back(bindings.at(t.desc.id));}
 out.cpu_peak=plan.resident_bytes;out.ps_peak=budget.ps_reserved;out.pl_peak=budget.pl_reserved;
 for(auto&s:slots){auto end=json::add(s.offset,s.bytes);if(s.domain==AddressDomain::CPU)out.cpu_peak=std::max(out.cpu_peak,json::add(end,plan.resident_bytes));else if(s.domain==AddressDomain::PS)out.ps_peak=std::max(out.ps_peak,end);else out.pl_peak=std::max(out.pl_peak,end);}return Result<MemoryPlan>::success(std::move(out));
 }catch(const std::exception&e){return Result<MemoryPlan>::failure({ENOMEM,"MEMORY_REJECTED",e.what()});}}
}
