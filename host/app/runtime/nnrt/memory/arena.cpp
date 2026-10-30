#include <vxnn/arena.hpp>
#include "../package/json.hpp"
#include <cerrno>
namespace vxnn {namespace {
bool domain_ok(AddressDomain d){return d==AddressDomain::CPU||d==AddressDomain::PS||d==AddressDomain::PL;}
bool overlaps(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<json::add(b,m)&&b<json::add(a,n);}
struct Slot {AddressDomain domain;uint64_t start,data,bytes;std::vector<std::pair<uint64_t,uint64_t>> lives;};
}
Result<ArenaPlan> plan_arenas(const Plan&plan,const MemoryBudget&budget){try{
 json::check(vxunified::nonzero_digest(plan.plan_id)&&budget.resident_models&&budget.resident_models<=2&&budget.max_objects&&budget.max_objects<=64&&plan.caps.max_objects&&plan.caps.max_objects<=64&&budget.existing_objects<=std::min(budget.max_objects,plan.caps.max_objects),"identity/resident/object budget");
 json::check(plan.tensors.size()<=4096&&plan.nodes.size()<=4096&&plan.resident_bytes<=budget.cpu_bytes&&budget.cpu_bytes<=(1ULL<<30),"package/CPU/tensor budget");
 auto capacity=[&](AddressDomain d){return d==AddressDomain::CPU?budget.cpu_bytes-plan.resident_bytes:d==AddressDomain::PS?std::min(budget.ps_bytes,plan.caps.ps_bytes):std::min(budget.pl_bytes,plan.caps.pl_bytes);};
 auto reserved=[&](AddressDomain d){return d==AddressDomain::CPU?uint64_t(0):d==AddressDomain::PS?budget.ps_reserved:budget.pl_reserved;};
 json::check(reserved(AddressDomain::PS)<=capacity(AddressDomain::PS)&&reserved(AddressDomain::PL)<=capacity(AddressDomain::PL),"reserved range capacity");json::add(plan.caps.ps_base,capacity(AddressDomain::PS));json::add(plan.caps.pl_base,capacity(AddressDomain::PL));
 for(const auto&h:budget.held){json::check(domain_ok(h.domain),"held domain");if(!h.retired)json::check(h.bytes&&json::add(h.offset,h.bytes)<=capacity(h.domain),"held range capacity");}
 struct Request {PlannedTensor tensor;NodeId workspace=0;bool readonly=false;};std::vector<Request> requests;std::set<TensorId> ids;std::set<NodeId> nodes;
 json::check(!plan.readonly_placeholder_bytes||plan.readonly_placeholder_bytes==64,"bounded readonly placeholder");
 if(plan.readonly_placeholder_bytes){PlannedTensor ro;ro.desc.bytes=64;ro.desc.alignment=64;ro.desc.domain=AddressDomain::PL;ro.desc.access=1;ro.first=0;ro.last=UINT64_MAX;requests.push_back({ro,0,true});}
 for(const auto&t:plan.tensors){json::check(t.desc.id&&ids.insert(t.desc.id).second&&domain_ok(t.desc.domain)&&t.first<=t.last&&t.desc.bytes&&t.desc.alignment>=64&&t.desc.alignment<=(1ULL<<20)&&!(t.desc.alignment&(t.desc.alignment-1))&&t.desc.access&&!(t.desc.access&~3u)&&(!t.desc.constant||t.desc.access==1),"tensor view metadata");auto copy=t;if(copy.desc.constant)copy.last=UINT64_MAX;requests.push_back({copy,0});}
 for(size_t i=0;i<plan.nodes.size();i++){const auto&n=plan.nodes[i];json::check(n.node.id&&nodes.insert(n.node.id).second,"node identity");if(n.workspace_bytes){PlannedTensor w;w.desc.bytes=n.workspace_bytes;w.desc.alignment=plan.caps.alignment;w.desc.domain=AddressDomain::PL;w.desc.access=3;w.first=w.last=i+1;json::check(w.desc.alignment>=64&&w.desc.alignment<=(1ULL<<20)&&!(w.desc.alignment&(w.desc.alignment-1)),"workspace alignment");requests.push_back({w,n.node.id});}}
 std::stable_sort(requests.begin(),requests.end(),[](const Request&a,const Request&b){return a.tensor.first<b.tensor.first;});std::vector<Slot> slots;std::map<TensorId,ArenaView> views;ArenaPlan out;out.plan_id=plan.plan_id;
 for(const auto&r:requests){const auto&t=r.tensor;uint64_t index=slots.size();
  for(size_t i=0;i<slots.size();i++){const auto&s=slots[i];if(s.domain!=t.desc.domain||s.data%t.desc.alignment||t.desc.bytes>json::add(s.start,s.bytes)-s.data-64)continue;bool live=std::any_of(s.lives.begin(),s.lives.end(),[&](const auto&l){return !(l.second<t.first||t.last<l.first);});if(!live){index=i;break;}}
  if(index==slots.size()){
   uint64_t bytes=json::align(json::add(json::add(t.desc.alignment,t.desc.bytes),64),t.desc.alignment),start=json::align(reserved(t.desc.domain),t.desc.alignment);bool move=true;
   while(move){move=false;for(const auto&s:slots)if(s.domain==t.desc.domain&&overlaps(start,bytes,s.start,s.bytes)){start=json::align(json::add(s.start,s.bytes),t.desc.alignment);move=true;}for(const auto&h:budget.held)if(!h.retired&&h.domain==t.desc.domain&&overlaps(start,bytes,h.offset,h.bytes)){start=json::align(json::add(h.offset,h.bytes),t.desc.alignment);move=true;}}
   json::check(json::add(start,bytes)<=capacity(t.desc.domain),"guarded tensor/workspace capacity");slots.push_back({t.desc.domain,start,json::add(start,t.desc.alignment),bytes,{}});
  }
  auto&s=slots[index];s.lives.emplace_back(t.first,t.last);ArenaView v;v.tensor={t.desc.id,t.desc.domain,s.data,t.desc.bytes,index+1};v.access=t.desc.access;if(r.readonly)out.readonly_placeholder=v;else if(r.workspace)out.workspaces.emplace(r.workspace,v);else views.emplace(t.desc.id,v);
 }
 std::vector<size_t> order;for(size_t i=0;i<slots.size();i++)order.push_back(i);std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return slots[a].domain!=slots[b].domain?slots[a].domain<slots[b].domain:slots[a].start<slots[b].start;});std::vector<uint64_t> owner(slots.size());out.object_count=budget.existing_objects;
 for(auto i:order){const auto&s=slots[i];bool merge=!out.objects.empty()&&out.objects.back().domain==s.domain;uint64_t end=merge?json::add(out.objects.back().offset,out.objects.back().bytes):s.start;
  if(merge)for(const auto&h:budget.held)if(!h.retired&&h.domain==s.domain&&overlaps(end,s.start-end,h.offset,h.bytes))merge=false;
  if(merge)out.objects.back().bytes=json::add(s.start,s.bytes)-out.objects.back().offset;
  else{out.objects.push_back({out.objects.size()+1,s.domain,s.start,s.bytes,3});if(s.domain!=AddressDomain::CPU){++out.object_count;json::check(out.object_count<=std::min(budget.max_objects,plan.caps.max_objects),"64 physical device objects exceeded");}}
  owner[i]=out.objects.back().slot;
 }
 auto finish=[&](ArenaView&v){v.arena=owner.at(v.tensor.slot-1);const auto&a=out.objects.at(v.arena-1);v.object_offset=v.tensor.offset-a.offset;json::check(v.object_offset>=64,"leading guard");v.guard_before=v.object_offset-64;v.guard_after=json::add(v.object_offset,v.tensor.bytes);json::check(json::add(v.guard_after,64)<=a.bytes,"trailing guard");};
 for(const auto&t:plan.tensors){auto v=views.at(t.desc.id);finish(v);out.tensors.push_back(v);}for(auto&kv:out.workspaces)finish(kv.second);if(out.readonly_placeholder)finish(*out.readonly_placeholder);
 out.cpu_peak=plan.resident_bytes;out.ps_peak=budget.ps_reserved;out.pl_peak=budget.pl_reserved;for(const auto&a:out.objects){auto end=json::add(a.offset,a.bytes);if(a.domain==AddressDomain::CPU)out.cpu_peak=std::max(out.cpu_peak,json::add(end,plan.resident_bytes));else if(a.domain==AddressDomain::PS)out.ps_peak=std::max(out.ps_peak,end);else out.pl_peak=std::max(out.pl_peak,end);}
 return Result<ArenaPlan>::success(std::move(out));
 }catch(const std::bad_alloc&){return Result<ArenaPlan>::failure({ENOMEM,"ARENA","allocation"});}catch(const std::exception&e){return Result<ArenaPlan>::failure({EINVAL,"ARENA",e.what()});}}
