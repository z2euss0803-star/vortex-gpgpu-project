#include <vxnn/arena_storage.hpp>
#include <vxnn/request_input.hpp>
#include "../package/json.hpp"
#include <cerrno>
#include <time.h>
namespace vxnn {
namespace {
uint64_t now(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
}
struct ArenaStorage::Impl {
 Plan plan;ArenaPlan original_layout;std::shared_ptr<ArenaStorage>owner;bool borrow_live=false;
 ArenaPlan layout;std::vector<ArenaAllocation> allocations;std::optional<ArenaBindings> bindings;ArenaStorageReceipt receipt;IGpuBackend&backend;
 explicit Impl(IGpuBackend&b):backend(b){}
 Result<void> fail(Error error){receipt.phase=ArenaStoragePhase::FAULTED;bindings.reset();if(!receipt.first_error)receipt.first_error=error;return Result<void>::failure(*receipt.first_error);}
};
ArenaStorage::ArenaStorage(std::unique_ptr<Impl>p):impl_(std::move(p)){}
ArenaStorage::~ArenaStorage()=default;
Result<std::shared_ptr<ArenaStorage>> ArenaStorage::create(const Plan&p,const ArenaPlan&a,uint64_t epoch,IGpuBackend&backend){try{
 json::check(vxunified::nonzero_digest(p.plan_id)&&p.plan_id==a.plan_id&&epoch&&p.policy.hardware.device_generation==epoch&&vxunified::s8_provider(p.policy.hardware),"S8 plan and epoch binding");
 json::check(p.caps.contexts==1&&!p.caps.preempt&&p.caps.gpu_address_bits==32&&p.caps.pl_address_bits==40&&p.caps.ps_dma_bits==32&&p.caps.max_objects&&p.caps.max_objects<=64&&a.object_count&&a.object_count<=p.caps.max_objects,"bounded native memory capabilities");
 json::check(a.objects.size()==1&&a.tensors.size()==p.tensors.size(),"single arena provider only");const auto&o=a.objects[0];
 json::check(o.slot==1&&o.domain==AddressDomain::PL&&o.access==3&&o.offset==(8u<<20)&&o.bytes&&o.bytes%64==0,"exact single PL arena and reserved prefix");
 const auto bytes=json::add(o.offset,o.bytes);json::check(json::add(bytes,128)<=p.caps.pl_bytes&&a.pl_peak==bytes,"prefix and outer allocation guards included in capacity");
 std::unique_ptr<Impl> s(new Impl(backend));s->plan=p;s->original_layout=a;s->layout=a;s->allocations.reserve(1);s->receipt.epoch=epoch;s->receipt.prefix_bytes=o.offset;s->receipt.backing_bytes=bytes;s->layout.objects[0].offset=0;s->layout.objects[0].bytes=bytes;
 auto shift=[&](ArenaView&v){
  json::check(v.arena==1&&v.tensor.domain==AddressDomain::PL&&v.tensor.bytes&&v.guard_bytes==64&&v.object_offset>=64&&v.guard_before==v.object_offset-64&&v.guard_after==json::add(v.object_offset,v.tensor.bytes)&&json::add(v.guard_after,64)<=o.bytes&&v.tensor.offset==json::add(o.offset,v.object_offset),"exact source view and guards");
  v.object_offset=json::add(v.object_offset,o.offset);v.guard_before=json::add(v.guard_before,o.offset);v.guard_after=json::add(v.guard_after,o.offset);
 };
 for(auto&v:s->layout.tensors){shift(v);}
 for(auto&kv:s->layout.workspaces){shift(kv.second);}
 json::check(bool(a.readonly_placeholder)==bool(p.readonly_placeholder_bytes),"readonly placeholder matches plan");
 if(s->layout.readonly_placeholder)shift(*s->layout.readonly_placeholder);
 return Result<std::shared_ptr<ArenaStorage>>::success(std::shared_ptr<ArenaStorage>(new ArenaStorage(std::move(s))));
 }catch(const std::bad_alloc&){return Result<std::shared_ptr<ArenaStorage>>::failure({ENOMEM,"ARENA_STORAGE","metadata allocation"});}
 catch(const std::exception&e){return Result<std::shared_ptr<ArenaStorage>>::failure({EINVAL,"ARENA_STORAGE",e.what()});}}
Result<void> ArenaStorage::allocate(){
 auto&s=*impl_;auto&r=s.receipt;if(r.first_error)return Result<void>::failure(*r.first_error);
 if(r.borrowed_input)return Result<void>::failure({EOPNOTSUPP,"ARENA_INPUT_VIEW","borrowed view cannot allocate"});
 if(r.phase!=ArenaStoragePhase::PREPARED)return Result<void>::failure({EALREADY,"ARENA_STORAGE","allocation already attempted"});
 auto start=now();if(!start)return s.fail({EIO,"ARENA_STORAGE","monotonic clock"});r.phase=ArenaStoragePhase::ALLOCATING;r.allocation_attempted=true;
 auto finish=[&](){auto end=now();if(!end||end<start)return false;r.allocate_ns=end-start;return true;};
 try{
  auto result=s.backend.allocate(r.backing_bytes,AddressDomain::PL,3);
  // Retain the returned root before any subsequent metadata allocation/check.
  // Unknown allocation after ioctl return failure remains owned by the fd.
  if(result){r.root=*result.value;}
  bool timed=finish();if(!result)return s.fail(*result.error);if(!timed)return s.fail({EIO,"ARENA_STORAGE","monotonic clock"});
  const auto&b=*r.root;if(!b.handle||!b.object_generation||b.offset||b.bytes!=r.backing_bytes||b.access!=3||b.device_generation!=r.epoch)return s.fail({EPROTO,"ARENA_STORAGE","returned root differs"});
  s.allocations.push_back({1,AddressDomain::PL,b});auto bound=bind_arenas(s.layout,s.allocations,r.epoch);if(!bound)return s.fail(*bound.error);s.bindings=*bound.value;r.phase=ArenaStoragePhase::READY;return Result<void>::success();
 }catch(const std::bad_alloc&){finish();return s.fail({ENOMEM,"ARENA_STORAGE","allocation metadata; fd state retained"});}
 catch(const std::exception&e){finish();return s.fail({EIO,"ARENA_STORAGE",e.what()});}
 catch(...){finish();return s.fail({EIO,"ARENA_STORAGE","unknown allocation exception"});}
}
Result<void> ArenaStorage::release(){
 auto&s=*impl_;auto&r=s.receipt;if(r.first_error)return Result<void>::failure(*r.first_error);
 if(r.borrowed_input)return Result<void>::failure({EOPNOTSUPP,"ARENA_INPUT_VIEW","only explicit view retirement is allowed"});
 if(r.input_views)return Result<void>::failure({EBUSY,"ARENA_STORAGE","live input view retains owning root"});
 if(r.phase!=ArenaStoragePhase::READY||!r.root)return Result<void>::failure({EALREADY,"ARENA_STORAGE","not releasable"});
 auto start=now();if(!start)return s.fail({EIO,"ARENA_STORAGE","monotonic clock"});r.phase=ArenaStoragePhase::RELEASING;r.release_attempted=true;s.bindings.reset();
 auto finish=[&](){auto end=now();if(!end||end<start)return false;r.release_ns=end-start;return true;};
 try{
  auto result=s.backend.release_buffer(*r.root);bool timed=finish();if(!result)return s.fail(*result.error);
  // A successful release must not leave stale READY views even on clock error.
  r.root.reset();s.allocations.clear();if(!timed)return s.fail({EIO,"ARENA_STORAGE","monotonic clock"});r.phase=ArenaStoragePhase::RETIRED;return Result<void>::success();
 }catch(const std::bad_alloc&){finish();return s.fail({ENOMEM,"ARENA_STORAGE","release metadata; fd state retained"});}
 catch(const std::exception&e){finish();return s.fail({EIO,"ARENA_STORAGE",e.what()});}
 catch(...){finish();return s.fail({EIO,"ARENA_STORAGE","unknown release exception"});}
}
namespace {
bool same_view(const ArenaView&a,const ArenaView&b){
 const auto&x=a.tensor;const auto&y=b.tensor;
 return x.tensor==y.tensor&&x.domain==y.domain&&x.offset==y.offset&&x.bytes==y.bytes&&x.slot==y.slot&&a.arena==b.arena&&a.object_offset==b.object_offset&&a.guard_before==b.guard_before&&a.guard_after==b.guard_after&&a.guard_bytes==b.guard_bytes&&a.access==b.access;
}
bool same_physical_layout(const ArenaPlan&a,const ArenaPlan&b){
 if(a.cpu_peak!=b.cpu_peak||a.ps_peak!=b.ps_peak||a.pl_peak!=b.pl_peak||a.object_count!=b.object_count||a.objects.size()!=b.objects.size()||a.tensors.size()!=b.tensors.size()||a.workspaces.size()!=b.workspaces.size()||bool(a.readonly_placeholder)!=bool(b.readonly_placeholder))return false;
 for(size_t i=0;i<a.objects.size();++i){const auto&x=a.objects[i];const auto&y=b.objects[i];if(x.slot!=y.slot||x.domain!=y.domain||x.offset!=y.offset||x.bytes!=y.bytes||x.access!=y.access)return false;}
 for(size_t i=0;i<a.tensors.size();++i)if(!same_view(a.tensors[i],b.tensors[i]))return false;
 for(const auto&x:a.workspaces){auto it=b.workspaces.find(x.first);if(it==b.workspaces.end()||!same_view(x.second,it->second))return false;}
 return !a.readonly_placeholder||same_view(*a.readonly_placeholder,*b.readonly_placeholder);
}
}
Result<std::shared_ptr<ArenaStorage>>ArenaStorage::borrow_input(const std::shared_ptr<ArenaStorage>&root,const RequestInput&input){
 using R=Result<std::shared_ptr<ArenaStorage>>;
 if(!root)return R::failure({EINVAL,"ARENA_INPUT_VIEW","owning root required"});
 auto&o=*root->impl_;const auto&r=o.receipt;
 if(o.owner||r.borrowed_input||r.phase!=ArenaStoragePhase::READY||r.first_error||!r.root||!o.bindings||r.input_views)return R::failure({EBUSY,"ARENA_INPUT_VIEW","exclusive READY unborrowed root required"});
 try{
  // This is the only Plan derivation: the admitted frozen graph is copied by
  // the existing full payload binder. No external Plan/layout ID can enter.
  auto bound=bind_request_input(o.plan,input);if(!bound)return R::failure(*bound.error);
  json::check(bound.value->plan_id==request_input_plan_id(o.plan.plan_id,input)&&bound.value->model_id==o.plan.model_id&&bound.value->policy.hardware.device_generation==r.epoch,"derived input Plan/model/epoch identity");
  ArenaPlan logical=o.original_layout;logical.plan_id=bound.value->plan_id;
  auto made=create(*bound.value,logical,r.epoch,o.backend);if(!made)return made;
  auto view=*made.value;auto&s=*view->impl_;
  json::check(same_physical_layout(s.layout,o.layout)&&s.receipt.backing_bytes==r.backing_bytes&&s.receipt.prefix_bytes==r.prefix_bytes,"complete normalized arena/guard/lifetime topology equality");
  s.allocations=o.allocations;auto refs=bind_arenas(s.layout,s.allocations,r.epoch);if(!refs)return R::failure(*refs.error);
  s.bindings=*refs.value;s.receipt.root=r.root;s.receipt.phase=ArenaStoragePhase::READY;s.receipt.borrowed_input=true;s.owner=root;s.borrow_live=true;
  ++o.receipt.input_views;return R::success(std::move(view));
 }catch(const std::bad_alloc&){return R::failure({ENOMEM,"ARENA_INPUT_VIEW","metadata allocation; root retained"});}
 catch(const std::exception&e){return R::failure({EINVAL,"ARENA_INPUT_VIEW",e.what()});}
}
Result<void>ArenaStorage::retire_input_view(bool retired){
 auto&s=*impl_;auto&r=s.receipt;
 if(r.first_error)return Result<void>::failure(*r.first_error);
 if(!r.borrowed_input||!s.owner||!s.borrow_live||r.phase!=ArenaStoragePhase::READY)return Result<void>::failure({EALREADY,"ARENA_INPUT_VIEW","not a live borrowed input view"});
 if(!retired)return Result<void>::failure({EBUSY,"ARENA_INPUT_VIEW","consumer retirement proof required"});
 auto&o=*s.owner->impl_;
 if(o.receipt.phase!=ArenaStoragePhase::READY||o.receipt.first_error||o.receipt.input_views!=1||!o.receipt.root||!r.root||r.root->handle!=o.receipt.root->handle||r.root->object_generation!=o.receipt.root->object_generation||r.epoch!=o.receipt.epoch)return s.fail({ESTALE,"ARENA_INPUT_VIEW","owning root binding changed"});
 s.bindings.reset();s.allocations.clear();r.root.reset();r.phase=ArenaStoragePhase::RETIRED;s.borrow_live=false;--o.receipt.input_views;s.owner.reset();return Result<void>::success();
}
const Plan&ArenaStorage::plan()const{return impl_->plan;}
const ArenaPlan& ArenaStorage::layout()const{return impl_->layout;}
const std::vector<ArenaAllocation>& ArenaStorage::allocations()const{return impl_->allocations;}
const std::optional<ArenaBindings>& ArenaStorage::bindings()const{return impl_->bindings;}
const ArenaStorageReceipt& ArenaStorage::receipt()const{return impl_->receipt;}
}
