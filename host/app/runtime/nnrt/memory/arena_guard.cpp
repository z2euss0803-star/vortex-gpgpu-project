#include <vxnn/arena_guard.hpp>
#include "../package/json.hpp"
#include <cerrno>
#include <time.h>
namespace vxnn {
namespace {
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<json::add(b,m)&&b<json::add(a,n);}
struct Entry {ArenaView view;BufferRef backing;uint64_t first=0,last=0;NodeId workspace=0;bool live=false;};
}
struct ArenaGuards::Impl {
 ArenaGuardReceipt receipt;IGpuBackend&backend;Digest plan;std::vector<NodeId> nodes;std::vector<uint64_t> jobs;std::vector<Entry> entries;size_t next=0;uint64_t retired_in_node=0;
 Impl(IGpuBackend&b,Digest p):backend(b),plan(p){}
 Result<void> fail(Error e){if(!receipt.first_error)receipt.first_error=e;return Result<void>::failure(*receipt.first_error);}
 Result<void> state(bool yes,const char*why){return yes?Result<void>::success():fail({EPROTO,"ARENA_GUARD",why});}
 NodeId current()const{return next<nodes.size()?nodes[next]:0;}
 std::array<uint8_t,64> pattern(const Entry&e,bool tail)const{
  // Canary, not a cryptographic data digest. Identity/lifetime/side distinguish
  // reused logical views; the frozen model hashes protect actual constants.
  std::array<uint8_t,64> p{};uint64_t salt=e.view.tensor.tensor^e.workspace^e.first^e.backing.device_generation^(tail?0x31:0x62);
  for(size_t i=0;i<p.size();i++){p[i]=uint8_t(0x40|((plan[(i+salt)%32]^salt^i)&0x3f));}
  return p;
 }
 Result<void> transfer(Entry&e,bool tail,bool write){
  BufferRef ref=e.backing;ref.offset=tail?e.view.guard_after:e.view.guard_before;ref.bytes=64;ref.access=write?2:1;
  const auto expected=pattern(e,tail);std::array<uint8_t,64> got{};auto start=now();
  if(!start)return fail({EIO,"ARENA_GUARD","monotonic clock"});
  auto finish=[&](){auto end=now();if(!end||end<start)return false;auto&total=write?receipt.seed_ns:receipt.check_ns;total=json::add(total,end-start);return true;};
  try{
   auto copied=write?backend.copy(ref,Bytes{expected.data(),64}):backend.copy(ref,MutableBytes{got.data(),64});
   if(write)++receipt.writes;else ++receipt.reads;
   bool timed=finish();if(!copied)return fail(*copied.error);if(!timed)return fail({EIO,"ARENA_GUARD","monotonic clock"});
  }catch(const std::bad_alloc&){finish();return fail({ENOMEM,"ARENA_GUARD","COPY allocation"});}
  catch(const std::exception&ex){finish();return fail({EIO,"ARENA_GUARD",ex.what()});}
  catch(...){finish();return fail({EIO,"ARENA_GUARD","unknown COPY exception"});}
  if(!write&&got!=expected){receipt.fault=ArenaGuardFault{current(),e.view.tensor.tensor,bool(e.workspace),tail,ref,expected,got};return fail({EBADMSG,"ARENA_GUARD","logical guard mismatch"});}
  return Result<void>::success();
 }
 Result<void> check(){for(auto&e:entries)if(e.live)for(bool tail:{false,true}){auto r=transfer(e,tail,false);if(!r)return r;}return Result<void>::success();}
 Result<void> seed(uint64_t step){for(auto&e:entries)if(e.first==step){e.live=true;for(bool tail:{false,true}){auto r=transfer(e,tail,true);if(!r)return r;}}return Result<void>::success();}
};
ArenaGuards::ArenaGuards(std::unique_ptr<Impl>p):impl_(std::move(p)){}
ArenaGuards::~ArenaGuards()=default;
Result<std::shared_ptr<ArenaGuards>> ArenaGuards::create(const Plan&p,const ArenaPlan&a,const std::vector<ArenaAllocation>&owned,uint64_t epoch,IGpuBackend&backend){try{
 json::check(p.plan_id==a.plan_id&&p.policy.hardware.device_generation==epoch&&!p.nodes.empty()&&p.nodes.size()<=4096&&p.tensors.size()<=4096&&p.tensors.size()==a.tensors.size(),"exact plan and bounded graph");
 auto bindings=bind_arenas(a,owned,epoch);if(!bindings)return Result<std::shared_ptr<ArenaGuards>>::failure(*bindings.error);
 std::unique_ptr<Impl> s(new Impl(backend,p.plan_id));s->entries.reserve(p.tensors.size()+a.workspaces.size());s->nodes.reserve(p.nodes.size());std::map<TensorId,const PlannedTensor*> tensors;std::map<uint64_t,BufferRef> objects;
 for(const auto&b:owned)objects.emplace(b.arena,b.backing);
 for(const auto&t:p.tensors)json::check(t.desc.id&&t.first<=t.last&&t.first<=p.nodes.size()&&tensors.emplace(t.desc.id,&t).second,"unique tensor and bounded lifetime");
 std::set<TensorId> seen;
 for(const auto&v:a.tensors){json::check(tensors.count(v.tensor.tensor)&&seen.insert(v.tensor.tensor).second,"exact logical tensor set");const auto&t=*tensors.at(v.tensor.tensor);json::check(v.tensor.bytes==t.desc.bytes&&v.tensor.domain==t.desc.domain&&v.access==t.desc.access,"tensor extent/domain/access match graph");s->entries.push_back({v,objects.at(v.arena),t.first,t.desc.constant?UINT64_MAX:t.last,0,false});}
 std::set<NodeId> seen_nodes;size_t workspaces=0;
 for(size_t i=0;i<p.nodes.size();i++){
  const auto&n=p.nodes[i];json::check(n.node.id&&seen_nodes.insert(n.node.id).second&&n.job_count<=0xfffffffeULL,"unique node and bounded job count");s->nodes.push_back(n.node.id);s->jobs.push_back(std::max(uint64_t(1),n.job_count));
  for(auto id:n.node.inputs)json::check(tensors.count(id)&&tensors.at(id)->first<i+1&&(tensors.at(id)->desc.constant||tensors.at(id)->last>=i+1),"input produced and live before consumer");
  for(auto id:n.node.outputs)json::check(tensors.count(id)&&!tensors.at(id)->desc.constant&&tensors.at(id)->first==i+1&&tensors.at(id)->last>=i+1,"output exact producer lifetime");
  if(n.workspace_bytes){++workspaces;json::check(a.workspaces.count(n.node.id),"workspace missing");auto v=a.workspaces.at(n.node.id);json::check(v.tensor.bytes==n.workspace_bytes&&v.access==3&&!v.tensor.tensor,"workspace extent/access");s->entries.push_back({v,objects.at(v.arena),i+1,i+1,n.node.id,false});}
 }
 json::check(workspaces==a.workspaces.size(),"no extra workspace");
 json::check(bool(a.readonly_placeholder)==bool(p.readonly_placeholder_bytes),"readonly placeholder matches plan");
 if(a.readonly_placeholder){const auto&v=*a.readonly_placeholder;json::check(p.readonly_placeholder_bytes==64&&v.tensor.bytes==64&&v.access==1,"readonly placeholder extent/access");s->entries.push_back({v,objects.at(v.arena),0,UINT64_MAX,0,false});}
 for(auto id:p.inputs)json::check(tensors.count(id)&&!tensors.at(id)->first,"input initialized before first node");
 for(auto id:p.outputs)json::check(tensors.count(id)&&tensors.at(id)->last>=p.nodes.size(),"output remains live for publication");
 for(size_t i=0;i<s->entries.size();i++)for(size_t j=0;j<i;j++){
  const auto&x=s->entries[i];const auto&y=s->entries[j];if(x.view.arena!=y.view.arena||x.last<y.first||y.last<x.first)continue;
  json::check(!overlap(x.view.guard_before,json::add(x.view.tensor.bytes,128),y.view.guard_before,json::add(y.view.tensor.bytes,128)),"live data/guard intervals overlap");
 }
 return Result<std::shared_ptr<ArenaGuards>>::success(std::shared_ptr<ArenaGuards>(new ArenaGuards(std::move(s))));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<ArenaGuards>>::failure({ENOMEM,"ARENA_GUARD","metadata allocation"});}
 catch(const std::exception&e){return Result<std::shared_ptr<ArenaGuards>>::failure({EINVAL,"ARENA_GUARD",e.what()});}}
