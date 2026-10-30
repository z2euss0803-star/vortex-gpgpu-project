#pragma once
#include <vxunified/scheduler.hpp>
#include <vxunified/exit_retention.hpp>
#include <array>
#include <deque>
#include <map>
#include <mutex>
#include <tuple>
#include <time.h>
namespace vxunified {
struct Scheduler::State {
 detail::ExitRetention exit_retention;
 using Key=std::tuple<std::string,std::string,std::string>;
 struct Entry {Key key;WorkRequest request;Digest source_id{};uint64_t count=0,next=0,token=0;std::optional<WorkItem> head;RequestStatus status=RequestStatus::QUEUED;bool active=false,legacy=false;size_t completion_slot=0;int error=0;};
 struct Slot {bool held=false;std::optional<WorkOutcome> value;uint64_t order=0;};
 mutable std::mutex mutex;std::shared_ptr<PendingBudget> budget;std::function<uint64_t()> clock;
 std::map<Key,std::shared_ptr<Entry>> index;std::deque<Key> ring;std::optional<Key> barrier;
 std::array<Slot,64> slots;uint32_t gpu=0,cpu=0;uint64_t completion_order=0;bool closing=false,whole_requests=false;std::string boot;
 static Key key(const RequestKey&k){return {k.boot_id,k.client_id,k.request_id};}
 static bool terminal(RequestStatus s){return s==RequestStatus::COMPLETED||s==RequestStatus::FAILED||s==RequestStatus::CANCELLED;}
 static bool hardware(WorkKind k){return k==WorkKind::GPU||k==WorkKind::LEGACY_EXCLUSIVE;}
 static uint32_t cost(WorkKind k){return k==WorkKind::CPU?4:1;}
 static uint64_t monotonic(){timespec t{};if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return uint64_t(t.tv_sec)*1000000000ULL+uint64_t(t.tv_nsec);}
 static bool identifier(const std::string&s){if(s.empty()||s.size()>128)return false;for(unsigned char c:s)if(c<33||c>126)return false;return true;}
 uint32_t completion_count()const{uint32_t n=0;for(auto&s:slots)n+=s.value.has_value();return n;}
 uint32_t reserved_count()const{uint32_t n=0;for(auto&s:slots)n+=s.held;return n;}
 Result<WorkItem> load(Entry&e){
  try{
  auto result=e.request.source->get(e.next);if(!result)return result;
  WorkItem w=*result.value;
  if(w.work_index!=e.next||!w.node||w.dependencies.size()>32||w.pending_token||w.cancel_control)return Result<WorkItem>::failure({EINVAL,"SCHEDULER","invalid lowered item"});
  for(auto d:w.dependencies)if(d>=e.next)return Result<WorkItem>::failure({EINVAL,"SCHEDULER","dependency not retired prior work"});
  if(w.kind!=WorkKind::GPU&&w.kind!=WorkKind::CPU&&w.kind!=WorkKind::TRANSFER&&w.kind!=WorkKind::PRESENT&&w.kind!=WorkKind::LEGACY_EXCLUSIVE)return Result<WorkItem>::failure({EINVAL,"SCHEDULER","unknown work kind"});
  if(e.next&&((w.kind==WorkKind::LEGACY_EXCLUSIVE)!=e.legacy))return Result<WorkItem>::failure({EINVAL,"SCHEDULER","cannot enter or leave legacy barrier mid transaction"});
  if(w.kind==WorkKind::GPU){
   if(auto*j=std::get_if<GPUJobDesc>(&w.operation)){if(!j->program_handle||!j->program_generation||!j->device_generation||!j->cookie)return Result<WorkItem>::failure({EINVAL,"SCHEDULER","unbound lowered GPU job"});}
   else if(auto*d=std::get_if<DeferredGpuRef>(&w.operation)){if(!identifier(d->registered_name)||d->node!=w.node||!d->device_generation||!d->cookie)return Result<WorkItem>::failure({EINVAL,"SCHEDULER","invalid deferred GPU identity"});}
   else return Result<WorkItem>::failure({EINVAL,"SCHEDULER","GPU descriptor type"});
  }
  else if(!std::holds_alternative<CpuTaskRef>(w.operation)||std::get<CpuTaskRef>(w.operation).registered_name.empty()||std::get<CpuTaskRef>(w.operation).registered_name.size()>128)return Result<WorkItem>::failure({EINVAL,"SCHEDULER","unregistered work operation"});
  w.request=e.request.request;w.app=e.request.app;
  if(e.request.queue_deadline_ns&&(!w.queue_deadline_ns||e.request.queue_deadline_ns<w.queue_deadline_ns))w.queue_deadline_ns=e.request.queue_deadline_ns;
  w.cancel_control=std::make_shared<vxnn::CpuCancel>();return Result<WorkItem>::success(std::move(w));
  }catch(const std::bad_alloc&){return Result<WorkItem>::failure({ENOMEM,"SCHEDULER","lowered item allocation"});}catch(...){return Result<WorkItem>::failure({EIO,"SCHEDULER","lowering source exception"});}
 }
 Result<void> reserve(Entry&e){if(!e.head||e.token)return Result<void>::success();auto t=budget->reserve(cost(e.head->kind));if(!t)return Result<void>::failure(*t.error);e.token=*t.value;e.head->pending_token=e.token;return Result<void>::success();}
 void terminalize(Entry&e,RequestStatus status,int error){
  // Call only after releasing non-active tokens or proving retirement.
  e.status=status;e.error=error;e.head.reset();e.request.source.reset();const auto&k=e.key;
  auto it=std::find(ring.begin(),ring.end(),k);if(it!=ring.end())ring.erase(it);if(barrier&&*barrier==k)barrier.reset();
 }
 Result<void> cancel_locked(Entry&e){
  if(terminal(e.status))return Result<void>::success();
  if(e.head&&e.head->cancel_control)e.head->cancel_control->cancel();
  if(e.active){e.status=RequestStatus::CANCEL_PENDING;return Result<void>::success();}
  if(e.token){auto r=budget->release(e.token);if(!r)return r;e.token=0;}
  terminalize(e,RequestStatus::CANCELLED,ECANCELED);return Result<void>::success();
 }
};
}
