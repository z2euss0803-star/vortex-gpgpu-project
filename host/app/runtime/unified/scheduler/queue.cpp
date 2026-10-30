#include "state.hpp"
namespace vxunified {
Scheduler::Scheduler(std::shared_ptr<PendingBudget>b,std::function<uint64_t()>clock,bool whole):state_(std::make_shared<State>()){state_->budget=std::move(b);state_->clock=clock?std::move(clock):State::monotonic;state_->whole_requests=whole;}
Scheduler::~Scheduler(){try{if(shutdown())return;}catch(...){/* Retain even when error construction fails. */}state_->exit_retention.retain(state_);}
Result<RequestKey> Scheduler::submit(const WorkRequest&r){
 auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);
 try{
  if(!s.budget||!State::identifier(r.request.boot_id)||!State::identifier(r.request.client_id)||!State::identifier(r.request.request_id)||!nonzero_digest(r.model)||!nonzero_digest(r.plan)||!nonzero_digest(r.input_sha)||r.input_bytes>(1ULL<<30)||!r.source||static_cast<unsigned>(r.app)<1||static_cast<unsigned>(r.app)>3)return Result<RequestKey>::failure({EINVAL,"SCHEDULER","request identity or bounds"});
  if(!s.boot.empty()&&s.boot!=r.request.boot_id)return Result<RequestKey>::failure({ESTALE,"SCHEDULER","boot epoch differs"});
  auto source_id=r.source->identity();auto count=r.source->size();if(!nonzero_digest(source_id)||!count||count>0xfffffffeULL)return Result<RequestKey>::failure({EINVAL,"SCHEDULER","unbound source"});
  auto key=State::key(r.request);auto found=s.index.find(key);
  if(found!=s.index.end()){auto&e=*found->second;auto&old=e.request;if(e.source_id!=source_id||e.count!=count||old.app!=r.app||old.model!=r.model||old.plan!=r.plan||old.input_sha!=r.input_sha||old.input_bytes!=r.input_bytes||old.queue_deadline_ns!=r.queue_deadline_ns)return Result<RequestKey>::failure({EEXIST,"SCHEDULER","request id reused for different payload"});return Result<RequestKey>::success(r.request);}
  if(s.closing)return Result<RequestKey>::failure({ESHUTDOWN,"SCHEDULER","closed admission"});
  if(s.ring.size()>=8)return Result<RequestKey>::failure({EAGAIN,"SCHEDULER","request queue8"});
  if(s.index.size()>=256)return Result<RequestKey>::failure({ENOSPC,"SCHEDULER","idempotency index256; explicit forget required"});
  uint64_t now=s.clock();if(!now)return Result<RequestKey>::failure({EIO,"SCHEDULER","clock"});if(r.queue_deadline_ns&&now>=r.queue_deadline_ns)return Result<RequestKey>::failure({ETIMEDOUT,"SCHEDULER","expired before admission"});
  auto e=std::make_shared<State::Entry>();e->key=key;e->request=r;e->source_id=source_id;e->count=count;auto item=s.load(*e);if(!item)return Result<RequestKey>::failure(*item.error);e->head=*item.value;e->legacy=e->head->kind==WorkKind::LEGACY_EXCLUSIVE;
  auto reserved=s.reserve(*e);if(!reserved)return Result<RequestKey>::failure(*reserved.error);
  try{s.index.emplace(key,e);s.ring.push_back(key);if(s.boot.empty())s.boot=r.request.boot_id;}catch(...){s.index.erase(key);auto it=std::find(s.ring.begin(),s.ring.end(),key);if(it!=s.ring.end())s.ring.erase(it);s.budget->release(e->token);throw;}
  return Result<RequestKey>::success(r.request);
 }catch(const std::bad_alloc&){return Result<RequestKey>::failure({ENOMEM,"SCHEDULER","admission allocation"});}catch(...){return Result<RequestKey>::failure({EIO,"SCHEDULER","source/clock exception"});}
}
Result<RequestStatus> Scheduler::status(RequestKey k)const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto it=s.index.find(State::key(k));if(it==s.index.end())return Result<RequestStatus>::failure({ENOENT,"SCHEDULER","unknown request"});return Result<RequestStatus>::success(it->second->status);}
Result<int> Scheduler::request_error(RequestKey k)const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto it=s.index.find(State::key(k));if(it==s.index.end())return Result<int>::failure({ENOENT,"SCHEDULER","unknown request"});return Result<int>::success(it->second->error);}
Result<WorkOutcome> Scheduler::take_completion(){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);State::Slot*first=nullptr;for(auto&slot:s.slots)if(slot.value&&(!first||slot.order<first->order))first=&slot;if(!first)return Result<WorkOutcome>::failure({EAGAIN,"SCHEDULER","no completion"});try{auto out=*first->value;first->value.reset();first->order=0;return Result<WorkOutcome>::success(std::move(out));}catch(...){return Result<WorkOutcome>::failure({ENOMEM,"SCHEDULER","completion delivery allocation"});}}
Result<void> Scheduler::forget(RequestKey k){auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);auto it=s.index.find(State::key(k));if(it==s.index.end())return Result<void>::failure({ENOENT,"SCHEDULER","unknown request"});if(!State::terminal(it->second->status))return Result<void>::failure({EBUSY,"SCHEDULER","request still live"});for(auto&slot:s.slots)if(slot.value&&State::key(slot.value->request)==State::key(k))return Result<void>::failure({EBUSY,"SCHEDULER","completion not consumed"});s.index.erase(it);return Result<void>::success();}
SchedulerStats Scheduler::stats()const{auto&s=*state_;std::lock_guard<std::mutex>lock(s.mutex);SchedulerStats out;out.requests=s.ring.size();out.indexed=s.index.size();out.active_gpu=s.gpu;out.active_cpu=s.cpu;out.pending=s.budget?s.budget->used():0;out.completions=s.completion_count();out.reserved_completions=s.reserved_count();out.closing=s.closing;return out;}
}