Result<ArenaBindings> bind_arenas(const ArenaPlan&plan,const std::vector<ArenaAllocation>&allocations,uint64_t generation){try{
 json::check(vxunified::nonzero_digest(plan.plan_id)&&generation&&allocations.size()==plan.objects.size()&&plan.objects.size()<=64,"complete explicit arena/epoch binding");std::map<uint64_t,const ArenaAllocation*> map;std::set<uint64_t> handles;
 for(const auto&a:allocations){json::check(a.arena&&a.arena<=plan.objects.size()&&map.emplace(a.arena,&a).second,"arena identity/duplicate");const auto&o=plan.objects[a.arena-1];const auto&b=a.backing;json::check(o.slot==a.arena&&o.domain!=AddressDomain::CPU&&o.domain==a.domain&&b.handle&&handles.insert(b.handle).second&&b.object_generation&&b.device_generation==generation&&!b.offset&&b.bytes==o.bytes&&(b.access&3)==3&&!(b.access&~3u),"owned backing/domain/extent/permission/epoch");}
 ArenaBindings out;auto resolve=[&](const ArenaView&v){json::check(v.arena&&map.count(v.arena)&&v.tensor.bytes&&v.access&&!(v.access&~3u),"view arena and access");const auto&a=plan.objects.at(v.arena-1);const auto&base=map.at(v.arena)->backing;json::check(v.tensor.domain==a.domain&&v.tensor.offset>=a.offset&&v.tensor.offset-a.offset==v.object_offset&&v.guard_bytes==64&&v.object_offset>=64&&v.guard_before==v.object_offset-64&&v.guard_after==json::add(v.object_offset,v.tensor.bytes)&&json::add(v.guard_after,64)<=base.bytes,"view and both guards within owned object");return BufferRef{base.handle,base.object_generation,v.object_offset,v.tensor.bytes,v.access,generation};};
 for(const auto&v:plan.tensors){json::check(v.tensor.tensor&&out.tensors.emplace(v.tensor.tensor,resolve(v)).second,"tensor binding duplicate");}
 for(const auto&kv:plan.workspaces){json::check(kv.first&&out.workspaces.emplace(kv.first,resolve(kv.second)).second,"workspace binding duplicate");}
 if(plan.readonly_placeholder){json::check(plan.readonly_placeholder->tensor.bytes==64&&plan.readonly_placeholder->access==1,"readonly placeholder extent/access");out.readonly_placeholder=resolve(*plan.readonly_placeholder);}
 return Result<ArenaBindings>::success(std::move(out));
 }catch(const std::bad_alloc&){return Result<ArenaBindings>::failure({ENOMEM,"ARENA_BIND","allocation"});}catch(const std::exception&e){return Result<ArenaBindings>::failure({EINVAL,"ARENA_BIND",e.what()});}}
}
