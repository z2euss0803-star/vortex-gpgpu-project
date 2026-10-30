#include "state.hpp"
#include <type_traits>
namespace vxunified {
Result<WorkItem> Scheduler::next_ready(){
 WorkItem item;auto ready=dispatch_into(item);if(!ready)return Result<WorkItem>::failure(*ready.error);return Result<WorkItem>::success(std::move(item));
}
Result<void> Scheduler::dispatch_into(WorkItem&output,std::function<Result<void>(const WorkItem&)>prepare){
 static_assert(std::is_nothrow_move_assignable<WorkItem>::value,"handoff must not allocate");
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);if(s.closing)return Result<void>::failure({ESHUTDOWN,"SCHEDULER","closed dispatch"});
 try{
  uint64_t now=s.clock();if(!now)return Result<void>::failure({EIO,"SCHEDULER","clock"});
  size_t n=s.ring.size();for(size_t i=0;i<n&&!s.ring.empty();i++){
   auto entry=s.index.at(s.ring.front());auto&e=*entry;const auto&key=e.key;std::rotate(s.ring.begin(),s.ring.begin()+1,s.ring.end());if(e.active||State::terminal(e.status))continue;
   if(s.barrier&&*s.barrier!=key)continue;
   if(!e.head){auto item=s.load(e);if(!item){s.terminalize(e,RequestStatus::FAILED,item.error->code);continue;}e.head=*item.value;}
   if(e.head->queue_deadline_ns&&now>=e.head->queue_deadline_ns){if(e.token){auto r=s.budget->release(e.token);if(!r)return Result<void>::failure(*r.error);e.token=0;}s.terminalize(e,RequestStatus::FAILED,ETIMEDOUT);continue;}
   // Metadata-only residency gate. Keep queued identity/token and rotate;
   // no fixed-address program load or active completion slot on backpressure.
   auto eligible=e.request.source->ready(e.next);if(!eligible)return Result<void>::failure(*eligible.error);if(!*eligible.value)continue;
   auto reserved=s.reserve(e);if(!reserved)continue;
   // Claim only an actually eligible head. P8 controls can await a native
   // frame, so an unready request must never block its own predecessor.
   if(e.legacy||s.whole_requests){s.barrier=key;if(s.gpu||s.cpu)continue;}
   if(State::hardware(e.head->kind)&&s.gpu)continue;
   size_t slot=0;while(slot<s.slots.size()&&(s.slots[slot].held||s.slots[slot].value))++slot;if(slot==s.slots.size())return Result<void>::failure({EAGAIN,"SCHEDULER","completion64 backpressure"});
   auto result=*e.head;if(prepare){auto prepared=prepare(result);if(!prepared)return prepared;}output=std::move(result);
   s.slots[slot].held=true;e.completion_slot=slot;e.active=true;e.status=RequestStatus::RUNNING;if(State::hardware(output.kind))++s.gpu;else ++s.cpu;return Result<void>::success();
  }
  return Result<void>::failure({EAGAIN,"SCHEDULER","no ready work"});
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SCHEDULER","dispatch allocation"});}catch(...){return Result<void>::failure({EIO,"SCHEDULER","lowering/clock exception"});}
}
Result<void> Scheduler::complete(const WorkOutcome&o){
 try{
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto it=s.index.find(State::key(o.request));if(it==s.index.end())return Result<void>::failure({ENOENT,"SCHEDULER","unknown completion"});auto&e=*it->second;
 if(!e.active||!e.head||e.head->node!=o.node||e.next!=o.work_index)return Result<void>::failure({EALREADY,"SCHEDULER","not current active work"});
 if((o.status!=RequestStatus::COMPLETED&&o.status!=RequestStatus::FAILED&&o.status!=RequestStatus::CANCELLED)||(o.status==RequestStatus::COMPLETED?o.result!=0:o.result==0))return Result<void>::failure({EINVAL,"SCHEDULER","completion status/result mismatch"});
 bool hw=State::hardware(e.head->kind);if(!o.retired)return Result<void>::failure({EBUSY,"SCHEDULER","no retirement proof"});
 if(e.head->kind==WorkKind::LEGACY_EXCLUSIVE){
  if(o.completion||o.acknowledged||!o.legacy||!o.legacy->aggregate_complete||!o.legacy->quiescent||!o.legacy->device_generation||(o.legacy->profile_id!=4&&o.legacy->profile_id!=6)||!nonzero_digest(o.legacy->result_sha)||!nonzero_digest(o.legacy->info_sha))return Result<void>::failure({EBUSY,"SCHEDULER","independent legacy aggregate retirement required; no invented v2 sequence"});
 }else{
  if(o.legacy)return Result<void>::failure({EINVAL,"SCHEDULER","legacy proof on non-legacy work"});
  if(hw&&(!o.acknowledged||!o.completion||!o.completion->sequence||o.completion->result!=o.result))return Result<void>::failure({EBUSY,"SCHEDULER","no v2 ACK and retirement proof"});
 }
 if(hw&&e.head->kind==WorkKind::GPU&&o.completion->cookie!=gpu_work_cookie(*e.head))return Result<void>::failure({ESTALE,"SCHEDULER","completion cookie differs"});
 if(s.completion_order==UINT64_MAX)return Result<void>::failure({EOVERFLOW,"SCHEDULER","completion order"});
 auto&slot=s.slots[e.completion_slot];if(!slot.held||slot.value)return Result<void>::failure({EIO,"SCHEDULER","completion reservation lost"});
 try{slot.value=o;if(e.status==RequestStatus::CANCEL_PENDING){slot.value->status=RequestStatus::CANCELLED;slot.value->result=ECANCELED;}}catch(...){slot.value.reset();return Result<void>::failure({ENOMEM,"SCHEDULER","completion allocation; active retained"});}
 auto released=s.budget->release(e.token);if(!released){slot.value.reset();return released;}e.token=0;slot.held=false;slot.order=++s.completion_order;e.active=false;if(hw)--s.gpu;else --s.cpu;e.head.reset();++e.next;
 if(e.status==RequestStatus::CANCEL_PENDING||o.status==RequestStatus::CANCELLED)s.terminalize(e,RequestStatus::CANCELLED,ECANCELED);
 else if(o.status==RequestStatus::FAILED)s.terminalize(e,RequestStatus::FAILED,o.result);
 else if(e.next==e.count)s.terminalize(e,RequestStatus::COMPLETED,0);
 else e.status=RequestStatus::QUEUED;
 return Result<void>::success();
 }catch(const std::bad_alloc&){return Result<void>::failure({ENOMEM,"SCHEDULER","allocation"});}
}
}