Result<void> ArenaGuards::initialize(){auto&s=*impl_;if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);auto valid=s.state(!s.receipt.initialized,"already initialized");if(!valid)return valid;s.receipt.initialized=true;return s.seed(0);}
Result<void> ArenaGuards::begin_node(NodeId node){auto&s=*impl_;if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);auto valid=s.state(s.receipt.initialized&&!s.receipt.active&&s.next<s.nodes.size()&&s.current()==node,"node order or unretired predecessor");if(!valid)return valid;
 auto checked=s.check();if(!checked)return checked;for(auto&e:s.entries)if(e.last<s.next+1)e.live=false;s.receipt.active=true;s.retired_in_node=0;return s.seed(s.next+1);}
Result<void> ArenaGuards::verify_retired_node(NodeId node){auto&s=*impl_;if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);auto valid=s.state(s.receipt.initialized&&s.receipt.active&&s.current()==node&&s.next<s.jobs.size()&&s.retired_in_node+1==s.jobs[s.next],"retirement order or missing subjob ACKs");if(!valid)return valid;auto checked=s.check();if(!checked)return checked;++s.retired_in_node;++s.receipt.retired_batches;s.receipt.active=false;++s.next;s.receipt.completed_nodes=s.next;return Result<void>::success();}
Result<void> ArenaGuards::verify_retired_subjob(NodeId node){auto&s=*impl_;if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);auto valid=s.state(s.receipt.initialized&&s.receipt.active&&s.current()==node&&s.next<s.jobs.size()&&s.retired_in_node+1<s.jobs[s.next],"intermediate retirement order or final subjob must close node");if(!valid)return valid;auto checked=s.check();if(!checked)return checked;++s.retired_in_node;++s.receipt.retired_batches;return Result<void>::success();}
Result<void> ArenaGuards::verify_final(){auto&s=*impl_;if(s.receipt.first_error)return Result<void>::failure(*s.receipt.first_error);auto valid=s.state(s.receipt.initialized&&!s.receipt.active&&s.next==s.nodes.size()&&!s.receipt.final_verified,"final before all retirements or duplicate");if(!valid)return valid;auto checked=s.check();if(!checked)return checked;s.receipt.final_verified=true;return Result<void>::success();}
const ArenaGuardReceipt& ArenaGuards::receipt()const{return impl_->receipt;}
}
